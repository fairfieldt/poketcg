// Online duels the server runs (server/authority.mjs): this client runs no engine. It plays its
// item stream through DuelUI, with a RemoteDuel (duel_view.js) as the board DuelUI reads, and
// sends the answers to its own requests.
//
// Items are numbered; after a reconnect the client asks for the stream from the first item it
// has not received. A page reload starts from item 0 and fast-forwards (DuelUI.skipping) through
// the items received at rejoin, then resends an answer the server may have missed.
import { REQ } from './engine.js';

export class ServerDuelSession {
	constructor({ ui, remote, client, peerName, onStatus }) {
		this.ui = ui;
		this.remote = remote;
		this.client = client;
		this.peerName = peerName || 'opponent';
		this.onStatus = onStatus || (() => {});
		this.items = [];
		this.next = 0;          // index of the next item to play
		this.catchUp = 0;       // items below this index replay without pacing
		this.waiter = null;
		this.failed = null;
		this.waiting = false;   // showing "Waiting for ..."
		this.sentAnswer = null; // { n, id, v } until a later item arrives
		this.current = null;    // the item being played
	}

	/* items received so far: where a rejoin continues the stream */
	get received() { return this.items.length; }

	receive(item) {
		if (item.n !== this.items.length) return; // duplicate after a rejoin
		this.items.push(item);
		if (this.sentAnswer && item.n > this.sentAnswer.n) this.sentAnswer = null;
		this.wake();
	}

	/* items of a rejoin; fastForward: this page has not shown them (reload) */
	receiveAll(items, fastForward = false) {
		const end = items.length ? items[items.length - 1].n + 1 : 0;
		if (fastForward) this.catchUp = Math.max(this.catchUp, end);
		for (const item of items) this.receive(item);
		if (this.sentAnswer) this.client.send({ t: 'answer', id: this.sentAnswer.id, v: this.sentAnswer.v });
	}

	fail(err) {
		this.failed = err;
		if (this.waiter) { const w = this.waiter; this.waiter = null; w.reject(err); }
	}

	wake() {
		if (this.waiter && this.next < this.items.length) {
			const w = this.waiter;
			this.waiter = null;
			w.resolve(this.items[this.next++]);
		}
	}

	nextItem() {
		if (this.failed) return Promise.reject(this.failed);
		if (this.next < this.items.length) return Promise.resolve(this.items[this.next++]);
		return new Promise((resolve, reject) => { this.waiter = { resolve, reject }; });
	}

	/* plays the stream; resolves with the end item */
	async run() {
		for (;;) {
			const item = await this.nextItem();
			const replay = item.n < this.catchUp;
			if (this.ui.skipping !== replay) {
				this.ui.skipping = replay;
				if (!replay) this.ui.render();
			}
			const end = await this.play(item, replay);
			if (end) return end;
		}
	}

	clearWaiting() {
		if (!this.waiting) return;
		this.waiting = false;
		this.ui.setStatus('');
	}

	async play(item, replay) {
		const ui = this.ui;
		this.current = item;
		this.remote.apply(item);
		switch (item.k) {
		case 'ev':
			this.clearWaiting();
			await ui.onEvent(item.ev);
			return null;
		case 'sync':
			ui.render();
			return null;
		case 'wait':
			ui.render();
			if (!replay || item.n === this.items.length - 1) {
				ui.showWaiting(item.req, this.peerName);
				this.waiting = true;
			}
			return null;
		case 'req':
			this.clearWaiting();
			await this.request(item, replay);
			return null;
		case 'end':
			this.clearWaiting();
			ui.skipping = false;
			ui.render();
			return item;
		default:
			return null;
		}
	}

	async request(item, replay) {
		const ui = this.ui, req = item.req;
		if (!item.own) { await ui.onRequest(req, { remote: true }); return; }
		if (!item.act) {
			if (replay) return;
			if (req.type === REQ.ACK) { await ui.onRequest(req); return; }
			// view-only card list: the server did not wait for it; open card pages here
			for (;;) {
				const i = await ui.onRequest(req);
				if (!(i >= 0 && i < req.numItems)) return;
				await ui.showCardPage(this.remote.card(this.remote.cardId(req.shown, req.items[i])));
			}
		}
		if (item.v !== undefined) return; // answered before this page was loaded
		if (ui.skipping) { ui.skipping = false; ui.render(); }
		const v = await ui.onRequest(req);
		item.v = v;
		this.sentAnswer = { n: item.n, id: item.id, v };
		if (!this.client.send({ t: 'answer', id: item.id, v })) this.onStatus('Not connected: your move will be sent when the connection is back.', 'warn');
	}
}
