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
const validId = (id) => Number.isSafeInteger(id) && id > 0;
const diaryId = (name) => {
	const match = /^adventure([1-9][0-9]*)$/.exec(name);
	const id = match ? Number(match[1]) : 0;
	return validId(id) ? id : 0;
};
const ownerBytes = (id) => utf8.encode(String(id));
const ownerId = (bytes) => {
	if (!bytes) return 0;
	if (bytes.length === 1 && (bytes[0] === 1 || bytes[0] === 2)) return bytes[0];
	const text = new TextDecoder().decode(bytes);
	const id = /^[1-9][0-9]*$/.test(text) ? Number(text) : 0;
	return validId(id) ? id : 0;
};
function migrateLegacy(files) {
	if (!files.duelowner) return files;
	const id = ownerId(unb64(files.duelowner));
	if (id !== 1 && id !== 2) return files;
	const duel = `duel-${id}`, owner = `duelowner-${id}`;
	if ((files.duel && files[duel] && files[duel] !== files.duel) ||
		(files[owner] && ownerId(unb64(files[owner])) !== id))
		throw new Error('Conflicting saved duels.');
	const next = { ...files, [owner]: files[owner] || files.duelowner };
	if (files.duel) next[duel] = files.duel;
	delete next.duel; delete next.duelowner;
	return next;
}
export class Cartridge {
	constructor(storage = globalThis.localStorage) {
		this.storage = storage;
		this.files = {};
		try {
			const saved = JSON.parse(storage?.getItem(KEY) || 'null');
			if (saved?.version === 1 && saved.files && !Array.isArray(saved.files)) this.files = saved.files;
		} catch {}
		this.migrateLegacyDuels();
	}
	read(name) {
		try { return this.files[name] ? unb64(this.files[name]) : null; }
		catch { return null; }
	}
	_replace(next) {
		try {
			this.storage?.setItem(KEY, JSON.stringify({ version: 1, files: next }));
			this.files = next;
			return true;
		} catch { return false; }
	}
	migrateLegacyDuels() {
		try {
			const next = migrateLegacy(this.files);
			return next === this.files || this._replace(next);
		} catch { return false; }
	}
	commit(changes) {
		const next = { ...this.files };
		for (const [name, data] of Object.entries(changes)) {
			if (data == null) delete next[name];
			else next[name] = b64(data);
		}
		return this._replace(next);
	}
	export() { return utf8.encode(JSON.stringify({ version: 1, files: this.files })); }
	import(bytes) {
		try {
			const data = JSON.parse(new TextDecoder().decode(bytes));
			if (data.version !== 1 || !data.files || typeof data.files !== 'object' || Array.isArray(data.files)) return false;
			const next = {};
			for (const [key, value] of Object.entries(data.files)) {
				if (!/^[a-z0-9-]+$/.test(key) || typeof value !== 'string') return false;
				const decoded = unb64(value);
				if (decoded.length > 16384) return false;
				next[key] = b64(decoded);
			}
			return this._replace(migrateLegacy(next));
		} catch { return false; }
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
		this.profileId = 1;
		this.peerId = null;
		this.readProfileId = null;
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
		const logical = this.inst.cstr(name, 64);
		const key = this.storageKey(logical);
		if (!key) return kind === 0 ? -1 : 0;
		if (kind === 0) {
			let saved = this.cartridge.read(key);
			if (logical === 'duelowner') saved = ownerId(saved) === this.profileId ? new Uint8Array([1]) : null;
			if (!saved) return -1;
			const n = Math.min(size, saved.length);
			this.inst.u8.set(saved.subarray(0, n), data);
			return n;
		}
		if (kind === 1) return +this.cartridge.commit({ [key]: logical === 'duelowner' ? ownerBytes(this.profileId) : this.inst.u8.slice(data, data + size) });
		if (kind === 2) return +this.cartridge.commit({ [key]: null });
		if (kind === 3) {
			const peer = this.storageKey(this.inst.cstr(second, 64));
			if (!peer || peer === key) return 0;
			return +this.cartridge.commit({
				[key]: this.inst.u8.slice(data, data + size),
				[peer]: this.inst.u8.slice(other, other + otherSize),
			});
		}
		return -1;
	}
	storageKey(key) {
		if (key === 'adventure1') return `adventure${this.readProfileId || this.profileId}`;
		if (key === 'adventure2') return this.peerId ? `adventure${this.peerId}` : null;
		if (key === 'duel' || key === 'duelowner') return `${key}-${this.profileId}`;
		return key;
	}
	async choosePeer() {
		const diaries = this.diaries().filter((diary) => diary.id !== this.profileId);
		const id = this.handlers.peer ? await this.handlers.peer({ activeId: this.profileId, diaries }) :
			(diaries.some((diary) => diary.id === this.peerId) ? this.peerId : diaries[0]?.id);
		return this.setPeer(id) ? 1 : 0;
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
		if (kind === 8) value = this.choosePeer();
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
		if (this.busy) return false;
		const candidate = new Cartridge(null);
		if (!candidate.import(bytes)) return false;
		const validator = new AdventureEngine(this.module, {}, candidate);
		for (const key of Object.keys(candidate.files)) {
			if (key.startsWith('adventure') && (!diaryId(key) || !validator.profile(diaryId(key)))) return false;
			if ((key === 'duel' || key === 'duelowner')) return false;
			if (key.startsWith('duel-') || key.startsWith('duelowner-')) {
				const match = /^(?:duel|duelowner)-([1-9][0-9]*)$/.exec(key);
				const id = match ? Number(match[1]) : 0;
				if (!validId(id) || !validator.profile(id)) return false;
				const duel = candidate.read(`duel-${id}`), owner = candidate.read(`duelowner-${id}`);
				if (ownerId(owner) !== id) return false;
				// The source writes ownership before the duel's first save menu.
				if (!duel && key.startsWith('duelowner-')) continue;
				if (!duel || duel.length !== validator.x.web_save_size()) return false;
				validator.inst.u8.set(duel, validator.x.web_save_buffer());
				if (validator.x.web_saved_duel_info(duel.length, validator.x.web_text_buffer()) !== 0) return false;
			}
		}
		const changes = Object.fromEntries(Object.keys(this.cartridge.files).map((key) => [key, null]));
		for (const name of Object.keys(candidate.files)) changes[name] = candidate.read(name);
		return this.cartridge.commit(changes);
	}
	installDiary(id, bytes) {
		if (!validId(id) || this.busy || !(bytes instanceof Uint8Array)) return false;
		const key = `adventure${id}`;
		if (Object.hasOwn(this.cartridge.files, key)) return false;
		const candidate = new Cartridge(null);
		if (!candidate.commit({ [key]: bytes })) return false;
		const validator = new AdventureEngine(this.module, {}, candidate);
		if (!validator.profile(id)) return false;
		return this.cartridge.commit({ [key]: bytes });
	}
	diaries() {
		return Object.keys(this.cartridge.files).map(diaryId).filter((id) => id && this.profile(id))
			.sort((a, b) => a - b).map((id) => {
				const data = this.cartridge.read(`adventure${id}`);
				const name = new TextDecoder().decode(data.subarray(8, 24)).replace(/\0.*$/, '');
				return { id, name };
			});
	}
	nextDiaryId() {
		const used = new Set(Object.keys(this.cartridge.files).map(diaryId));
		let id = 1; while (used.has(id)) id++;
		return id;
	}
	profile(id) {
		if (!validId(id)) return false;
		const previous = this.readProfileId;
		try { this.readProfileId = id; return !!this.x.web_adv_profile(1); }
		finally { this.readProfileId = previous; }
	}
	setPeer(id) {
		if (!validId(id) || id === this.profileId || !this.profile(id)) return false;
		this.peerId = id;
		return true;
	}
	saveDuel(bytes) {
		if (!(bytes instanceof Uint8Array) || bytes.length !== this.x.web_save_size()) return false;
		return this.cartridge.commit({ [`duel-${this.profileId}`]: bytes,
			[`duelowner-${this.profileId}`]: ownerBytes(this.profileId) });
	}
	loadProfile(id) {
		if (!validId(id)) return Promise.resolve(-1);
		if (this.busy) return Promise.reject(new Error('Finish the current action first.'));
		this.profileId = id;
		return this.call('web_adv_load_only', 1);
	}
	start(id, { fresh = false, name = 'MARK', seed = (Math.random() * 0x1000000) | 0 } = {}) {
		if (!validId(id)) return Promise.resolve(-1);
		if (this.busy) return Promise.reject(new Error('Finish the current action first.'));
		if (!this.cartridge.migrateLegacyDuels()) return Promise.resolve(-3);
		this.profileId = id;
		const p = this.x.web_adv_io();
		this.inst.u8.fill(0, p, p + 16);
		this.inst.u8.set(utf8.encode(name.slice(0, 6)), p);
		return this.call('web_adv_start', 1, +fresh, seed);
	}
	action(action, arg = 0) {
		return this.call('web_adv_action', action, arg);
	}
	stopMovement() {
		return this.action(12);
	}
	setDuelRng(values) {
		this.x.web_adv_rng(...values);
	}
}
