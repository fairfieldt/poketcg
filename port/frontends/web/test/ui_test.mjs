// Browser test of the web frontend with Playwright (Chromium):
//   1. hot-seat duel: start from the title menu, play a few turns through the UI (mouse + keys),
//      open the Check menu and a card page, take screenshots;
//   2. online duel: two browser contexts duel through the relay server for a few turns, the guest
//      reloads the page mid-duel and rejoins (replaying the duel from the server log);
//   3. deck builder: build/save/export a deck.
// Fails on any console error/warning or page error.
//
//   node test/ui_test.mjs [--port 8130] [--turns 4] [--shots test/screenshots] [--headed]
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import fs from 'node:fs';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : d; };
const PORT = Number(opt('--port', 8130));
const TURNS = Number(opt('--turns', 4));
const SHOTS = path.resolve(opt('--shots', path.join(here, 'screenshots')));
const BASE = `http://localhost:${PORT}/duel.html?fast=1`;
fs.mkdirSync(SHOTS, { recursive: true });

const REQ = { ACK: 1, DUEL_MENU: 2, CARD_LIST: 3, CARD_ITEM_MENU: 4, PLAY_AREA: 5, MENU: 6, YES_NO: 7, ENERGY_DISCARD: 8, PRIZE: 9, COLOR: 10, NUMBER: 11 };
const problems = [];
let failed = false;
const check = (cond, msg) => { if (!cond) { failed = true; console.log('  FAIL: ' + msg); } else console.log('  ok: ' + msg); };

function watch(page, label) {
	page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') problems.push(`[${label}] console ${m.type()}: ${m.text()}`); });
	page.on('pageerror', (e) => problems.push(`[${label}] page error: ${e.message}`));
}

/* what the page is waiting for */
function situation(page) {
	return page.evaluate(() => {
		const app = window.tcgApp, ui = app && app.ui;
		if (!ui) return { kind: 'none' };
		const dlg = document.querySelector('.duel .modal:not(.hidden) .dialog');
		if (dlg) return { kind: 'dialog', text: dlg.textContent };
		const req = ui.req;
		const s = {
			kind: req && ui.controls.length ? 'req' : 'busy', turn: ui.turnNumber ?? -1, text: ui.text,
			status: document.querySelector('.status-line') ? document.querySelector('.status-line').textContent : '',
			seq: app.online ? app.online.seq : null, hash: app.duel.stateHash(),
		};
		if (!req) return s;
		Object.assign(s, { type: req.type, duelist: req.duelist, menu: req.menu, n: req.numItems, arg: req.arg, info: req.text2Str, header: req.textStr, overlay: ui.overlayKind, local: !app.online || req.duelist === app.online.localSide });
		if (req.type === 3) {
			s.cards = Array.from(req.items.slice(0, req.numItems)).map((i) => {
				const c = app.duel.card(app.duel.cardId(req.shown, i));
				return { name: c.name, basic: c.isPokemon && c.stage === 0, energy: c.isEnergy, pokemon: c.isPokemon };
			});
		}
		return s;
	});
}

