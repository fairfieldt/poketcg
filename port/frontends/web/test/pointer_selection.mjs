// Exercise real browser clicks, pointer moves and confirms against source UI.
// Generated metadata/WASM/art come from the existing dist; no build is needed.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(import.meta.url);
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const source = process.env.POINTER_TEST_JS_ROOT || path.join(web, 'src/js');
const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.wasm': 'application/wasm', '.png': 'image/png' };
const server = http.createServer((request, response) => {
    const name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
    const candidate = name.startsWith('/js/') ? path.join(source, name.slice(4)) :
        name.startsWith('/css/') ? path.join(web, 'src/css', name.slice(5)) : '';
    const file = candidate && fs.existsSync(candidate) ? candidate : path.join(web, 'dist', name);
    fs.readFile(file, (error, bytes) => {
        if (error) { response.writeHead(404).end(); return; }
        response.writeHead(200, { 'content-type': mime[path.extname(file)] || 'application/octet-stream' });
        response.end(bytes);
    });
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}/duel.html?fast=1`;
let browser;
try {
    browser = await chromium.launch({ channel: 'chrome', headless: true });
    const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
    page.setDefaultTimeout(10000);
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(base);
    await page.waitForSelector('.title .menu .btn');

    // Standalone title/deck menus share their keyboard cursor with clicks.
    await page.locator('.title .menu .btn').nth(1).hover();
    assert.match(await page.locator('.title .menu .btn.cur').textContent(), /Duel vs\. Computer/);
    assert.notEqual(await page.locator('.title .menu .btn').nth(0).evaluate(e => getComputedStyle(e).backgroundColor),
        await page.locator('.title .menu .btn').nth(1).evaluate(e => getComputedStyle(e).backgroundColor), 'Hover cannot look like the selected menu item');
    await page.keyboard.press('ArrowDown');
    await page.locator('.title .menu .btn').nth(2).hover();
    assert.match(await page.locator('.title .menu .btn.cur').textContent(), /2 Players/);
    await page.keyboard.press('z');
    assert.equal(await page.locator('h2').textContent(), 'Player 1: choose a deck');
    await page.locator('.deck-tile:not(:disabled)').nth(2).hover();
    assert(await page.locator('.deck-tile:not(:disabled)').first().evaluate(e => e.classList.contains('cur')));
    await page.locator('.deck-tile:not(:disabled)').nth(1).click();
    assert.equal(await page.locator('h2').textContent(), 'Player 2: choose a deck');

    // Use real compiled card metadata with deterministic deck/slot fixtures.
    await page.evaluate(async () => {
        const { DuelUI, K } = await import('./js/duel_ui.js');
        const { C, REQ, TCG_PLAYER, TCG_OPPONENT } = await import('./js/engine.js');
        const app = window.tcgApp;
        document.removeEventListener('keydown', app.keyHandler); app.keyHandler = null;
        const root = document.createElement('div'); app.root.replaceChildren(root);
        const duel = Object.create(app.duel);
        const ids = [C.GOLDEEN, C.SEAKING, C.PIKACHU_LV12];
        if (!ids.every(id => Number.isInteger(id) && id > 0 && id <= duel.numCards)) throw new Error('Invalid card fixture');
        duel.cardId = (_owner, index) => ids[index];
        const ui = new DuelUI(root, duel, { pacing: false });
        const fixture = window.pointerFixture = { ui, K, C, REQ, TCG_PLAYER, TCG_OPPONENT, selected: [], answer: null };
        fixture.req = (type, rest = {}) => ({ type, duelist: TCG_PLAYER, shown: TCG_PLAYER, cursor: 0, numItems: 3,
            arg: [0, 0, 0, 0], items: new Uint8Array([0, 1, 2]), textStr: '', text2Str: '', ...rest });
        fixture.reset = () => { ui.controls = []; ui.hideOverlay(); ui.clearMenu(); fixture.selected = []; fixture.answer = null; };
        fixture.install = kind => {
            fixture.reset();
            let control;
            const select = i => fixture.selected.push(i);
            if (kind === 'list') {
                control = ui.cardListView({ d: TCG_PLAYER, items: [0, 1, 2], onSelect: select });
                ui.showOverlay('cardlist', control.wrap);
            } else if (kind === 'targets') {
                const state = ui.sideState.bind(ui);
                ui.sideState = d => ({ d, hand: 3, deck: 60, slots: ids.map(id => ({ card: duel.card(id), hp: 60,
                    color: 3, energies: [0, 0, 0, 0, 0, 0, 0], status: 0 })) });
                control = ui.playAreaView({ d: TCG_PLAYER, onSelect: select });
                ui.sideState = state;
                ui.showOverlay('playarea', control.wrap);
            } else control = ui.textMenu({ items: ['Hand', 'Attack', 'Done'].map(label => ({ label })), onSelect: select });
            fixture.control = control; ui.pushControl(control);
        };
    });
    const selected = () => page.evaluate(() => window.pointerFixture.selected);
    const install = kind => page.evaluate(kind => window.pointerFixture.install(kind), kind);
    const controller = key => page.evaluate(key => {
        const f = window.pointerFixture; f.ui.controls.at(-1).key(f.K[key]);
    }, key);

    for (const [kind, selector] of [['list', '.cl-item'], ['targets', '.pa-row'], ['menu', '.tm-item']]) {
        await install(kind);
        const rows = page.locator(selector);
        await rows.nth(1).click();
        await rows.nth(2).hover();
        assert.notEqual(await rows.nth(1).evaluate(e => getComputedStyle(e).backgroundColor),
            await rows.nth(2).evaluate(e => getComputedStyle(e).backgroundColor), `${kind}: hover cannot look selected`);
        await page.keyboard.press('z');
        assert.deepEqual(await selected(), [1, 1], `${kind}: click then hover must confirm the clicked item`);
        await page.keyboard.press('ArrowDown');
        await rows.nth(0).hover();
        await page.keyboard.press('z');
        assert.equal((await selected()).at(-1), 2, `${kind}: keyboard selection survives hover`);
        await controller('UP');
        await rows.nth(2).hover();
        await controller('A');
        assert.equal((await selected()).at(-1), 1, `${kind}: controller selection survives hover`);
        await rows.nth(0).click();
        assert.equal((await selected()).at(-1), 0, `${kind}: another explicit click changes selection`);
    }

    // Actual hand-list request followed by its retained Play/Check action menu.
    await page.evaluate(() => {
        const f = window.pointerFixture; f.reset();
        f.ui.onRequest(f.req(f.REQ.CARD_LIST, { arg: [0, 1, 0, 0] })).then(value => { f.answer = value; });
    });
    await page.locator('.cl-item').nth(1).click();
    await page.waitForFunction(() => window.pointerFixture.answer === 1);
    await page.evaluate(() => {
        const f = window.pointerFixture; f.answer = null;
        f.ui.onRequest(f.req(f.REQ.CARD_ITEM_MENU, { arg: [1, 0, 0, 0], textStr: 'Play\nCheck' }))
            .then(value => { f.answer = value; });
    });
    await page.locator('.cl-item').nth(2).hover();
    assert.equal(await page.evaluate(() => window.pointerFixture.ui.cardListCtl.cursor), 1);
    assert.match(await page.locator('.cl-item.cur').textContent(), /Seaking/);
    assert.match(await page.locator('.cl-item.picked').textContent(), /Seaking/);
    await page.locator('.cl-item').nth(2).click();
    await page.waitForFunction(() => window.pointerFixture.answer === -1);
    await page.evaluate(() => {
        const f = window.pointerFixture; f.answer = null;
        f.ui.onRequest(f.req(f.REQ.CARD_LIST, { cursor: 1, arg: [0, 1, 0, 0] })).then(value => { f.answer = value; });
    });
    await page.waitForFunction(() => window.pointerFixture.answer === 2);
    await page.evaluate(() => {
        const f = window.pointerFixture; f.answer = null;
        f.ui.onRequest(f.req(f.REQ.CARD_ITEM_MENU, { arg: [2, 0, 0, 0], textStr: 'Play\nCheck' }))
            .then(value => { f.answer = value; });
    });
    await page.locator('.cl-item').nth(0).hover();
    assert.equal(await page.evaluate(() => window.pointerFixture.ui.cardListCtl.cursor), 2);
    assert.match(await page.locator('.cl-item.picked').textContent(), /Pikachu/);
    await page.locator('.item-menu .tm-item').first().click();
    await page.waitForFunction(() => window.pointerFixture.answer === 0);

    await page.evaluate(() => {
        const f = window.pointerFixture; f.reset();
        f.ui.browsePlayArea = async side => { f.selected.push(side); };
        f.ui.openCheckMenu(f.TCG_PLAYER);
    });
    await page.locator('.check-menu .tm-item').nth(1).click();
    await page.locator('.check-menu .tm-item').nth(0).hover();
    await page.keyboard.press('z');
    assert.deepEqual(await selected(), [0xc3, 0xc3], 'Check menu keeps the clicked side after returning');
    await page.locator('.check-menu .tm-item').nth(0).click();
    assert.equal((await selected()).at(-1), 0xc2, 'Check menu accepts an explicit different choice');

    // Prize and Peek cursors must also ignore a pointer left over their grid.
    for (const kind of ['prize', 'peek']) {
        await page.evaluate(kind => {
            const f = window.pointerFixture; f.reset();
            const request = f.req(kind === 'prize' ? f.REQ.PRIZE : f.REQ.MENU, { menu: 6, cursor: 1, arg: [63, 0, 0, 0] });
            f.ui.onRequest(request).then(value => { f.answer = value; });
        }, kind);
        await page.locator('.prize-card').nth(4).hover();
        await page.keyboard.press('z');
        await page.waitForFunction(() => window.pointerFixture.answer !== null);
        assert.equal(await page.evaluate(() => window.pointerFixture.answer), 1, `${kind}: hover cannot steal confirm`);
        await page.evaluate(kind => {
            const f = window.pointerFixture; f.reset();
            const request = f.req(kind === 'prize' ? f.REQ.PRIZE : f.REQ.MENU, { menu: 6, cursor: 1, arg: [63, 0, 0, 0] });
            f.ui.onRequest(request).then(value => { f.answer = value; });
        }, kind);
        await page.locator('.prize-card').nth(4).click();
        await page.waitForFunction(() => window.pointerFixture.answer !== null);
        assert.equal(await page.evaluate(() => window.pointerFixture.answer), 4, `${kind}: explicit click chooses another target`);
    }

    // Verify explicit hand reselection through the actual compiled C request loop.
    await page.goto(base);
    await page.getByRole('button', { name: '2 Players (same screen)', exact: true }).click();
    await page.locator('.deck-tile:not(:disabled)').first().click();
    await page.locator('.deck-tile:not(:disabled)').first().click();
    await page.waitForSelector('.duel .field');
    let handReady = false;
    for (let step = 0; step < 100; step++) {
        const request = await page.evaluate(() => {
            const app = window.tcgApp, r = app.ui?.req;
            if (!r || !app.ui.controls.length) return null;
            const choices = r.type === 3 ? Array.from(r.items.slice(0, r.numItems)).map(index => app.duel.card(app.duel.cardId(r.shown, index))) : [];
            return { type: r.type, arg: r.arg, info: r.text2Str, basic: choices.findIndex(card => card.isPokemon && card.stage === 0) };
        });
        if (!request) { await page.waitForTimeout(10); continue; }
        if (request.type === 2) { await page.locator('.duel-menu .tm-item').first().click(); continue; }
        if (request.type === 3 && request.arg[1] === 1) { handReady = true; break; }
        if (request.type === 3 && request.basic >= 0 && !/Bench/i.test(request.info)) await page.locator('.cl-item').nth(request.basic).click();
        else await page.keyboard.press(request.type === 3 ? 'x' : 'z');
    }
    assert(handReady, 'Real duel reached the hand list');
    const deckIndices = await page.evaluate(() => Array.from(window.tcgApp.ui.req.items.slice(0, 3)));
    assert(deckIndices.length >= 3);
    await page.locator('.cl-item').nth(0).click();
    await page.waitForFunction(deck => window.tcgApp.ui.req.type === 4 && window.tcgApp.ui.req.arg[0] === deck, deckIndices[0]);
    await page.locator('.cl-item').nth(2).hover();
    assert.equal(await page.evaluate(() => window.tcgApp.ui.cardListCtl.cursor), 0);
    await page.locator('.cl-item').nth(1).click();
    await page.waitForFunction(deck => window.tcgApp.ui.req.type === 4 && window.tcgApp.ui.req.arg[0] === deck, deckIndices[1]);
    await page.locator('.cl-item').nth(2).hover();
    assert.equal(await page.evaluate(() => window.tcgApp.ui.cardListCtl.cursor), 1);
    assert.deepEqual(errors, []);
    console.log('POINTER SELECTION PASS: real click→hover→confirm, actual C hand reselection, keyboard/controller, explicit clicks, menus/targets/prizes/peek; no page errors');
} finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
