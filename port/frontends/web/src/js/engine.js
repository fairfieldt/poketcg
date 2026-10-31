// JavaScript API of the WebAssembly duel engine (works in browsers and in Node).
//
//   const duel = await Engine.load();              // or Engine.load(bytes | url)
//   const result = await duel.run(setup, {
//     onEvent(ev) { ... },                           // may return a Promise (pacing)
//     async onRequest(req) { return answer; },       // may return a value or a Promise
//   });
//
// The engine calls request() synchronously; when onRequest/onEvent return a Promise the whole
// wasm stack is suspended with Binaryen's Asyncify (asyncify_start_unwind ... stop_unwind),
// the promise is awaited, and the stack is restored (asyncify_start_rewind + calling web_run
// again) with the answer. Every run() uses a fresh wasm instance, so an aborted duel (a
// tcg_fatal from an unported stub, or duel.abort()) never leaves stale state behind.

import { C, W, T } from './gen/consts.js';

export { C, W, T };

export class TcgFatal extends Error {
	constructor(msg) { super(msg); this.name = 'TcgFatal'; }
}
export class DuelAborted extends Error {
	constructor(msg = 'duel aborted') { super(msg); this.name = 'DuelAborted'; }
}

export const TCG_PLAYER = 0xc2, TCG_OPPONENT = 0xc3, TCG_CANCEL = -1;
export const TCG_HUMAN = 0, TCG_AI = 1;
export const TCG_SAVE_SIZE = 1660, TCG_CONTINUE_BROKEN = -1, TCG_SAMS_PRACTICE_DECK_ID = 0;
export const EV = { TEXT: 1, SCREEN: 2, CARD: 3, ANIM: 4, COIN: 5, DAMAGE: 6, SFX: 7, MUSIC: 8, WAIT: 9, TURN: 10, DUEL_END: 11, THINKING: 12 };
export const TEXT_MODE = { MESSAGE: 0, PRACTICE_CHECKLIST: 1, PRACTICE_SPEECH: 2 };
export const SCREEN = { NONE: 0, MAIN: 1, PLAY_AREA: 2, CARD_LIST: 3, LARGE_CARD: 4, COIN_TOSS: 5, ENERGY_DISCARD: 6, EMPTY: 7, BOX_MESSAGE: 8, OTHER: 9 };
export const REQ = { ACK: 1, DUEL_MENU: 2, CARD_LIST: 3, CARD_ITEM_MENU: 4, PLAY_AREA: 5, MENU: 6, YES_NO: 7, ENERGY_DISCARD: 8, PRIZE: 9, COLOR: 10, NUMBER: 11, CONFIRM: 12 };
export const MENU = { GENERIC: 0, ATTACKS: 1, PLAY_AREA: 2, TWO_ITEMS: 3, CARD_LIST: 4, PEEK_AREA: 5, PEEK_TARGET: 6 };
export const EV_NAMES = Object.fromEntries(Object.entries(EV).map(([k, v]) => [v, k]));
export const REQ_NAMES = Object.fromEntries(Object.entries(REQ).map(([k, v]) => [v, k]));

const ASYNC_NORMAL = 0, ASYNC_UNWINDING = 1, ASYNC_REWINDING = 2;
const ITEMS_COPY = 128;
const utf8 = new TextDecoder('utf-8');
const utf8enc = new TextEncoder();

function isThenable(v) { return v && typeof v.then === 'function'; }

export async function compileModule(source) {
	if (source instanceof WebAssembly.Module) return source;
	if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) return WebAssembly.compile(source);
	const url = source || new URL('../tcg.wasm', import.meta.url);
	if (typeof process !== 'undefined' && process.versions && process.versions.node && String(url).startsWith('file:')) {
		const fs = await import('node:fs/promises');
		return WebAssembly.compile(await fs.readFile(new URL(url)));
	}
	const resp = await fetch(url);
	if (!resp.ok) throw new Error(`cannot load ${url}: ${resp.status}`);
	if (WebAssembly.compileStreaming && (resp.headers.get('content-type') || '').includes('application/wasm'))
		return WebAssembly.compileStreaming(resp);
	return WebAssembly.compile(await resp.arrayBuffer());
}