/* a simple but sensible player: place a basic, attach an energy and bench a basic each turn, attack, done */
async function act(page, s, st) {
	const click = (sel) => page.click(sel, { timeout: 5000 });
	if (st.turn !== s.turn) { st.turn = s.turn; st.energy = false; st.bench = false; st.attack = 0; st.menu = 0; }
	switch (s.type) {
	case REQ.ACK:
		if (st.ackKeys = !st.ackKeys) await page.keyboard.press('z'); else await click('.textbox');
		return 'ack';
	case REQ.DUEL_MENU: {
		const items = ['Hand', 'Attack', 'Done'];
		const label = items[Math.min(st.menu++, 2)];
		await click(`.duel-menu .tm-item:has-text("${label}")`);
		return 'menu ' + label;
	}
	case REQ.CARD_LIST: {
		const cards = s.cards || [];
		let i = -1;
		if (/Active/.test(s.info)) i = cards.findIndex((c) => c.basic);
		else if (/Bench/.test(s.info)) { if (!st.setupBench) { st.setupBench = true; i = cards.findIndex((c) => c.basic); } }
		else if (s.arg[1] === 1) { // hand (PLAY_CHECK)
			if (!st.energy) { st.energy = true; i = cards.findIndex((c) => c.energy); }
			if (i < 0 && !st.bench) { st.bench = true; i = cards.findIndex((c) => c.basic); }
		} else if (!s.arg[0]) i = 0;
		if (i >= 0) { await click(`.overlay .cl-item >> nth=${i}`); return 'pick ' + cards[i].name; }
		await page.keyboard.press('x');
		return 'back';
	}
	case REQ.CARD_ITEM_MENU:
		await click('.item-menu .tm-item >> nth=0');
		return 'play';
	case REQ.PLAY_AREA:
		if (s.arg[1]) { await page.keyboard.press('x'); return 'back'; }
		await click('.overlay .pa-row:not(.disabled) >> nth=0');
		return 'slot';
	case REQ.MENU:
		if (s.menu === 1) {
			if (st.attack++ < 1) { await click('.attack-menu .tm-item >> nth=0'); return 'attack'; }
			await page.keyboard.press('x');
			return 'cancel attack';
		}
		if (await page.$('.overlay .pa-row:not(.disabled)')) { await click('.overlay .pa-row:not(.disabled) >> nth=0'); return 'menu slot'; }
		if (await page.$('.overlay .cl-item')) { await click('.overlay .cl-item >> nth=0'); return 'menu card'; }
		await click('.tm-item >> nth=0');
		return 'menu item';
	case REQ.YES_NO:
		await click('.yes-no .tm-item >> nth=0');
		return 'yes';
	case REQ.PRIZE:
		await click('.prize-card:not(.taken) >> nth=0');
		return 'prize';
	case REQ.ENERGY_DISCARD:
		await click('.overlay .cl-item >> nth=0');
		return 'energy';
	default:
		await page.keyboard.press('z');
		return 'z';
	}
}

/* play until both duelists had `turns` turns (wDuelTurns >= turns), the duel ends or the step budget runs out */
async function play(pages, { turns, maxSteps = 1500, onStep } = {}) {
	const st = pages.map(() => ({ turn: -2 }));
	let last = '', same = 0;
	for (let step = 0; step < maxSteps; step++) {
		let acted = false, done = true;
		for (let k = 0; k < pages.length; k++) {
			const s = await situation(pages[k]);
			if (s.kind === 'dialog') return { ended: s.text };
			if (s.turn < turns) done = false;
			if (s.kind !== 'req' || !s.local) continue;
			const key = `${k}:${s.type}:${s.text}:${s.turn}:${s.seq}`;
			same = key === last ? same + 1 : 0;
			last = key;
			if (same > 25) { await pages[k].keyboard.press('x'); same = 0; continue; }
			if (onStep) await onStep(k, s, 'before');
			const what = await act(pages[k], s, st[k]);
			if (onStep) await onStep(k, s, what);
			acted = true;
			break;
		}
		if (done) return { turns };
		if (!acted) await pages[0].waitForTimeout(40);
	}
	return { timeout: true };
}

