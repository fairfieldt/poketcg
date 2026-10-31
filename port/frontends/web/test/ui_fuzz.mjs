// Random-input browser test: plays hot-seat duels by clicking random interactive elements of
// whatever the UI shows for the current request (lists, play area rows, menu items, prizes,
// Back/Cancel buttons) or pressing random keys, and checks that every request gets answered
// (the engine keeps asking new requests), and that there are no console errors.
//
//   node test/ui_fuzz.mjs [--port 8131] [--steps 1500] [--seed 1] [--duels 3]
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : d; };
const PORT = Number(opt('--port', 8131));
const STEPS = Number(opt('--steps', 1500));
const DUELS = Number(opt('--duels', 3));
let seed = Number(opt('--seed', 1));
const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };

const proc = spawn(process.execPath, [path.join(here, '..', 'server', 'server.js'), '--port', String(PORT)], { env: { ...process.env, QUIET: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
await new Promise((r) => proc.stdout.once('data', r));
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
const problems = [];
page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') problems.push(`console ${m.type()}: ${m.text()}`); });
page.on('pageerror', (e) => problems.push(`page error: ${e.message}`));
page.on('dialog', (d) => d.accept());
await page.goto(`http://localhost:${PORT}/duel.html?fast=1`);

const TARGETS = [
	'.overlay .cl-item', '.overlay .pa-row:not(.disabled)', '.textbox .tm-item', '.prize-card:not(.taken)',
	'.overlay .cl-buttons .btn', '.check-menu .tm-item', '.overlay .btn.small', '.duel .bench-slot', '.duel .active',
];
const KEYS = ['z', 'z', 'z', 'x', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'c'];
const seen = new Map();
let failed = false;
for (let d = 0; d < DUELS; d++) {
	await page.click('text=2 Players (same screen)');
	const tiles = await page.$$('.deck-tile:not([disabled])');
	await tiles[rnd(tiles.length)].click();
	const tiles2 = await page.$$('.deck-tile:not([disabled])');
	await tiles2[rnd(tiles2.length)].click();
	await page.waitForSelector('.duel .field');
	let lastReq = -1, stall = 0, ended = '';
	for (let step = 0; step < STEPS; step++) {
		const s = await page.evaluate(() => {
			const app = window.tcgApp;
			const dlg = document.querySelector('.duel .modal:not(.hidden) .dialog');
			const r = app.duel._run;
			return { dialog: dlg ? dlg.textContent : '', requests: r ? r.requests : -1, type: app.ui && app.ui.req ? app.ui.req.type : 0, modal: !!document.querySelector('.duel .modal:not(.hidden) .card-page') };
		});
		if (s.dialog) { ended = s.dialog.replace(/\s+/g, ' ').slice(0, 80); break; }
		seen.set(s.type, (seen.get(s.type) || 0) + 1);
		if (s.requests === lastReq) stall++; else { stall = 0; lastReq = s.requests; }
		if (stall > 300) { failed = true; console.log(`  FAIL: duel ${d + 1}: no new request for 300 actions (request type ${s.type})`); break; }
		if (s.modal) { await page.keyboard.press('x'); continue; }
		// bias towards progress: at the duel menu, often attack or end the turn
		if (s.type === 2 && rnd(2) === 0) { await page.click(`.duel-menu .tm-item:has-text("${rnd(2) ? 'Attack' : 'Done'}")`).catch(() => {}); continue; }
		if (rnd(3) === 0) { await page.keyboard.press(KEYS[rnd(KEYS.length)]); continue; }
		const sel = TARGETS[rnd(TARGETS.length)];
		// centers of the visible elements matching sel (one round trip)
		const pts = await page.evaluate((q) => [...document.querySelectorAll(q)].map((e) => e.getBoundingClientRect())
			.filter((r) => r.width > 0 && r.height > 0 && r.bottom > 0 && r.top < innerHeight).map((r) => [r.x + r.width / 2, r.y + r.height / 2]), sel);
		if (!pts.length) { await page.keyboard.press(rnd(4) ? 'z' : 'x'); continue; }
		const [x, y] = pts[rnd(pts.length)];
		await page.mouse.click(x, y);
	}
	const reqs = await page.evaluate(() => (window.tcgApp.duel._run ? window.tcgApp.duel._run.requests : 'done'));
	console.log(`duel ${d + 1}: ${ended || 'still running'} (requests answered: ${reqs})`);
	if (!ended) await page.evaluate(() => window.tcgApp.duel.abort('fuzz'));
	await page.goto(`http://localhost:${PORT}/duel.html?fast=1`);
}
console.log('request types seen:', JSON.stringify(Object.fromEntries(seen)));
await browser.close();
proc.kill();
if (problems.length) { failed = true; console.log('console problems:\n  ' + [...new Set(problems)].join('\n  ')); }
console.log(failed ? 'UI FUZZ FAILED' : 'UI FUZZ PASSED');
process.exit(failed ? 1 : 0);
