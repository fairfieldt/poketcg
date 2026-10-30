// Online duels through the relay server (server/server.js).
//
// Both clients run the same deterministic engine with the same setup. The host is the
// TCG_PLAYER side, the guest the TCG_OPPONENT side; both duelists are TCG_HUMAN. For every
// non-ACK request, the duelist it belongs to answers it in its UI and sends
//   {t:'answer', seq, rt, v, h}
// seq = number of non-ACK requests before this one, rt = request type, v = answer,
// h = engine state hash at the request (duel.stateHash()). The other client waits for the
// answer with its own seq, and checks rt and h against its own request: any mismatch is a desync.
// TCG_REQ_ACK requests never change engine state: they are answered locally and never sent.
//
// After a disconnect the client re-attaches with {t:'rejoin', room, token}; the server answers
// with the setup and the whole answer log, so a client that lost its engine (page reload) replays
// the duel up to the current point, and one that kept it picks up any answers it missed.
import { REQ, TCG_PLAYER, TCG_OPPONENT } from './engine.js';

const SESSION_KEY = 'tcg.online';

export class NetError extends Error {}

export class RelayClient {
	constructor(url) {
		this.url = url;
		this.ws = null;
		this.handlers = new Map();
		this.closedByUser = false;
		this.queue = [];
		this.reconnectDelay = 1000;
	}

	on(type, fn) { this.handlers.set(type, fn); return this; }

	emit(type, msg) {
		const fn = this.handlers.get(type);
		if (fn) fn(msg);
	}

	connect() {
		return new Promise((resolve, reject) => {
			let opened = false;
			const ws = new WebSocket(this.url);
			this.ws = ws;
			ws.onopen = () => {
				opened = true;
				this.reconnectDelay = 1000;
				this.emit('open');
				resolve();
			};
			ws.onmessage = (e) => {
				let m;
				try { m = JSON.parse(e.data); } catch { return; }
				this.emit(m.t, m);
				this.emit('*', m);
			};
			ws.onerror = () => { if (!opened) reject(new NetError(`cannot connect to ${this.url}`)); };
			ws.onclose = () => {
				if (this.ws !== ws) return;
				this.ws = null;
				if (opened) this.emit('close');
				if (!opened) reject(new NetError(`cannot connect to ${this.url}`));
			};
		});
	}

	get connected() { return !!(this.ws && this.ws.readyState === 1); }

	send(msg) {
		if (this.connected) { this.ws.send(JSON.stringify(msg)); return true; }
		return false;
	}

	close() {
		this.closedByUser = true;
		if (this.ws) { try { this.ws.close(); } catch { /* ignore */ } }
		this.ws = null;
	}
}

export function relayUrl() {
	const loc = globalThis.location;
	const proto = loc.protocol === 'https:' ? 'wss:' : 'ws:';
	return `${proto}//${loc.host}/ws`;
}

export function saveSession(s) {
	try { sessionStorage.setItem(SESSION_KEY, JSON.stringify(s)); } catch { /* ignore */ }
}
export function loadSession() {
	try { return JSON.parse(sessionStorage.getItem(SESSION_KEY) || 'null'); } catch { return null; }
}
export function clearSession() {
	try { sessionStorage.removeItem(SESSION_KEY); } catch { /* ignore */ }
}

/**
 * The request handler of an online duel. ui = DuelUI, duel = engine Duel, client = RelayClient.
 * role = 'host' | 'guest'. log = answers already known (rejoin), replayed without UI.
 */
export class OnlineDuel {
	constructor({ ui, duel, client, role, peerName, log = [], onDesync, onStatus }) {
		this.ui = ui;
		this.duel = duel;
		this.client = client;
		this.role = role;
		this.localSide = role === 'host' ? TCG_PLAYER : TCG_OPPONENT;
		this.peerName = peerName || 'opponent';
		this.seq = 0;                 // next non-ACK request number
		this.inbox = new Map();       // seq -> answer message (from the peer or the log)
		this.sent = new Map();        // seq -> answer message we sent (resent after a reconnect)
		this.waiter = null;
		this.replayUntil = 0;
		this.onDesync = onDesync || (() => {});
		this.onStatus = onStatus || (() => {});
		this.failed = null;
		this.addLog(log);
		this.replayUntil = log.length;
		if (this.replayUntil) this.ui.skipping = true;
	}

	/* answers from a rejoin log or relayed by the server */
	addLog(entries) {
		for (const m of entries) {
			if (m.seq < this.seq) continue;
			if (!this.inbox.has(m.seq)) this.inbox.set(m.seq, m);
		}
		this.wake();
	}

	receive(m) {
		if (m.seq < this.seq) return; // already consumed (duplicate after a rejoin)
		this.inbox.set(m.seq, m);
		this.wake();
	}

	/* after a reconnect: resend our answers the server may not have logged */
	resendFrom(logLength) {
		for (const [seq, m] of this.sent) if (seq >= logLength) this.client.send(m);
	}

	wake() {
		if (this.waiter && this.inbox.has(this.waiter.seq)) {
			const w = this.waiter;
			this.waiter = null;
			w.resolve(this.inbox.get(w.seq));
		}
	}

	fail(err) {
		this.failed = err;
		if (this.waiter) { const w = this.waiter; this.waiter = null; w.reject(err); }
	}

	waitFor(seq) {
		if (this.failed) return Promise.reject(this.failed);
		if (this.inbox.has(seq)) return Promise.resolve(this.inbox.get(seq));
		return new Promise((resolve, reject) => { this.waiter = { seq, resolve, reject }; });
	}

	check(m, seq, req, h) {
		if (m.seq !== seq || m.rt !== req.type || (m.h >>> 0) !== h) {
			const detail = `request #${seq}: expected type ${req.type} hash ${h.toString(16)}, got type ${m.rt} hash ${(m.h >>> 0).toString(16)}`;
			this.client.send({ t: 'desync', seq, detail });
			const err = new NetError(`Desync detected (${detail})`);
			this.onDesync(err);
			throw err;
		}
	}

	onEvent(ev) {
		return this.ui.onEvent(ev);
	}

	async onRequest(req) {
		const replaying = this.seq < this.replayUntil;
		if (req.type === REQ.ACK) {
			if (replaying) return 0;
			return this.ui.onRequest(req, { remote: req.duelist !== this.localSide });
		}
		const seq = this.seq;
		const h = this.duel.stateHash();
		let v;
		if (replaying || this.inbox.has(seq)) {
			// known answer (rejoin log): ours or the peer's
			const m = this.inbox.get(seq);
			this.check(m, seq, req, h);
			v = m.v;
		} else if (req.duelist === this.localSide) {
			v = await this.ui.onRequest(req);
			const m = { t: 'answer', seq, rt: req.type, v, h };
			this.sent.set(seq, m);
			if (!this.client.send(m)) this.onStatus('Not connected: your move will be sent when the connection is back.', 'warn');
		} else {
			this.ui.render();
			this.ui.showWaiting(req, this.peerName);
			const m = await this.waitFor(seq);
			this.check(m, seq, req, h);
			this.ui.setStatus('');
			v = m.v;
		}
		this.inbox.delete(seq);
		this.seq = seq + 1;
		if (this.ui.skipping && this.seq >= this.replayUntil) {
			this.ui.skipping = false;
			this.ui.render();
		}
		return v;
	}
}
