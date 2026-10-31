// The persistent source story/menu instance; duels run in an independent
// instance of the same module so their synchronous engine can await its UI.
import { Instance, compileModule, TcgFatal } from './engine.js';
const utf8 = new TextEncoder();
const KEY = 'poketcg.adventure.cartridge.v1';
const b64 = (bytes) => {
	let s = '';
	for (const b of bytes) s += String.fromCharCode(b);
	return btoa(s);
};
const unb64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
export class Cartridge {
	constructor(storage = globalThis.localStorage) {
		this.storage = storage;
		this.files = {};
		try {
			const saved = JSON.parse(storage?.getItem(KEY) || 'null');
			if (saved?.version === 1 && saved.files) this.files = saved.files;
		} catch {}
	}
	read(name) {
		try {
			return this.files[name] ? unb64(this.files[name]) : null;
		} catch {
			return null;
		}
	}
	commit(changes) {
		const next = { ...this.files };
		for (const [name, data] of Object.entries(changes)) {
			if (data == null) delete next[name];
			else next[name] = b64(data);
		}
		const encoded = JSON.stringify({ version: 1, files: next });
		try {
			this.storage?.setItem(KEY, encoded);
			this.files = next;
			return true;
		} catch {
			return false;
		}
	}
	export() {
		return new TextEncoder().encode(JSON.stringify({ version: 1, files: this.files }));
	}
	import(bytes) {
		try {
			if (bytes.length > 262144) return false;
			const data = JSON.parse(new TextDecoder().decode(bytes));
			if (data.version !== 1 || !data.files || typeof data.files !== 'object') return false;
			for (const [k, v] of Object.entries(data.files)) {
				if (!/^[a-z0-9-]+$/.test(k) || typeof v !== 'string' || unb64(v).length > 16384) return false;
			}
			const old = this.files;
			this.files = {};
			if (this.commit(Object.fromEntries(Object.entries(data.files).map(([k, v]) => [k, unb64(v)]))))
				return true;
			this.files = old;
			return false;
		} catch {
			return false;
		}
	}
}
export class AdventureEngine {
	static async load(source, handlers = {}, cartridge = new Cartridge()) {
		const module = await compileModule(source);
		return new AdventureEngine(module, handlers, cartridge);
	}
	constructor(module, handlers, cartridge) {
		this.module = module;
		this.handlers = handlers;
		this.cartridge = cartridge;
		this.busy = false;
		this.pending = null;
		this.inst = new Instance(module, {
			env: {
				js_fatal: (p) => {
					throw new TcgFatal(this.inst.cstr(p));
				},
				js_event: () => 0,
				js_request: () => -1,
				js_save: () => {},
				js_adv_clock: () => Date.now(),
				js_adv_sound: (kind, id) => this.handlers.sound?.(kind, id),
				js_adv_store: (kind, name, data, size, second, other, otherSize) =>
					this.store(kind, name, data, size, second, other, otherSize),
				js_adventure: (kind, ...args) => this.event(kind, args),
			},
		});
	}
	get x() {
		return this.inst.x;
	}
	get view() {
		return JSON.parse(this.inst.cstr(this.x.web_adv_view(), 12288));
	}
	get name() {
		return this.inst.cstr(this.x.web_adv_name(), 16);
	}
	store(kind, name, data, size, second, other, otherSize) {
		const key = this.inst.cstr(name, 64);
		if (kind === 0) {
			const saved = this.cartridge.read(key);
			if (!saved) return -1;
			const n = Math.min(size, saved.length);
			this.inst.u8.set(saved.subarray(0, n), data);
			return n;
		}
		if (kind === 1) return +this.cartridge.commit({ [key]: this.inst.u8.slice(data, data + size) });
		if (kind === 2) return +this.cartridge.commit({ [key]: null });
		if (kind === 3)
			return +this.cartridge.commit({
				[key]: this.inst.u8.slice(data, data + size),
				[this.inst.cstr(second, 64)]: this.inst.u8.slice(other, other + otherSize),
			});
		return -1;
	}
	event(kind, args) {
		const run = this.pending;
		if (!run) return 0;
		if (run.state === 2) {
			this.x.asyncify_stop_rewind();
			run.state = 0;
			return run.value | 0;
		}
		const m = this.inst.u8,
			dv = new DataView(m.buffer),
			str = (p) => this.inst.cstr(p, 16384) || '';
		let value;
		if (kind === 1) value = this.handlers.frame?.(this.view);
		if (kind === 2) value = this.handlers.message?.(str(args[0]), str(args[1]));
		if (kind === 3) {
			const items = [];
			for (let i = 0; i < args[2]; i++) items.push(str(dv.getUint32(args[1] + i * 4, true)));
			value = this.handlers.menu?.(str(args[0]), items, args[3], !!args[4]);
		}
		if (kind === 4) value = this.handlers.card?.(args[0]);
		if (kind === 5) {
			const buf = m.slice(args[0], args[0] + 256),
				duelists = [0, 1].map((i) => ({
					kind: buf[i * 62] ? 'ai' : 'human',
					deckId: buf[i * 62 + 1],
					cards: Array.from(buf.slice(i * 62 + 2, i * 62 + 62)),
					name: str(args[0] + 160 + i * 32),
				}));
			const setup = {
				duelists,
				prizes: buf[124],
				rng: Array.from(buf.slice(125, 128)),
				music: buf[128],
				animationsDisabled: !!buf[129],
				receivedLegendaryCards: !!buf[130],
				skipDelayAllowed: !!buf[131],
			};
			value = this.handlers.duel?.(setup, args[1] ? m.slice(args[1], args[1] + args[2]) : null);
		}
		if (kind === 6) value = this.handlers.report?.(str(args[0]), str(args[1]));
		if (kind === 7) value = this.handlers.audioWait?.();
		if (value && typeof value.then === 'function') {
			run.promise = value;
			this.inst.resetAsyncData();
			this.x.asyncify_start_unwind(this.inst.asyncData);
			run.state = 1;
			return 0;
		}
		return value == null ? 0 : value | 0;
	}
	async call(fn, ...args) {
		if (this.busy) throw new Error('Finish the current action first.');
		this.busy = true;
		const run = { state: 0, promise: null, value: 0 };
		this.pending = run;
		try {
			let result = this.x[fn](...args);
			while (run.state === 1) {
				this.x.asyncify_stop_unwind();
				run.state = 0;
				run.value = await run.promise;
				this.x.asyncify_start_rewind(this.inst.asyncData);
				run.state = 2;
				result = this.x[fn](...args);
			}
			return result;
		} finally {
			this.pending = null;
			this.busy = false;
		}
	}
	importCartridge(bytes) {
		const candidate = new Cartridge(null);
		if (!candidate.import(bytes)) return false;
		const validator = new AdventureEngine(this.module, {}, candidate);
		for (const profile of [1, 2])
			if (candidate.read(`adventure${profile}`) && !validator.profile(profile)) return false;
		const duel = candidate.read('duel');
		if (duel) {
			const x = validator.x;
			if (duel.length !== x.web_save_size()) return false;
			validator.inst.u8.set(duel, x.web_save_buffer());
			if (x.web_saved_duel_info(duel.length, x.web_text_buffer()) !== 0) return false;
			const owner = candidate.read('duelowner')?.[0];
			if ((owner !== 1 && owner !== 2) || !validator.profile(owner)) return false;
		}
		const changes = Object.fromEntries(Object.keys(this.cartridge.files).map((k) => [k, null]));
		for (const name of Object.keys(candidate.files)) changes[name] = candidate.read(name);
		return this.cartridge.commit(changes);
	}
	profile(number) {
		return !!this.x.web_adv_profile(number);
	}
	start(number, { fresh = false, name = 'MARK', seed = (Math.random() * 0x1000000) | 0 } = {}) {
		const p = this.x.web_adv_io();
		this.inst.u8.fill(0, p, p + 16);
		this.inst.u8.set(utf8.encode(name.slice(0, 6)), p);
		return this.call('web_adv_start', number, +fresh, seed);
	}
	action(action, arg = 0) {
		return this.call('web_adv_action', action, arg);
	}
	setDuelRng(values) {
		this.x.web_adv_rng(...values);
	}
}
