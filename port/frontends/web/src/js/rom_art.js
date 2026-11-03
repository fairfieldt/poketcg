// Optional card art is decoded only from a validated, browser-local ROM.
const KEY = 'tcg.artRom.v1';
const HASHES = new Set(['0f8670a583255cff3e5b7ca71b5d7454d928fc48', '43c86f1abc865f9d021cdb9f106300eac04a2b93']);
let sheet = '';
let cardName = (id) => `Card ${id}`;
export function artSheet() { return sheet; }
export function artName(id) { return cardName(id); }

// SHA-1 identifies the exact supported dump, including on HTTP origins without SubtleCrypto.
export function sha1(bytes) {
	const data = new Uint8Array(Math.ceil((bytes.length + 9) / 64) * 64);
	data.set(bytes); data[bytes.length] = 0x80;
	new DataView(data.buffer).setUint32(data.length - 4, bytes.length * 8);
	let h = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0];
	const w = new Int32Array(80), view = new DataView(data.buffer);
	const rol = (v, n) => (v << n) | (v >>> (32 - n));
	for (let offset = 0; offset < data.length; offset += 64) {
		for (let i = 0; i < 80; i++) w[i] = i < 16 ? view.getInt32(offset + i * 4) : rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
		let [a, b, c, d, e] = h;
		for (let i = 0; i < 80; i++) {
			const f = i < 20 ? (b & c) | (~b & d) : i < 40 ? b ^ c ^ d : i < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
			const k = i < 20 ? 0x5a827999 : i < 40 ? 0x6ed9eba1 : i < 60 ? 0x8f1bbcdc : 0xca62c1d6;
			const t = (rol(a, 5) + f + e + k + w[i]) | 0;
			e = d; d = c; c = rol(b, 30); b = a; a = t;
		}
		h = h.map((v, i) => (v + [a, b, c, d, e][i]) | 0);
	}
	return h.map((v) => (v >>> 0).toString(16).padStart(8, '0')).join('');
}
export function validateRom(bytes) {
	if (bytes.length !== 1048576 || !HASHES.has(sha1(bytes)))
		throw new Error('Choose an unmodified Pokémon Trading Card Game (USA, Europe) ROM (.gbc), original or Virtual Console.');
}
export function decodeCard(bytes, offset) {
	const rgba = new Uint8ClampedArray(64 * 48 * 4);
	for (let y = 0; y < 48; y++) for (let x = 0; x < 64; x++) {
		const tile = ((x >> 3) * 6 + (y >> 3)) * 16 + (y & 7) * 2 + offset;
		const bit = 7 - (x & 7), shade = ((bytes[tile] >> bit) & 1) | (((bytes[tile + 1] >> bit) & 1) << 1);
		const p = offset + 768 + shade * 2, color = bytes[p] | bytes[p + 1] << 8;
		const dest = (y * 64 + x) * 4;
		for (let c = 0; c < 3; c++) { const v = (color >> (c * 5)) & 31; rgba[dest + c] = (v << 3) | (v >> 2); }
		rgba[dest + 3] = 255;
	}
	return rgba;
}
async function renderSheet(bytes) {
	const { CARD_OFFSETS } = await import('./gen/rom-art.js');
	const canvas = document.createElement('canvas');
	canvas.width = 1024; canvas.height = Math.ceil((Math.max(...Object.keys(CARD_OFFSETS).map(Number)) + 1) / 16) * 48;
	const ctx = canvas.getContext('2d');
	for (const [id, offset] of Object.entries(CARD_OFFSETS)) ctx.putImageData(new ImageData(decodeCard(bytes, offset), 64, 48), (+id % 16) * 64, Math.floor(+id / 16) * 48);
	return canvas.toDataURL('image/png');
}
function encode(bytes) {
	let str = '';
	for (let i = 0; i < bytes.length; i += 8192) str += String.fromCharCode(...bytes.subarray(i, i + 8192));
	return btoa(str);
}
export async function loadRomArt(file) {
	if (file.size !== 1048576) throw new Error('Choose a 1 MiB Pokémon Trading Card Game ROM (.gbc).');
	const bytes = new Uint8Array(await file.arrayBuffer());
	validateRom(bytes);
	const next = await renderSheet(bytes);
	try { localStorage.setItem(KEY, encode(bytes)); }
	catch { throw new Error('Could not save the ROM. Free browser storage and try again.'); }
	sheet = next;
}
export async function initRomArt(name) {
	cardName = name;
	document.documentElement.classList.add('local-art');
	try {
		const saved = localStorage.getItem(KEY);
		if (saved) {
			if (saved.length !== 1398104) throw new Error('Invalid stored ROM');
			const bytes = Uint8Array.from(atob(saved), (ch) => ch.charCodeAt(0));
			validateRom(bytes); sheet = await renderSheet(bytes);
		}
		return '';
	} catch { return 'Saved ROM could not be loaded. Choose your ROM again.'; }
}
export function removeRomArt() {
	try { localStorage.removeItem(KEY); } catch { throw new Error('Could not remove the saved ROM.'); }
	sheet = '';
}
