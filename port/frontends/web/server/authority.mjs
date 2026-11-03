// Authoritative online duels: the server runs the only engine and each client only renders.
//
// Clients send their decks and their answers to their own requests. The server shuffles each
// deck list (deck indexes then say nothing about card identities), picks the RNG seed, runs
// the duel, checks that every answer comes from the duelist who was asked, and sends each
// duelist a stream of items with what that duelist may see (src/js/duel_view.js):
//
//   {k:'ev', ev, s, c, l}            engine event + board changes
//   {k:'req', id, own, act, req, v}  a request: own = asked of this duelist; act = needs an
//                                    answer {t:'answer', id, v} (ACKs and view-only card lists
//                                    are answered by the server at once and only shown); v is
//                                    filled in once answered (replayed streams skip it).
//                                    own = false: the other duelist's text box (advances alone)
//   {k:'wait', id, req:{type, duelist}}   the other duelist is choosing
//   {k:'sync', s, c}                 board changes from steps this duelist does not see
//   {k:'end', result, finished, turns} or {k:'end', error}
//
// Events are grouped into spans: everything up to and including the next request. The asked
// duelist sees the whole span (it is their screen in the original). The other duelist sees it
// only if it shows something public (coin, damage, animation, turn, box message, a card put
// into play or shown by a public effect, or any change of their view of the board) and reveals
// no card they cannot see: a card drawn, a card page, a card list of a hidden zone. Otherwise
// they get the board changes alone.
import path from 'node:path';
import crypto from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';

const MAX_ITEMS = 200000;

export async function loadAuthority(dist) {
	const load = (name) => import(pathToFileURL(path.join(dist, 'js', name)).href);
	const engine = await load('engine.js');
	const view = await load('duel_view.js');
	const data = await load('data.js');
	const module = await engine.compileModule(await readFile(path.join(dist, 'tcg.wasm')));
	const statics = new engine.Duel(module);
	const numCards = statics.numCards;
	const lib = { engine, view, module };
	return {
		/* problems with a submitted deck (empty if it can be played) */
		checkDeck(cards) {
			if (!Array.isArray(cards) || cards.length !== engine.C.DECK_SIZE) return [`A deck needs ${engine.C.DECK_SIZE} cards.`];
			if (!cards.every((id) => Number.isInteger(id) && id >= 1 && id <= numCards)) return ['The deck contains unknown cards.'];
			return data.validateDeck(cards, (id) => statics.card(id));
		},
		createDuel: (opts) => new AuthoritativeDuel(lib, opts),
	};
}

function shuffled(list) {
	const a = list.slice();
	for (let i = a.length - 1; i > 0; i--) {
		const j = crypto.randomInt(i + 1);
		[a[i], a[j]] = [a[j], a[i]];
	}
	return a;
}

export class AuthoritativeDuel {
	/* decks: [hostCards, guestCards]; names: [host, guest]; emit(viewer 0|1, item) */
	constructor(lib, { decks, names, emit, seed, skipDelayAllowed = false }) {
		const { engine, view } = lib;
		this.E = engine;
		this.V = view;
		this.duel = new engine.Duel(lib.module);
		this.setup = {
			duelists: [0, 1].map((i) => ({ kind: 'human', cards: shuffled(decks[i]), name: names[i] })),
			prizes: 6, seed: seed ?? crypto.randomInt(1 << 24), skipDelayAllowed,
		};
		this.emit = emit || (() => {});
		this.logs = [[], []];
		this.span = [];
		this.prev = null;             // raw board at the end of the previous span
		this.sent = [null, null];     // per viewer: the redacted duel variables it has
		this.known = [new Set(), new Set()]; // per viewer: side * 64 + deck index of identities sent
		this.revealed = false;        // setups are face down until the first turn
		this.screen = 0;
		this.nextId = 0;
		this.pending = null;
		this.result = null;
		const { T } = engine;
		this.publicCaptions = new Set([
			T.UsedText, T.WasPlacedInTheHandText, T.CardWasChosenText, T.PokemonWasReturnedToDeckText,
			T.PokemonWasReturnedFromArenaToHandText, T.PokemonWasReturnedFromBenchToHandText,
			T.CameToTheBenchText, T.AttachedEnergyToPokemonText, T.PlacedOnTheBenchText,
			T.PlacedInTheArenaText, T.DuelistPlacedACardText,
		]);
	}

	start() {
		const { DuelAborted } = this.E;
		this.done = this.duel.run(this.setup, {
			onEvent: (ev) => this.onEvent(ev),
			onRequest: (req) => this.onRequest(req),
		}).then((res) => {
			this.closeSpan(null);
			this.finish({ result: res.result, finished: res.finished, turns: res.turns });
		}, (err) => {
			if (err instanceof DuelAborted) { this.result = { aborted: String(err.message) }; return; }
			this.closeSpan(null);
			this.finish({ error: String((err && err.message) || err) });
		});
		return this.done;
	}

	finish(result) {
		this.result = result;
		for (const v of [0, 1]) this.push(v, { k: 'end', ...result });
	}

	abort(reason = 'room closed') {
		this.duel.abort(reason);
	}