/* parsed card record (struct CardData, see port/src/common.h) */
function parseCard(id, d, text) {
	const rd16 = (o) => d[o] | (d[o + 1] << 8);
	const type = d[C.CARD_DATA_TYPE];
	const card = {
		id, type, raw: d,
		name: text(rd16(C.CARD_DATA_NAME)), nameText: rd16(C.CARD_DATA_NAME),
		rarity: d[C.CARD_DATA_RARITY], set: d[C.CARD_DATA_SET], gfx: rd16(C.CARD_DATA_GFX),
		isPokemon: type < C.TYPE_ENERGY, isEnergy: type >= C.TYPE_ENERGY && type < C.TYPE_TRAINER, isTrainer: type >= C.TYPE_TRAINER,
	};
	if (card.isPokemon) {
		card.hp = d[C.CARD_DATA_HP];
		card.stage = d[C.CARD_DATA_STAGE];
		card.preEvoName = rd16(C.CARD_DATA_PREEVO_NAME) ? text(rd16(C.CARD_DATA_PREEVO_NAME)) : '';
		card.attacks = [];
		for (const base of [C.CARD_DATA_ATTACK1, C.CARD_DATA_ATTACK2]) {
			const nameId = rd16(base + 4);
			if (!nameId) continue;
			const cost = [];
			for (let i = 0; i < 4; i++) { cost.push(d[base + i] >> 4, d[base + i] & 15); }
			const desc1 = rd16(base + 6), desc2 = rd16(base + 8);
			card.attacks.push({
				index: base === C.CARD_DATA_ATTACK1 ? 0 : 1,
				name: text(nameId), cost: cost.slice(0, 7), // FIRE GRASS LIGHTNING WATER FIGHTING PSYCHIC COLORLESS
				description: [desc1, desc2].filter(Boolean).map(text).join('\n'),
				damage: d[base + 10], category: d[base + 11],
				isPower: (d[base + 11] & 0x7f) === C.POKEMON_POWER,
			});
		}
		card.retreat = d[C.CARD_DATA_RETREAT_COST];
		card.weakness = d[C.CARD_DATA_WEAKNESS];
		card.resistance = d[C.CARD_DATA_RESISTANCE];
		card.category = rd16(C.CARD_DATA_CATEGORY) ? text(rd16(C.CARD_DATA_CATEGORY)) : '';
		card.pokedex = d[C.CARD_DATA_POKEDEX_NUMBER];
		card.level = d[C.CARD_DATA_LEVEL];
		card.length = [d[C.CARD_DATA_LENGTH], d[C.CARD_DATA_LENGTH + 1]];
		card.weight = rd16(C.CARD_DATA_WEIGHT);
		card.description = rd16(C.CARD_DATA_PKMN_DESCRIPTION) ? text(rd16(C.CARD_DATA_PKMN_DESCRIPTION)) : '';
	} else {
		// long non-Pokemon descriptions have a second page (wLoadedCard1NonPokemonDescription + 2)
		const desc = rd16(C.CARD_DATA_NONPKMN_DESCRIPTION), desc2 = rd16(C.CARD_DATA_NONPKMN_DESCRIPTION + 2);
		card.description = [desc, desc2].filter(Boolean).map(text).join('\n');
	}
	return card;
}

export class Instance {
	constructor(module, imports) {
		this.instance = new WebAssembly.Instance(module, imports);
		this.x = this.instance.exports;
		if (this.x._initialize) this.x._initialize();
		this.asyncData = this.x.web_asyncify_data();
		this.asyncSize = this.x.web_asyncify_data_size();
	}
	get u8() { return new Uint8Array(this.x.memory.buffer); }
	cstr(ptr, max = 4096) {
		if (!ptr) return null;
		const m = this.u8;
		let end = ptr;
		while (end < ptr + max && m[end]) end++;
		return utf8.decode(m.subarray(ptr, end));
	}
	resetAsyncData() {
		const dv = new DataView(this.x.memory.buffer);
		dv.setUint32(this.asyncData, this.asyncData + 8, true);
		dv.setUint32(this.asyncData + 4, this.asyncData + this.asyncSize, true);
	}
}

