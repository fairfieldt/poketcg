// Browser integration: optional art, exact validation, persistence and storage failure.
import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { sha1, validateRom, decodeCard } from '../src/js/rom_art.js';
const web = fileURLToPath(new URL('../', import.meta.url));
const root = path.resolve(web, '../../..');
const dist = path.resolve(process.env.TCG_TEST_DIST || path.join(web, 'dist-multiplayer'));
const rom = await readFile(path.join(root, 'poketcg.gbc'));
for (const bytes of [new Uint8Array(), new TextEncoder().encode('abc'), rom])
	assert.equal(sha1(bytes), createHash('sha1').update(bytes).digest('hex'));
validateRom(rom);
const bad = Buffer.from(rom); bad[100000] ^= 1;
assert.throws(() => validateRom(bad));
assert.throws(() => validateRom(rom.subarray(0, 100)));
const { CARD_OFFSETS } = await import(path.join(dist, 'js/gen/rom-art.js'));
const vc = await readFile(path.join(root, 'poketcg_vc.gbc')); validateRom(vc);
for (const offset of Object.values(CARD_OFFSETS)) assert.equal(decodeCard(vc, offset).length, 64 * 48 * 4);
async function files(dir) {
	const out = [];
	for (const entry of await readdir(dir, { withFileTypes: true })) {
		const file = path.join(dir, entry.name);
		if (entry.isDirectory()) out.push(...await files(file)); else out.push(file);
	}
	return out;
}
assert(!(await files(dist)).some((f) => /\.(png|ogg|gbc|2bpp|pal)$/.test(f)));
const server = http.createServer(async (req, res) => {
	try {
		const name = new URL(req.url, 'http://local').pathname;
		const file = path.join(dist, name === '/' ? 'index.html' : name);
		const data = await readFile(file);
		res.setHeader('Content-Type', ({ '.js': 'application/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.html': 'text/html', '.json': 'application/json' })[path.extname(file)] || 'application/octet-stream');
		res.end(data);
	} catch { res.writeHead(404).end(); }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const require = createRequire(import.meta.url);
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const browser = await chromium.launch({ channel: 'chrome', headless: true });
const page = await browser.newPage({ viewport: { width: 1280, height: 960 } });
const errors = [], images = [];
page.on('pageerror', (e) => errors.push(e.message));
page.on('request', (r) => { if (/\.(png|ogg|gbc)(\?|$)/.test(r.url())) images.push(r.url()); });
try {
	await page.goto(`http://127.0.0.1:${server.address().port}/`);
	await page.locator('.rom-input').waitFor();
	assert.equal(await page.locator('.title-arts .art-placeholder').count(), 5);
	assert((await page.locator('.title-arts').innerText()).includes('Charizard'));
	assert.equal(await page.locator('.title-arts .art').first().evaluate((n) => getComputedStyle(n).backgroundImage), 'none');
	await page.screenshot({ path: path.join(web, 'build/rom-art-placeholders.png') });
	const input = page.locator('.rom-input');
	await input.setInputFiles({ name: 'bad.gbc', mimeType: 'application/octet-stream', buffer: bad });
	await page.getByRole('status').filter({ hasText: 'unmodified' }).waitFor();
	assert.equal(await page.evaluate(() => localStorage.getItem('tcg.artRom.v1')), null);
	await input.setInputFiles(path.join(root, 'poketcg.gbc'));
	await page.getByRole('button', { name: 'Remove saved ROM' }).waitFor();
	assert.equal(await page.locator('.art-placeholder').count(), 0);
	await page.screenshot({ path: path.join(web, 'build/rom-art-loaded.png') });
	// Compare every decoded card pixel with the established generated sprite sheet.
	const expected = await readFile(path.join(web, 'dist/gfx/cards.png'));
	assert(await page.evaluate(async (base64) => {
		const { artSheet } = await import('/js/rom_art.js');
		const load = (src) => new Promise((resolve) => { const img = new Image(); img.onload = () => resolve(img); img.src = src; });
		const [actual, expected] = await Promise.all([load(artSheet()), load('data:image/png;base64,' + base64)]);
		const pixels = (img) => { const c = document.createElement('canvas'); c.width = img.width; c.height = img.height; const ctx = c.getContext('2d'); ctx.drawImage(img, 0, 0); return ctx.getImageData(0, 0, c.width, c.height).data; };
		const a = pixels(actual), b = pixels(expected); return a.length === b.length && a.every((v, i) => v === b[i]);
	}, expected.toString('base64')));
	await page.reload(); await page.getByRole('button', { name: 'Remove saved ROM' }).waitFor();
	assert.equal(await page.locator('.art-placeholder').count(), 0);
	await page.locator('.rom-input').setInputFiles({ name: 'bad.gbc', mimeType: 'application/octet-stream', buffer: bad });
	await page.getByRole('status').filter({ hasText: 'unmodified' }).waitFor();
	assert.equal(await page.locator('.art-placeholder').count(), 0);
	await page.getByRole('button', { name: 'Remove saved ROM' }).click();
	assert.equal(await page.locator('.title-arts .art-placeholder').count(), 5);
	await page.evaluate(() => { Storage.prototype.setItem = () => { throw new DOMException('Quota', 'QuotaExceededError'); }; });
	await page.locator('.rom-input').setInputFiles(path.join(root, 'poketcg.gbc'));
	await page.getByRole('status').filter({ hasText: 'Free browser storage' }).waitFor();
	assert.equal(await page.locator('.title-arts .art-placeholder').count(), 5);
	await page.reload(); await page.locator('.rom-input').waitFor();
	await page.getByRole('button', { name: 'Online duel', exact: true }).click();
	await page.getByRole('button', { name: /Charmander & Friends/ }).first().click();
	assert(await page.getByRole('checkbox', { name: /Server runs/ }).isChecked());
	await page.screenshot({ path: path.join(web, 'build/rom-art-lobby.png') });
	await page.goto(`http://127.0.0.1:${server.address().port}/?fast`);
	await page.getByRole('button', { name: 'Duel vs. Computer', exact: true }).click();
	await page.getByRole('button', { name: /Charmander & Friends/ }).first().click();
	await page.getByRole('button', { name: /Charmander & Friends/ }).first().click();
	await page.locator('.duel').waitFor();
	await page.waitForFunction(() => window.tcgApp.ui?.controls.length > 0);
	// Exercise shapes that appear later in a duel, too.
	await page.evaluate(() => {
		const ui = window.tcgApp.ui;
		ui.root.appendChild(ui.coinView());
		const back = document.createElement('div'); back.className = 'pile-back'; ui.root.appendChild(back);
	});
	await page.screenshot({ path: path.join(web, 'build/rom-art-duel.png') });
	assert.deepEqual(errors, []); assert.deepEqual(images, []);
	console.log('PASS: placeholder build, exact ROM validation, all card pixels, reload, removal, invalid replacement, storage failure, authoritative default, no art requests');
} finally { await browser.close(); await new Promise((resolve) => server.close(resolve)); }