	/* the asked duelist's answer; returns an error message, or null (also for stale duplicates) */
	answer(viewer, id, v) {
		const p = this.pending;
		if (!p || id !== p.id) return Number.isInteger(id) && id >= 0 && id < this.nextId ? null : 'no such request';
		if (viewer !== p.side) return 'not your request';
		const answer = Number.isInteger(v) && v >= -1 && v <= 255 ? v : this.E.TCG_CANCEL;
		this.pending = null;
		if (p.item) p.item.v = answer;
		p.resolve(answer);
		return null;
	}

	/* items of a viewer's stream from index `from` (rejoin) */
	itemsFrom(viewer, from = 0) {
		return this.logs[viewer].slice(Math.max(0, from | 0));
	}

	// ---------------------------------------------------------------- engine callbacks
	capture() {
		const { TCG_PLAYER, TCG_OPPONENT } = this.E;
		return {
			dv: [Uint8Array.from(this.duel.duelvars(TCG_PLAYER)), Uint8Array.from(this.duel.duelvars(TCG_OPPONENT))],
			revealed: this.revealed,
		};
	}

	tempList(count) {
		const w = this.duel.wram(), base = this.E.W.wDuelTempList - 0xc000, out = [];
		for (let i = 0; i < this.E.C.DECK_SIZE && w[base + i] !== 0xff; i++) out.push(w[base + i]);
		return count ? out.slice(0, count) : out;
	}

	onEvent(ev) {
		const { EV, SCREEN } = this.E;
		if (ev.type === EV.TURN) this.revealed = true;
		const entry = { ev: { type: ev.type, arg: ev.arg.slice(), text: ev.text, str: ev.str }, snap: this.capture() };
		if (ev.type === EV.SCREEN) {
			this.screen = ev.arg[0];
			if (ev.arg[0] === SCREEN.CARD_LIST) entry.list = this.tempList(ev.arg[2]);
		}
		this.span.push(entry);
	}

	onRequest(req) {
		const { REQ, TCG_CANCEL } = this.E;
		const local = req.type === REQ.ACK || (req.type === REQ.CARD_LIST && req.arg[0] === 1);
		const entry = { req, snap: this.capture(), id: this.nextId++, local, screen: this.screen };
		this.span.push(entry);
		this.closeSpan(entry);
		if (this.logs[0].length + this.logs[1].length > MAX_ITEMS) this.duel.abort('duel too long');
		if (local) return req.type === REQ.ACK ? 0 : TCG_CANCEL;
		return new Promise((resolve) => {
			this.pending = { id: entry.id, side: this.V.sideIndex(req.duelist), resolve, item: entry.ownerItem };
		});
	}

	// ---------------------------------------------------------------- per-viewer streams
	push(viewer, item) {
		item.n = this.logs[viewer].length;
		this.logs[viewer].push(item);
		this.emit(viewer, item);
	}

	closeSpan(term) {
		const span = this.span;
		this.span = [];
		if (!span.length) return;
		const end = span[span.length - 1].snap;
		for (const v of [0, 1]) this.deliver(v, span, term, end);
		this.prev = end;
	}

	deliver(v, span, term, end) {
		const { REQ } = this.E;
		const events = term ? span.slice(0, -1) : span;
		if (term && this.V.sideIndex(term.req.duelist) === v) {
			for (const e of events) this.push(v, this.eventItem(v, e, this.ownerReveals(e), true));
			const item = { k: 'req', id: term.id, own: true, act: !term.local, req: wireRequest(term.req), ...this.stateItem(v, term.snap, this.requestReveals(term)) };
			if (!term.local) term.ownerItem = item;
			this.push(v, item);
			return;
		}
		const reveals = this.publicReveals(v, events, end);
		if (reveals) {
			for (const e of events) this.push(v, this.eventItem(v, e, reveals.get(e) || [], reveals.has(e)));
			if (term && term.req.type === REQ.ACK) this.push(v, { k: 'req', id: term.id, own: false, act: false, req: remoteAck(term.req), ...this.stateItem(v, term.snap, []) });
			else if (term && !term.local) this.push(v, this.waitItem(v, term));
			return;
		}
		if (term && !term.local) { this.push(v, this.waitItem(v, term)); return; }
		const sync = this.stateItem(v, end, []);
		if (sync.s || sync.c) this.push(v, { k: 'sync', ...sync });
	}

	eventItem(v, e, reveals, withList) {
		const item = { k: 'ev', ev: e.ev, ...this.stateItem(v, e.snap, reveals) };
		if (e.list && withList) item.l = e.list;
		return item;
	}

	waitItem(v, term) {
		return { k: 'wait', id: term.id, req: { type: term.req.type, duelist: term.req.duelist }, ...this.stateItem(v, term.snap, []) };
	}

	/* identities the asked duelist's own screen shows */
	ownerReveals(e) {
		const { EV, SCREEN } = this.E, a = e.ev.arg, side = this.V.sideIndex(a[1]);
		if (e.ev.type === EV.CARD && a[3] === 0 && a[0] < 60) return [[side, a[0]]];
		if (e.ev.type === EV.SCREEN && a[0] === SCREEN.CARD_LIST && e.list) return e.list.map((idx) => [side, idx]);
		return [];
	}