export class Duel {
	constructor(module) {
		this.module = module;
		this.data = new Instance(module, this._imports(null));
		this.inst = this.data;
		this._cards = new Map();
		this._texts = new Map();
		this.running = false;
	}

	// ---------------------------------------------------------------- static data
	get numCards() { return this.data.x.web_num_cards(); }
	get numTexts() { return this.data.x.web_num_texts(); }
	text(id) {
		if (!this._texts.has(id)) this._texts.set(id, this.data.cstr(this.data.x.tcg_text(id)) || '');
		return this._texts.get(id);
	}
	/* text with the current TX_RAM substitutions of the running duel */
	formatText(id) {
		const inst = this.inst, x = inst.x;
		const buf = x.web_text_buffer();
		x.tcg_format_text(id, buf, x.web_text_buffer_size());
		return inst.cstr(buf);
	}
	cardData(id) { return this.data.u8.slice(this.data.x.tcg_card_data(id), this.data.x.tcg_card_data(id) + C.PKMN_CARD_DATA_LENGTH); }
	card(id) {
		if (!this._cards.has(id)) this._cards.set(id, parseCard(id, this.cardData(id), (t) => this.text(t)));
		return this._cards.get(id);
	}
	cards() { const out = []; for (let i = 1; i <= this.numCards; i++) out.push(this.card(i)); return out; }
	/* DeckPointers entry i (*_DECK constants = *_DECK_ID + 2): 60 card ids + the deck name text id */
	deckList(i) {
		const x = this.data.x, m = this.data.u8;
		let p = x.web_deck_list(i);
		if (!p) return null;
		const cards = [];
		while (m[p] && cards.length < 60) {
			const count = m[p], id = m[p + 1];
			for (let k = 0; k < count && cards.length < 60; k++) cards.push(id);
			p += 2;
		}
		while (m[p]) p += 2; // skip the rest of an oversized list
		const nameText = m[p + 1] | (m[p + 2] << 8);
		return { index: i, label: this.data.cstr(x.web_deck_label(i)), cards, nameText };
	}
	get numDecks() { return this.data.x.web_num_decks(); }

	// ---------------------------------------------------------------- live state (current or last duel)
	wram() { return new Uint8Array(this.inst.x.memory.buffer, this.inst.x.tcg_wram(), 0x2000); }
	hram() { return new Uint8Array(this.inst.x.memory.buffer, this.inst.x.tcg_hram(), 0x80); }
	duelvars(duelist) { return new Uint8Array(this.inst.x.memory.buffer, this.inst.x.tcg_duelvars(duelist), 256); }
	/* WRAM byte(s) by label (gen/wram.h), e.g. duel.w('wDuelTurns') */
	w(label, size = 1) {
		const off = W[label] - 0xc000;
		const m = this.wram();
		return size === 1 ? m[off] : m.slice(off, off + size);
	}
	h(label) { return this.hram()[W[label] - 0xff80]; }
	cardId(duelist, deckIndex) { return this.inst.x.tcg_card_id(duelist, deckIndex); }

