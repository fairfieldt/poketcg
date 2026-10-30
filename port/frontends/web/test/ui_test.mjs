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
const BASE = `http://localhost:${PORT}/?fast=1`;
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