	requestReveals(term) {
		const { REQ, MENU, SCREEN } = this.E, req = term.req, n = req.numItems, out = [];
		const side = this.V.sideIndex(req.shown);
		const add = (idx) => { if (idx < 60) out.push([side, idx]); };
		switch (req.type) {
		case REQ.CARD_LIST: case REQ.ENERGY_DISCARD:
			for (let i = 0; i < n; i++) add(req.items[i]);
			break;
		case REQ.CARD_ITEM_MENU:
			add(req.arg[0]);
			break;
		case REQ.MENU:
			if (req.menu === MENU.ATTACKS) for (let i = 0; i < n; i++) add(req.items[i * 2]);
			else if (req.menu === MENU.CARD_LIST || (req.menu === MENU.GENERIC && term.screen === SCREEN.CARD_LIST))
				for (let i = 0; i < n; i++) add(req.items[i]);
			break;
		default:
			break;
		}
		return out;
	}

	/* for the duelist who was not asked: Map(event entry -> identities it shows), or null if
	 * the span shows nothing public or reveals a card this duelist cannot see */
	publicReveals(v, events, end) {
		const { EV, SCREEN } = this.E, { sideIndex, locationVisible, PUBLIC_SIGNALS, isBoxMessage } = this.V;
		const reveals = new Map(), shownIds = new Set();
		let signal = false;
		for (const e of events) {
			const ev = e.ev, a = ev.arg;
			if (ev.type !== EV.CARD) continue;
			if (a[3] === 1 || a[0] >= 60) return null; // card page: someone browsing
			const side = sideIndex(a[1]);
			const seen = locationVisible(end.dv[side][a[0]], side === v, end.revealed) || (e.snap.revealed && this.publicCaptions.has(ev.text));
			if (!seen) return null;
			reveals.set(e, [[side, a[0]]]);
			shownIds.add(a[2]);
			signal = true;
		}
		for (const e of events) {
			const ev = e.ev, a = ev.arg;
			if (ev.type === EV.SCREEN && a[0] === SCREEN.LARGE_CARD && !shownIds.has(a[2])) return null;
			if (ev.type === EV.SCREEN && a[0] === SCREEN.CARD_LIST && e.list) {
				const side = sideIndex(a[1]);
				if (!e.list.every((idx) => idx < 60 && locationVisible(end.dv[side][idx], side === v, end.revealed))) return null;
				reveals.set(e, e.list.map((idx) => [side, idx]));
			}
			if (PUBLIC_SIGNALS.has(ev.type) || isBoxMessage(ev)) signal = true;
		}
		if (!signal && !this.viewChanged(v, this.prev, end)) return null;
		return reveals;
	}

	redacted(v, snap) {
		return [0, 1].map((side) => this.V.redactDuelvars(snap.dv[side], side === v, snap.revealed));
	}

	viewChanged(v, a, b) {
		if (!a || a.revealed !== b.revealed) return true;
		const x = this.redacted(v, a), y = this.redacted(v, b);
		return [0, 1].some((side) => !Buffer.from(x[side]).equals(Buffer.from(y[side])));
	}

	/* changes to viewer v's board copy and card identities up to snapshot `snap` */
	stateItem(v, snap, reveals) {
		const out = {}, s = [], c = [];
		const prev = this.sent[v], cur = this.redacted(v, snap);
		for (const side of [0, 1]) for (let off = 0; off < 256; off++)
			if (!prev || prev[side][off] !== cur[side][off]) s.push(side, off, cur[side][off]);
		this.sent[v] = cur;
		const known = this.known[v];
		const add = (side, idx) => {
			const key = side * 64 + idx;
			if (known.has(key)) return;
			const id = this.duel.cardId(this.V.sideOf(side), idx);
			if (!id) return;
			known.add(key);
			c.push(side, idx, id);
		};
		for (const side of [0, 1]) for (let idx = 0; idx < 60; idx++)
			if (this.V.locationVisible(snap.dv[side][idx], side === v, snap.revealed)) add(side, idx);
		for (const [side, idx] of reveals) add(side, idx);
		if (s.length) out.s = s;
		if (c.length) out.c = c;
		return out;
	}
}

/* the request as the asked client needs it (DuelUI.onRequest) */
function wireRequest(req) {
	return {
		type: req.type, duelist: req.duelist, shown: req.shown, menu: req.menu, numItems: req.numItems, cursor: req.cursor,
		text: req.text, text2: req.text2, str: req.str, textStr: req.textStr, text2Str: req.text2Str,
		items: Array.from(req.items), arg: req.arg.slice(),
	};
}

/* the other duelist's text box: shown, then advanced without input */
function remoteAck(req) {
	return { type: req.type, duelist: req.duelist, shown: req.shown, menu: 0, numItems: 0, cursor: 0, text: 0, text2: 0, str: null, textStr: '', text2Str: '', items: [], arg: [0, 0, 0, 0] };
}