	// ---------------------------------------------------------------- running a duel
	_imports(run) {
		const self = this;
		return {
			env: {
				js_adv_clock: () => Date.now(),
				js_adv_sound: () => {},
				js_adv_store: () => -1,
				js_adventure: () => 0,
				js_fatal(ptr) {
					const msg = self.inst.cstr(ptr) || 'fatal error';
					throw new TcgFatal(msg);
				},
				js_save(ptr, size) {
					// TcgHost.save: the duel's save file (TCG_SAVE_SIZE bytes), each time the original writes SRAM
					if (run && run.handlers.onSave) run.handlers.onSave(self.inst.u8.slice(ptr, ptr + size));
				},
				js_event(type, a0, a1, a2, a3, text, str) {
					if (!run) return;
					return run.call('event', () => {
						const ev = { type, arg: [a0, a1, a2, a3], text, str: self.inst.cstr(str) };
						if (ev.str === null && text && type === EV.TEXT) ev.str = self.formatText(text);
						return run.handlers.onEvent ? run.handlers.onEvent(ev) : undefined;
					});
				},
				js_request(type, duelist, shown, menu, num, cursor, text, text2, str, items, a0, a1, a2, a3) {
					if (!run) return TCG_CANCEL;
					return run.call('request', () => {
						const m = self.inst.u8;
						const req = {
							type, duelist, shown, menu, numItems: num, cursor, text, text2,
							str: self.inst.cstr(str),
							textStr: text ? self.formatText(text) : '',
							text2Str: text2 ? self.formatText(text2) : '',
							items: items ? m.slice(items, items + ITEMS_COPY) : new Uint8Array(0),
							arg: [a0, a1, a2, a3], seq: run.requests++,
						};
						return run.handlers.onRequest ? run.handlers.onRequest(req) : (type === REQ.ACK ? 0 : TCG_CANCEL);
					}, (v) => (v === undefined || v === null ? TCG_CANCEL : v | 0));
				},
			},
		};
	}

	/**
	 * setup = {
	 *   duelists: [{ kind: 'human'|'ai', deckId, cards: [60 card ids] | null, name }, {...}],
	 *   prizes: 6, seed: 24-bit int (or rng: [rng1, rng2, counter]), music, animationsDisabled,
	 *   receivedLegendaryCards (AI decks play like boss decks), skipDelayAllowed (sSkipDelayAllowed)
	 * }
	 * handlers: onEvent(ev), onRequest(req) (both may return a Promise), onSave(bytes) (TcgHost.save)
	 * Resolves with { result: 0 (PLAYER_TURN side won) | 1, finished: wDuelFinished, turns, requests }.
	 * Rejects with TcgFatal (engine abort, e.g. an unported effect/AI stub) or DuelAborted.
	 */
	run(setup, handlers = {}) {
		return this._start(setup, handlers, (x) => x.web_run());
	}

	/**
	 * "Continue Duel": resumes a duel from a save given to onSave (tcg_continue_duel). setup only
	 * gives the names and settings; decks, duelist types, turn, RNG and music come from the save.
	 * Resolves like run(), with result = TCG_CONTINUE_BROKEN (-1) and broken = true if the save is
	 * not valid (after the engine's "backup is broken" text).
	 */
	continueDuel(setup, save, handlers = {}) {
		return this._start(setup, handlers, (x, inst) => {
			if (!inst.saveLoaded) {
				const n = Math.min(save.length, x.web_save_size());
				inst.u8.set(save.subarray ? save.subarray(0, n) : save.slice(0, n), x.web_save_buffer());
				inst.saveLoaded = true;
				inst.saveSize = save.length;
			}
			return x.web_continue(inst.saveSize);
		}).then((res) => ({ ...res, broken: res.result === TCG_CONTINUE_BROKEN }));
	}

	/* tcg_saved_duel_info: what a save holds, or null if it is not a valid save */
	savedDuelInfo(save) {
		const x = this.data.x, m = this.data.u8;
		const n = Math.min(save.length, x.web_save_size());
		m.set(save.subarray ? save.subarray(0, n) : save.slice(0, n), x.web_save_buffer());
		const out = x.web_text_buffer();
		if (x.web_saved_duel_info(save.length, out) !== 0) return null;
		const b = this.data.u8.slice(out, out + 8);
		return {
			duelType: b[0], duelistType: [b[1], b[2]], opponentDeckId: b[3], turns: b[4], whoseTurn: b[5],
			prizesLeft: [b[6], b[7]],
		};
	}

