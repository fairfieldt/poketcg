// save_store.h for the browser (port/frontends/common/save_store.h): small named blobs kept
// between sessions, here in localStorage (base64). Same calls as the C versions:
//
//   saveStoreOpen(where)             where = key prefix (default 'tcg.save.'); false if storage is unusable
//   saveStoreRead(name)              Uint8Array, or null if there is no such file
//   saveStoreWrite(name, data)       replaces name (the old contents stay if the write fails)
//   saveStoreRemove(name)
//
// Plus export/import of a file to/from the user's disk: the File System Access API
// (showSaveFilePicker / showOpenFilePicker) where the browser has it, else a download link /
// <input type=file>. The exported duel save is the raw TcgHost.save image, the same bytes as the
// desktop store's <dir>/duel.sav.

export const SAVE_STORE_DUEL = 'duel';

let prefix = 'tcg.save.';
let storage = null;

export function saveStoreOpen(where) {
	if (where) prefix = where;
	try {
		storage = globalThis.localStorage;
		const k = prefix + '__probe';
		storage.setItem(k, '1');
		storage.removeItem(k);
		return true;
	} catch {
		storage = null;
		return false;
	}
}

function toBase64(bytes) {
	let s = '';
	for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
	return btoa(s);
}

function fromBase64(str) {
	const s = atob(str);
	const out = new Uint8Array(s.length);
	for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
	return out;
}

export function saveStoreRead(name) {
	if (!storage && !saveStoreOpen()) return null;
	try {
		const v = storage.getItem(prefix + name);
		return v === null ? null : fromBase64(v);
	} catch {
		return null;
	}
}

export function saveStoreWrite(name, data) {
	if (!storage && !saveStoreOpen()) return false;
	try {
		storage.setItem(prefix + name, toBase64(data instanceof Uint8Array ? data : new Uint8Array(data)));
		return true;
	} catch {
		return false; // quota: setItem leaves the old value
	}
}

export function saveStoreRemove(name) {
	if (!storage && !saveStoreOpen()) return false;
	try {
		storage.removeItem(prefix + name);
		return true;
	} catch {
		return false;
	}
}

// JSON side data (e.g. who played the saved duel), stored as UTF-8 blobs in the same store
export function saveStoreReadJSON(name) {
	const b = saveStoreRead(name);
	if (!b) return null;
	try { return JSON.parse(new TextDecoder().decode(b)); } catch { return null; }
}

export function saveStoreWriteJSON(name, value) {
	return saveStoreWrite(name, new TextEncoder().encode(JSON.stringify(value)));
}

// ---------------------------------------------------------------- files on the user's disk

export function hasFileSystemAccess() {
	return typeof globalThis.showSaveFilePicker === 'function' && typeof globalThis.showOpenFilePicker === 'function';
}

const PICKER_TYPES = [{ description: 'Pokémon TCG save file', accept: { 'application/octet-stream': ['.sav'] } }];

/* writes data to a file the user picks (or downloads it). resolves false if the user cancelled */
export async function exportFile(data, fileName = 'duel.sav') {
	const extension=fileName.split('.').pop().toLowerCase();
	const mime=extension==='json'?'application/json':extension==='txt'?'text/plain':'application/octet-stream';
	const types=extension==='json'?[{description:'Pokémon TCG cartridge',accept:{'application/json':['.json']}}]:extension==='txt'?[{description:'Text report',accept:{'text/plain':['.txt']}}]:PICKER_TYPES;
	if (hasFileSystemAccess()) {
		let handle;
		try {
			handle = await globalThis.showSaveFilePicker({ suggestedName: fileName, types });
		} catch (e) {
			if (e && e.name === 'AbortError') return false;
			handle = null; // not allowed here (e.g. no user gesture): fall back to a download
		}
		if (handle) {
			const w = await handle.createWritable();
			await w.write(data);
			await w.close();
			return true;
		}
	}
	const url = URL.createObjectURL(new Blob([data], { type: mime }));
	const a = document.createElement('a');
	a.href = url;
	a.download = fileName;
	document.body.appendChild(a);
	a.click();
	a.remove();
	setTimeout(() => URL.revokeObjectURL(url), 10000);
	return true;
}

/* reads a file the user picks: Uint8Array, or null if cancelled */
export async function importFile(options = {}) {
	const types=options.types||PICKER_TYPES;
	if (hasFileSystemAccess()) {
		try {
			const [handle] = await globalThis.showOpenFilePicker({ types, multiple: false });
			const file = await handle.getFile();
			return new Uint8Array(await file.arrayBuffer());
		} catch (e) {
			if (e && e.name === 'AbortError') return null;
			// fall through to <input type=file>
		}
	}
	return new Promise((resolve) => {
		const input = document.createElement('input');
		input.type = 'file';
		input.accept = options.accept||'.sav,application/octet-stream';
		input.style.display = 'none';
		input.addEventListener('change', async () => {
			const f = input.files && input.files[0];
			input.remove();
			resolve(f ? new Uint8Array(await f.arrayBuffer()) : null);
		});
		input.addEventListener('cancel', () => { input.remove(); resolve(null); });
		document.body.appendChild(input);
		input.click();
	});
}