async function startServer() {
	const proc = spawn(process.execPath, [path.join(here, '..', 'server', 'server.js'), '--port', String(PORT)], { env: { ...process.env, QUIET: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
	await new Promise((resolve, reject) => {
		proc.stdout.on('data', (d) => { if (String(d).includes('serving')) resolve(); });
		proc.on('exit', (c) => reject(new Error('server exited ' + c)));
		setTimeout(() => reject(new Error('server did not start')), 5000);
	});
	return proc;
}

async function hotseat(browser) {
	console.log('hot-seat duel');
	const ctx = await browser.newContext({ viewport: { width: 1280, height: 900 } });
	const page = await ctx.newPage();
	watch(page, 'hotseat');
	await page.goto(BASE);
	await page.click('text=2 Players (same screen)');
	await page.click('.deck-tile >> nth=0');
	await page.click('.deck-tile >> nth=1');
	await page.waitForSelector('.duel .field');
	await page.screenshot({ path: path.join(SHOTS, 'hotseat-1-start.png') });
	const shot = new Set();
	const res = await play([page], {
		turns: TURNS,
		onStep: async (k, s, what) => {
			const take = async (name) => { if (!shot.has(name)) { shot.add(name); await page.waitForTimeout(150); await page.screenshot({ path: path.join(SHOTS, `hotseat-${name}.png`) }); } };
			if (what === 'before') {
				if (s.type === REQ.CARD_LIST && s.turn >= 0) await take('3-hand');
				if (s.type === REQ.PLAY_AREA) await take('4-play-area');
				if (s.type === REQ.MENU && s.menu === 1) await take('5-attack-menu');
				if (s.type === REQ.ACK && s.overlay === 'coin') await take('0-coin-toss');
				if (s.type === REQ.ACK && s.overlay === 'largecard') await take('0-large-card');
			} else if (what === 'attack') await take('5-after-attack');
		},
	});
	check(!res.timeout, `hot-seat duel progressed (${JSON.stringify(res)})`);
	// duel menu + Check menu + card page
	for (let i = 0; i < 200; i++) {
		const s = await situation(page);
		if (s.kind === 'dialog') break;
		if (s.kind === 'req' && s.type === REQ.DUEL_MENU) {
			await page.screenshot({ path: path.join(SHOTS, 'hotseat-2-duel-menu.png') });
			await page.click('.duel-menu .tm-item:has-text("Check")');
			await page.click('.check-menu .tm-item:has-text("Your Play Area")');
			await page.waitForSelector('.overlay .play-area');
			await page.screenshot({ path: path.join(SHOTS, 'hotseat-6-check-play-area.png') });
			await page.click('.overlay .pa-row >> nth=0');
			await page.waitForSelector('.modal .card-page');
			await page.screenshot({ path: path.join(SHOTS, 'hotseat-7-card-page.png') });
			await page.keyboard.press('x'); // close the card page
			await page.keyboard.press('x'); // back to the check menu
			await page.keyboard.press('x'); // back to the duel menu
			const after = await situation(page);
			check(after.type === REQ.DUEL_MENU && after.kind === 'req', 'Check menu returns to the duel menu');
			break;
		}
		if (s.kind === 'req') await act(page, s, { turn: s.turn, energy: true, bench: true, attack: 9, menu: 9 });
		else await page.waitForTimeout(30);
	}
	const turn = await page.evaluate(() => window.tcgApp.ui.turnNumber);
	check(turn >= TURNS || res.ended, `reached turn ${turn}`);
	await page.screenshot({ path: path.join(SHOTS, 'hotseat-8-later.png') });
	// mobile layout
	await page.setViewportSize({ width: 390, height: 844 });
	await page.evaluate(() => window.dispatchEvent(new Event('resize')));
	await page.waitForTimeout(200);
	await page.screenshot({ path: path.join(SHOTS, 'hotseat-9-mobile.png') });
	await ctx.close();
}

/* play until the local duelist is at the duel menu */
async function toDuelMenu(page, maxSteps = 400) {
	const st = { turn: -2 };
	for (let i = 0; i < maxSteps; i++) {
		const s = await situation(page);
		if (s.kind === 'req' && s.type === REQ.DUEL_MENU) return s;
		if (s.kind === 'dialog') return s;
		if (s.kind === 'req') await act(page, s, Object.assign(st, { energy: true, bench: true, attack: 9, menu: 9, turn: s.turn }));
		else await page.waitForTimeout(30);
	}
	return null;
}

/* the debug skip-delay option, saving, Continue Duel, save file export/import */
async function saveContinue(browser) {
	console.log('save files / continue duel');
	const ctx = await browser.newContext({ viewport: { width: 1100, height: 900 }, acceptDownloads: true });
	const page = await ctx.newPage();
	watch(page, 'save');
	page.on('dialog', (d) => d.accept());
	await page.goto(BASE);
	await page.check('.skip-delay');
	await page.click('text=2 Players (same screen)');
	await page.click('.deck-tile >> nth=3');
	await page.click('.deck-tile >> nth=4');
	await page.waitForSelector('.duel .field');
	// pass turns (no attacks, so no early knock out) until a duel menu of turn 4
	let atMenu = null;
	for (let i = 0; i < 12; i++) {
		atMenu = await toDuelMenu(page);
		if (!atMenu || atMenu.type !== REQ.DUEL_MENU || await page.evaluate(() => window.tcgApp.duel.w('wDuelTurns')) >= 3) break;
		await page.click('.duel-menu .tm-item:has-text("Done")');
	}
	check(atMenu && atMenu.type === REQ.DUEL_MENU, 'at a duel menu after turn 3');
	check(await page.evaluate(() => window.tcgApp.duel.w('wSkipDelayAllowed')) === 1, 'skip-delay option reaches the engine (wSkipDelayAllowed = 1)');
	await page.keyboard.down('x');
	const ff = await page.evaluate(() => document.querySelector('.duel').classList.contains('fast-forward'));
	await page.keyboard.up('x');
	const ff2 = await page.evaluate(() => document.querySelector('.duel').classList.contains('fast-forward'));
	check(ff && !ff2, 'holding B fast-forwards (no pacing, no animations) while the option is on');
	const before = await page.evaluate(() => ({ hash: window.tcgApp.duel.stateHash(), turns: window.tcgApp.duel.w('wDuelTurns'), saves: window.tcgApp.saves }));
	check(before.saves > 0, `the duel was saved ${before.saves} times`);
	await page.click('.duel-bar >> text=Quit');
	const cont = await page.waitForSelector('.title .btn.continue');
	check(/turn \d+/.test(await cont.textContent()), `title offers Continue Duel: "${(await cont.textContent()).replace(/\s+/g, ' ')}"`);
	await page.screenshot({ path: path.join(SHOTS, 'save-1-title.png') });
	// export / import through the File System Access API (mocked pickers) ...
	await page.evaluate(() => {
		window.showSaveFilePicker = async () => ({ createWritable: async () => ({ write: async (d) => { window.__exported = Array.from(d); }, close: async () => {} }) });
		window.showOpenFilePicker = async () => [{ getFile: async () => new File([new Uint8Array(window.__exported)], 'duel.sav') }];
	});
	await page.click('text=Export save file');
	await page.waitForFunction(() => window.__exported);
	check(await page.evaluate(() => window.__exported.length) === 1660, 'export through showSaveFilePicker writes the 1660-byte save');
	await page.click('.save-row >> text=Delete');
	await page.waitForFunction(() => !document.querySelector('.title .btn.continue'));
	await page.click('text=Import save file');
	await page.waitForSelector('.title .btn.continue');
	check(true, 'import through showOpenFilePicker restores Continue Duel');
	// ... and without it: download link / <input type=file>
	await page.evaluate(() => { delete window.showSaveFilePicker; delete window.showOpenFilePicker; });
	const [download] = await Promise.all([page.waitForEvent('download'), page.click('text=Export save file')]);
	const file = path.join(SHOTS, 'duel.sav');
	await download.saveAs(file);
	check(fs.statSync(file).size === 1660 && download.suggestedFilename() === 'duel.sav', 'export fallback downloads duel.sav (1660 bytes)');
	await page.click('.save-row >> text=Delete');
	await page.waitForFunction(() => !document.querySelector('.title .btn.continue'));
	const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.click('text=Import save file')]);
	await chooser.setFiles(file);
	await page.waitForSelector('.title .btn.continue');
	check(true, 'import fallback (<input type=file>) restores Continue Duel');
	fs.unlinkSync(file);
	// continue: the duel resumes at the saved duel menu with the same state
	await page.click('.title .btn.continue');
	await page.waitForSelector('.duel .field');
	const resumed = await toDuelMenu(page, 50);
	const after = await page.evaluate(() => ({ hash: window.tcgApp.duel.stateHash(), turns: window.tcgApp.duel.w('wDuelTurns') }));
	check(resumed && resumed.type === REQ.DUEL_MENU && after.hash === before.hash && after.turns === before.turns,
		`continued duel resumes at the same duel menu (hash ${after.hash.toString(16)} = ${before.hash.toString(16)}, turn ${after.turns + 1})`);
	await page.screenshot({ path: path.join(SHOTS, 'save-2-continued.png') });
	const res = await play([page], { turns: before.turns + 2 });
	check(!res.timeout, `continued duel plays on (${JSON.stringify(res)})`);
	await ctx.close();
}

/* the practice duel with Sam (Dr. Mason's instructions) */
async function practice(browser) {
	console.log('practice duel');
	const ctx = await browser.newContext({ viewport: { width: 1100, height: 900 } });
	const page = await ctx.newPage();
	watch(page, 'practice');
	await page.goto(BASE);
	await page.click('text=Duel vs. Computer');
	await page.click('.deck-tile >> nth=0');
	await page.click('text=Practice duel with Sam');
	await page.waitForSelector('.duel .field');
	const texts = new Set();
	const st = { turn: -2 };
	for (let i = 0; i < 120; i++) {
		const s = await situation(page);
		if (s.text) texts.add(s.text.split('\n')[0]);
		if (s.kind === 'dialog') break;
		if (i === 30) await page.screenshot({ path: path.join(SHOTS, 'practice-1.png') });
		if (s.kind === 'req') await act(page, s, st); else await page.waitForTimeout(30);
	}
	const prizes = await page.evaluate(() => window.tcgApp.duel.w('wDuelInitialPrizes'));
	const practiceDuel = await page.evaluate(() => window.tcgApp.duel.w('wIsPracticeDuel'));
	check(prizes === 2 && practiceDuel !== 0, `practice duel set up by the engine (2 prizes, wIsPracticeDuel = ${practiceDuel})`);
	check([...texts].some((t) => /practice/i.test(t)), `practice texts shown, e.g. ${JSON.stringify([...texts].find((t) => /practice/i.test(t)))}`);
	await page.screenshot({ path: path.join(SHOTS, 'practice-2.png') });
	await ctx.close();
}

async function online(browser) {
	console.log('online duel');
	const hostCtx = await browser.newContext({ viewport: { width: 1000, height: 860 } });
	const guestCtx = await browser.newContext({ viewport: { width: 1000, height: 860 } });
	const host = await hostCtx.newPage(), guest = await guestCtx.newPage();
	watch(host, 'host');
	watch(guest, 'guest');
	await host.goto(BASE);
	await guest.goto(BASE);
	await host.click('text=Online duel');
	await host.click('.deck-tile >> nth=0');
	await host.click('text=Create a room');
	const code = (await (await host.waitForSelector('.room-display')).textContent()).trim();
	check(/^[A-Z]{4}$/.test(code), `room created: ${code}`);
	await host.screenshot({ path: path.join(SHOTS, 'online-1-room.png') });
	await guest.click('text=Online duel');
	await guest.click('.deck-tile >> nth=2');
	await guest.fill('.room-code', code);
	await guest.click('text=Join room');
	await host.waitForSelector('.duel .field');
	await guest.waitForSelector('.duel .field');
	let res = await play([host, guest], { turns: 2 });
	check(!res.timeout, `online duel reached turn 2 (${JSON.stringify(res)})`);
	await host.screenshot({ path: path.join(SHOTS, 'online-2-host.png') });
	await guest.screenshot({ path: path.join(SHOTS, 'online-2-guest.png') });
	// the guest reloads the page: the host is told, the guest rejoins and replays the duel
	const before = await situation(guest);
	await guest.reload();
	await host.waitForFunction(() => /lost the connection/.test(document.querySelector('.status-line').textContent), null, { timeout: 5000 });
	check(true, 'host is told the guest disconnected');
	await guest.click('text=Rejoin online duel');
	await guest.waitForSelector('.duel .field');
	await guest.waitForFunction((seq) => window.tcgApp.online && window.tcgApp.online.seq >= seq, before.seq, { timeout: 10000 });
	check(true, `guest replayed the duel up to answer #${before.seq}`);
	res = await play([host, guest], { turns: TURNS });
	check(!res.timeout, `online duel continued to turn ${TURNS} (${JSON.stringify(res)})`);
	// both engines must be in the same state when they wait at the same request
	// (click through local ACKs until one side waits for its own input and the other for the network)
	let same = null;
	for (let i = 0; i < 200 && same === null; i++) {
		const [a, b] = [await situation(host), await situation(guest)];
		const ack = [[host, a], [guest, b]].find(([, s]) => s.kind === 'req' && s.type === REQ.ACK);
		if (ack) { await ack[0].keyboard.press('z'); continue; }
		const atRequest = (s) => s.kind === 'req' && s.type !== REQ.ACK;
		if (a.seq === b.seq && (atRequest(a) || atRequest(b)) && (atRequest(a) !== atRequest(b))) same = a.hash === b.hash ? a.hash : false;
		else await host.waitForTimeout(50);
	}
	check(!!same, `host and guest engines are in the same state at the same request (hash ${same && same.toString(16)})`);
	await host.screenshot({ path: path.join(SHOTS, 'online-3-host.png') });
	await guest.screenshot({ path: path.join(SHOTS, 'online-3-guest.png') });
	const st = [await situation(host), await situation(guest)];
	check(!/Desync/i.test(st[0].status + st[1].status), 'no desync reported');
	const saved = await Promise.all([host, guest].map((p) => p.evaluate(() => localStorage.getItem('tcg.save.duel'))));
	check(saved[0] === null && saved[1] === null, 'online duels are not saved (no Continue Duel for link duels)');
	await hostCtx.close();
	await guestCtx.close();
}

async function vsComputer(browser) {
	console.log('duel vs. computer');
	const ctx = await browser.newContext({ viewport: { width: 1280, height: 900 } });
	const page = await ctx.newPage();
	watch(page, 'vs-ai');
	await page.goto(BASE);
	await page.click('text=Duel vs. Computer');
	await page.click('.deck-tile >> nth=0');
	await page.click('.deck-grid .deck-tile:has-text("Lightning & Fire")');
	await page.waitForSelector('.duel .field');
	const aiPorted = await page.evaluate(() => window.tcgApp.info.ai.length > 0);
	if (!aiPorted) {
		const dlg = await page.waitForSelector('.dialog.fatal', { timeout: 10000 });
		const text = await dlg.textContent();
		check(/not ported/i.test(text), `without the AI port the duel stops with a clear message: "${text.replace(/\s+/g, ' ').slice(0, 90)}…"`);
		await page.screenshot({ path: path.join(SHOTS, 'vs-ai-stub.png') });
		await page.click('.dialog >> text=Main menu');
		await page.waitForSelector('.title');
	} else {
		const res = await play([page], { turns: 3 });
		check(!res.timeout, `duel vs. the AI progressed (${JSON.stringify(res)})`);
		await page.screenshot({ path: path.join(SHOTS, 'vs-ai.png') });
	}
	await ctx.close();
}

/* every request type's widget, driven with synthetic requests on a board left by a real duel */
async function requestWidgets(browser) {
	console.log('request widgets');
	const ctx = await browser.newContext({ viewport: { width: 1100, height: 900 } });
	const page = await ctx.newPage();
	watch(page, 'widgets');
	page.on('dialog', (d) => d.accept());
	await page.goto(BASE);
	await page.click('text=2 Players (same screen)');
	await page.click('.deck-tile >> nth=0');
	await page.click('.deck-tile >> nth=1');
	await page.waitForSelector('.duel .field');
	await play([page], { turns: 2 });
	await page.click('.duel-bar >> text=Quit');
	await page.waitForSelector('.title');
	const results = await page.evaluate(async () => {
		const { DuelUI } = await import('./js/duel_ui.js');
		const app = window.tcgApp;
		if (app.keyHandler) { document.removeEventListener('keydown', app.keyHandler); app.keyHandler = null; } // the title menu's keys
		const root = document.createElement('div');
		document.getElementById('app').replaceChildren(root);
		const ui = new DuelUI(root, app.duel, { mode: 'hotseat', pacing: false });
		ui.names = ['P1', 'P2'];
		const key = (k) => document.dispatchEvent(new KeyboardEvent('keydown', { key: k, bubbles: true }));
		const tick = () => new Promise((r) => setTimeout(r, 20));
		const req = (o) => ({ duelist: 0xc2, shown: 0xc2, menu: 0, numItems: 0, cursor: 0, text: 0, text2: 0, str: null, textStr: '', text2Str: '', items: new Uint8Array(128), arg: [0, 0, 0, 0], seq: 0, ...o });
		let captured = '';
		const run = async (r, actions) => {
			const p = ui.onRequest(r);
			await tick();
			for (const a of actions) {
				if (a.startsWith('labels:')) { captured = [...document.querySelectorAll(a.slice(7))].map((e) => e.textContent).join('|'); continue; }
				if (a.startsWith('click:')) {
					const e = document.querySelector(a.slice(6));
					if (!e) return `no element ${a.slice(6)} in: ${root.querySelector('.overlay').innerHTML.slice(0, 300)}`;
					e.click();
				} else key(a);
				await tick();
			}
			return Promise.race([p, new Promise((res) => setTimeout(() => res('timeout'), 500))]);
		};
		const items = (a) => { const u = new Uint8Array(128); u.set(a); return u; };
		return {
			confirmA: await run(req({ type: 12 }), ['z']),
			confirmB: await run(req({ type: 12 }), ['x']),
			peekArea: await run(req({ type: 6, menu: 5, numItems: 2 }), ['ArrowDown', 'z']),
			peekTargetPrize: await run(req({ type: 6, menu: 6, numItems: 8, arg: [0b000101, 1, 0, 0] }), ['ArrowRight', 'z', 'ArrowRight', 'z']),
			peekTargetDeck: await run(req({ type: 6, menu: 6, numItems: 8, arg: [0, 0, 0, 0] }), ['click:.peek-extra .btn:last-child']),
			colorDefault: await run(req({ type: 10, numItems: 6, cursor: 3, items: items([1, 0, 3, 2, 4, 5]) }), ['z']),
			colorMoved: await run(req({ type: 10, numItems: 6, cursor: 3, items: items([1, 0, 3, 2, 4, 5]) }), ['ArrowRight', 'z']),
			yesNoTwoItems: await run(req({ type: 7, numItems: 2, cursor: 1, arg: [1, 0, 0, 0], textStr: "Please select the Deck:\n            Yours   Opponent's" }), ['labels:.yes-no .tm-item', 'ArrowRight', 'z']),
			twoItemLabels: captured,
			twoItems: await run(req({ type: 6, menu: 3, numItems: 2, cursor: 0, textStr: "Please select the Play Area:\n            Yours   Opponent's" }), ['labels:.two-items .tm-item', 'ArrowRight', 'z']),
			twoItemsLabels: captured,
			yesNoB: await run(req({ type: 7, numItems: 2, cursor: 1 }), ['x']),
			prize: await run(req({ type: 9, numItems: 6, arg: [0b100000, 0, 0, 0] }), ['z', 'click:.prize-card:not(.taken)']),
			number: await run(req({ type: 11, numItems: 3 }), ['ArrowRight', 'z']),
			generic: await run(req({ type: 6, menu: 0, numItems: 3 }), ['ArrowRight', 'z']),
			duelMenu: await run(req({ type: 2, numItems: 6, cursor: 0 }), ['ArrowDown', 'ArrowRight', 'z']),
			duelMenuB: await run(req({ type: 2, numItems: 6, cursor: 5 }), ['x', 'z']),
		};
	});
	const expect = {
		confirmA: 0, confirmB: -1, peekArea: 1, peekTargetPrize: 2, peekTargetDeck: 7, colorDefault: 3, colorMoved: 2,
		yesNoTwoItems: 0, twoItemLabels: "Yours|Opponent's", twoItems: 1, twoItemsLabels: "Yours|Opponent's", yesNoB: 0, prize: 5, number: 1, generic: 1, duelMenu: 3, duelMenuB: 5,
	};
	for (const [k, v] of Object.entries(expect)) check(results[k] === v, `${k}: ${JSON.stringify(results[k])}${results[k] === v ? '' : ` (expected ${JSON.stringify(v)})`}`);
	await ctx.close();
}

async function deckBuilder(browser) {
	console.log('deck builder');
	const ctx = await browser.newContext({ viewport: { width: 1280, height: 900 } });
	const page = await ctx.newPage();
	watch(page, 'deckbuilder');
	page.on('dialog', (d) => d.accept());
	await page.goto(BASE);
	await page.click('text=Deck builder');
	await page.waitForSelector('.db-card');
	await page.selectOption('.db-load', { label: 'Charmander & Friends' });
	await page.waitForFunction(() => document.querySelector('.db-count').textContent === '60/60');
	check(true, 'loaded a pre-constructed deck (60/60)');
	await page.fill('.db-search', 'charmander');
	const plus = page.locator('.db-card .btn.add').first();
	const disabledBefore = await plus.isDisabled();
	check(disabledBefore, 'cannot add a 61st card');
	await page.fill('.db-search', '');
	for (let i = 0; i < 3; i++) await page.click('.db-drow:has-text("Fire Energy") .btn >> nth=0'); // remove 3 Fire Energy
	await page.fill('.db-search', 'Charmander');
	const before = await page.evaluate(() => document.querySelector('.db-count').textContent);
	for (let i = 0; i < 4; i++) await page.locator('.db-card:has-text("Charmander Lv10") .btn.add').click({ force: true, timeout: 1000 }).catch(() => {});
	const count = await page.evaluate(() => document.querySelector('.db-count').textContent);
	const charmanders = await page.evaluate(() => document.querySelector('.db-card .db-num').textContent);
	check(before === '57/60' && count === '59/60' && charmanders === '×4', `same-name limit enforced (${before} -> ${count}, Charmander ${charmanders})`);
	await page.fill('.db-search', 'Fire Energy');
	await page.locator('.db-card:has-text("Fire Energy") .btn.add').first().click();
	check(await page.evaluate(() => document.querySelector('.db-count').textContent) === '60/60', 'basic energy has no same-name limit');
	await page.fill('.db-name', 'Test Deck');
	await page.click('.db-actions >> text=Save');
	const saved = await page.evaluate(() => JSON.parse(localStorage.getItem('tcg.customDecks') || '[]'));
	check(saved.length === 1 && saved[0].cards.length === 60 && saved[0].name === 'Test Deck', 'deck saved in localStorage');
	await page.click('text=Export code');
	const code = await page.inputValue('.dialog textarea');
	check(/^TCG1-/.test(code), 'deck exported as a code');
	await page.screenshot({ path: path.join(SHOTS, 'deckbuilder-1.png') });
	await page.click('.dialog button:has-text("Close")');
	await page.click('text=Clear');
	await page.click('text=Import code');
	await page.fill('.dialog textarea', code);
	await page.click('.dialog button.primary');
	const count2 = await page.evaluate(() => document.querySelector('.db-count').textContent);
	check(count2 === '60/60', 'deck code imported');
	await page.click('text=◀ Menu');
	await page.click('text=2 Players (same screen)');
	await page.waitForSelector('.deck-tile:has-text("Test Deck")');
	check(true, 'custom deck offered in deck selection');
	await ctx.close();
}

const server = await startServer();
const browser = await chromium.launch({ headless: !args.includes('--headed') });
try {
	await hotseat(browser);
	await online(browser);
	await vsComputer(browser);
	await saveContinue(browser);
	await practice(browser);
	await requestWidgets(browser);
	await deckBuilder(browser);
} catch (e) {
	failed = true;
	console.log('  FAIL: ' + (e.stack || e));
} finally {
	await browser.close();
	server.kill();
}
if (problems.length) { failed = true; console.log('console problems:\n  ' + problems.join('\n  ')); } else console.log('  ok: no console errors or warnings');
console.log(failed ? 'UI TEST FAILED' : 'UI TEST PASSED', `(screenshots in ${SHOTS})`);
process.exit(failed ? 1 : 0);