	async _start(setup, handlers, entry) {
		if (this.running) throw new Error('a duel is already running');
		this.running = true;
		const run = {
			handlers, requests: 0, state: ASYNC_NORMAL, pending: null, kind: null, resume: undefined,
			aborted: null, abortPromise: null,
		};
		run.abortPromise = new Promise((_, reject) => { run.abortReject = reject; });
		run.abortPromise.catch(() => {});
		this._run = run;
		const inst = new Instance(this.module, this._imports(run));
		this.inst = inst;
		const x = inst.x;
		run.call = (kind, fn, convert = (v) => v) => {
			if (run.state === ASYNC_REWINDING) {
				if (kind !== run.kind) throw new Error(`asyncify: rewinding ${run.kind} but ${kind} was called`);
				x.asyncify_stop_rewind();
				run.state = ASYNC_NORMAL;
				const v = run.resume;
				run.resume = undefined;
				return convert(v);
			}
			if (run.aborted) throw run.aborted;
			const v = fn();
			if (!isThenable(v)) return convert(v);
			run.pending = v;
			run.kind = kind;
			inst.resetAsyncData();
			x.asyncify_start_unwind(inst.asyncData);
			run.state = ASYNC_UNWINDING;
			return 0;
		};
		try {
			this._writeSetup(inst, setup || {});
			let result = entry(x, inst);
			while (run.state === ASYNC_UNWINDING) {
				x.asyncify_stop_unwind();
				run.state = ASYNC_NORMAL;
				run.resume = await Promise.race([run.pending, run.abortPromise]);
				run.pending = null;
				if (run.aborted) throw run.aborted;
				x.asyncify_start_rewind(inst.asyncData);
				run.state = ASYNC_REWINDING;
				result = entry(x, inst);
			}
			return { result, finished: this.w('wDuelFinished'), turns: this.w('wDuelTurns'), requests: run.requests };
		} finally {
			this.running = false;
			this._run = null;
		}
	}

	/* abort the running duel: run() rejects with DuelAborted */
	abort(reason) {
		const run = this._run;
		if (!run || run.aborted) return;
		run.aborted = new DuelAborted(reason);
		run.abortReject(run.aborted);
	}

	_writeSetup(inst, setup) {
		const base = inst.x.web_setup_buffer();
		const m = inst.u8;
		m.fill(0, base, base + 256);
		const ds = setup.duelists || [];
		for (let i = 0; i < 2; i++) {
			const d = ds[i] || {};
			const o = base + i * 62;
			m[o] = d.kind === 'ai' || d.kind === TCG_AI ? TCG_AI : TCG_HUMAN;
			m[o + 1] = d.deckId | 0;
			if (d.cards) for (let j = 0; j < 60; j++) m[o + 2 + j] = d.cards[j] | 0;
			const name = utf8enc.encode((d.name || (i ? 'OPPONENT' : 'PLAYER')).slice(0, 24));
			m.set(name.subarray(0, 31), base + 160 + i * 32);
		}
		m[base + 124] = setup.prizes || 6;
		const seed = setup.seed | 0;
		const rng = setup.rng || [seed & 0xff, (seed >> 8) & 0xff, (seed >> 16) & 0xff];
		m[base + 125] = rng[0] | 0; m[base + 126] = rng[1] | 0; m[base + 127] = rng[2] | 0;
		m[base + 128] = setup.music | 0;
		m[base + 129] = setup.animationsDisabled ? 1 : 0;
		m[base + 130] = setup.receivedLegendaryCards ? 1 : 0;
		m[base + 131] = setup.skipDelayAllowed ? 1 : 0;
	}

	/* tcg_state_hash: FNV-1a of the state SaveDuelData saves; online peers compare it to detect desyncs */
	stateHash() {
		return this.inst.x.tcg_state_hash() >>> 0;
	}
}

export const Engine = {
	/** source: URL/path of tcg.wasm (default: next to this module's parent), bytes, or a WebAssembly.Module */
	async load(source) {
		const module = await compileModule(source);
		return new Duel(module);
	},
};
