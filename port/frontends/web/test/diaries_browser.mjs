// Live-browser regression for unlimited diary IDs and the title Start button.
// Each context has isolated browser storage; the user's open browser is untouched.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(import.meta.url);
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const url = new URL(process.env.ADVENTURE_URL || 'http://ms.tdf.sh:8091/');
url.searchParams.set('fast', '1');
const output = path.join(web, 'build/diaries-browser');
fs.mkdirSync(output, { recursive: true });
const storageKey = 'poketcg.adventure.cartridge.v1';
const errors = [], startPositions = [];
let browser, page;

function watch(target) {
    target.setDefaultTimeout(15000);
    target.on('pageerror', error => errors.push(error.message));
    target.on('console', event => { if (event.type() === 'error') errors.push(event.text()); });
}
async function picker(target) {
    await target.getByRole('heading', { name: 'Choose diary', exact: true }).waitFor();
}
async function startToPicker(target, label) {
    await target.waitForSelector('.adventure-title-start:not([disabled])', { timeout: 60000 });
    const button = target.getByRole('button', { name: 'Start', exact: true });
    const before = await button.boundingBox();
    assert(before);
    assert(Math.abs(before.x + before.width / 2 - target.viewportSize().width / 2) < 1, `${label}: Start is centered`);
    await target.mouse.move(before.x + before.width / 2, before.y + before.height / 2);
    await target.mouse.down();
    const held = await button.boundingBox();
    assert(Math.abs(held.x - before.x) < .1, `${label}: Start stays centered during mousedown`);
    await target.mouse.up();
    const after = await button.boundingBox();
    assert(after && Math.abs(after.x - before.x) < .1, `${label}: Start stays centered after intro skip`);
    startPositions.push({ label, beforeX: before.x, heldX: held.x, afterX: after.x });
    await button.click();
    await picker(target);
}
async function continueDiary(target, id) {
    await target.getByRole('button', { name: new RegExp(`^Diary ${id}(?::|$)`) }).click();
    await target.getByRole('heading', { name: `Diary ${id}`, exact: true }).waitFor();
    await target.getByRole('button', { name: 'Continue', exact: true }).click();
    await target.waitForFunction(id => {
        const app = window.tcgAdventure;
        return app?.engine.profileId === id && app.active && !app.engine.busy && !app.modal && app.mode === 'world';
    }, id, { timeout: 60000 });
    return target.evaluate(async () => {
        const app = window.tcgAdventure;
        const { C } = await import(new URL('js/engine.js', location.href).href);
        return { id: app.engine.profileId, name: app.engine.name, view: app.engine.view,
            mason: app.engine.x.web_adv_event(C.EVENT_MASON_LAB_STATE),
            receivedStarter: C.MASON_LAB_RECEIVED_STARTER_DECK, laboratory: C.MASON_LABORATORY,
            duel: !!app.ui, pending: Object.keys(app.engine.cartridge.files).filter(key => /^duel(?:-|owner)/.test(key)) };
    });
}
async function returnToPicker(target) {
    await target.getByRole('button', { name: 'Menu', exact: true }).click();
    await target.getByRole('button', { name: 'Choose diary', exact: true }).click();
    await picker(target);
}
function completedTutorial(state) {
    assert.equal(state.view.totalCards, 90);
    assert.equal(state.mason, state.receivedStarter);
    assert.equal(state.duel, false);
    assert.deepEqual(state.pending, []);
}

try {
    browser = await chromium.launch({ channel: 'chrome', headless: true,
        args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
    const context = await browser.newContext({ viewport: { width: 1280, height: 900 } });
    page = await context.newPage(); watch(page);
    await page.goto(url.href);
    await startToPicker(page, 'desktop');
    assert.equal(await page.getByRole('button', { name: 'Diary 1', exact: true }).count(), 1);
    assert.equal(await page.getByRole('button', { name: 'Diary 2: MARK', exact: true }).count(), 1);
    assert.deepEqual(await page.evaluate(() => window.tcgAdventure.engine.diaries().map(diary => diary.id)), [2]);
    const seeded = await continueDiary(page, 2); completedTutorial(seeded);
    assert.equal(seeded.view.map, seeded.laboratory);
    await returnToPicker(page);
    const seedTwo = await page.evaluate(() => window.tcgAdventure.engine.cartridge.files.adventure2);

    const installed = await page.evaluate(() => {
        const engine = window.tcgAdventure.engine, bytes = engine.cartridge.read('adventure2');
        return [3, 7, 300].map(id => engine.installDiary(id, bytes));
    });
    assert.deepEqual(installed, [true, true, true]);
    // Canceling a new diary also refreshes the picker after the API installs.
    await page.getByRole('button', { name: 'New diary', exact: true }).click();
    await page.getByRole('textbox', { name: 'Player name', exact: true }).waitFor();
    await page.getByRole('button', { name: 'Back', exact: true }).click();
    await picker(page);
    assert.equal(await page.evaluate(() => !!window.tcgAdventure.engine.cartridge.read('adventure1')), false);
    for (const id of [2, 3, 7, 300])
        assert.equal(await page.getByRole('button', { name: `Diary ${id}: MARK`, exact: true }).count(), 1);
    await page.screenshot({ path: path.join(output, 'diary-picker.png') });
    await page.getByRole('button', { name: 'New diary', exact: true }).click();
    await page.getByRole('textbox', { name: 'Player name', exact: true }).press('Escape');
    await picker(page);
    assert.equal(await page.evaluate(() => !!window.tcgAdventure.engine.cartridge.read('adventure1')), false);

    for (const id of [3, 7]) {
        completedTutorial(await continueDiary(page, id));
        await returnToPicker(page);
    }
    completedTutorial(await continueDiary(page, 300));
    const progress = await page.evaluate(async () => {
        const engine = window.tcgAdventure.engine, before = engine.view;
        const world = await fetch(new URL('world/world.json', location.href)).then(response => response.json());
        const map = world.maps[before.map];
        const blocked = new Set(before.npcs.filter(npc => npc.visible).map(npc => `${npc.x},${npc.y}`));
        const next = [[2, 0, 2], [1, 2, 0], [3, -2, 0], [0, 0, -2]].find(([, dx, dy]) => {
            const x = before.x + dx, y = before.y + dy;
            return x >= 2 && y >= 2 && x < map.width - 2 && y < map.height - 2 &&
                !blocked.has(`${x},${y}`) && !(map.permissions[y >> 1][x >> 1] & 128);
        });
        if (!next) throw new Error('No free source-grid neighbor for progression check');
        await engine.action(next[0]);
        const after = engine.view;
        if (after.x === before.x && after.y === before.y) throw new Error('Source movement did not advance');
        await engine.action(6);
        return { view: after, save: engine.cartridge.files.adventure300 };
    });
    await page.screenshot({ path: path.join(output, 'diary-300.png') });
    await returnToPicker(page);
    progress.save = await page.evaluate(() => window.tcgAdventure.engine.cartridge.files.adventure300);
    assert.equal(await page.evaluate(() => window.tcgAdventure.engine.cartridge.files.adventure2), seedTwo,
        'Opening and saving other IDs preserves Diary 2');

    await page.reload();
    await startToPicker(page, 'reload');
    assert.equal(await page.evaluate(() => window.tcgAdventure.engine.cartridge.files.adventure300), progress.save);
    const reloaded = await continueDiary(page, 300); completedTutorial(reloaded);
    for (const key of ['map', 'x', 'y', 'totalCards', 'selectedDeck']) assert.equal(reloaded.view[key], progress.view[key], `Diary300 preserves ${key}`);
    await returnToPicker(page);
    assert.equal(await page.getByRole('button', { name: 'Diary 1', exact: true }).count(), 1);

    // Existing Diary 1 must survive first installation of the shipped Diary 2.
    const existing = await browser.newContext({ viewport: { width: 1280, height: 900 }, storageState: {
        cookies: [], origins: [{ origin: url.origin, localStorage: [{ name: storageKey,
            value: JSON.stringify({ version: 1, files: { adventure1: progress.save } }) }] }],
    } });
    const existingPage = await existing.newPage(); watch(existingPage);
    await existingPage.goto(url.href);
    await startToPicker(existingPage, 'existing-diary');
    assert.equal(await existingPage.evaluate(() => window.tcgAdventure.engine.cartridge.files.adventure1), progress.save);
    const preserved = await continueDiary(existingPage, 1); completedTutorial(preserved);
    for (const key of ['map', 'x', 'y', 'totalCards', 'selectedDeck']) assert.equal(preserved.view[key], progress.view[key]);
    await returnToPicker(existingPage);
    assert.equal(await existingPage.getByRole('button', { name: 'Diary 2: MARK', exact: true }).count(), 1);
    await existing.close();

    const mobile = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 1 });
    const mobilePage = await mobile.newPage(); watch(mobilePage);
    await mobilePage.goto(url.href); await startToPicker(mobilePage, 'mobile'); await mobile.close();
    assert.deepEqual(errors, []);
    fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify({ passed: true, url: url.href,
        diaries: [1, 2, 3, 7, 300], starterCards: 90, tutorialSkipped: true, newDiaryBack: true,
        newDiaryEscape: true, menuPicker: true, progressionReload: true, existingDiaryPreserved: true,
        startPositions, errors }, null, 2));
    console.log('DIARIES BROWSER PASS: seed90/no tutorial, IDs3/7/300, picker/back/Escape, save/reload, existing Diary1 preserved, desktop/mobile Start centered; no page errors');
} catch (error) {
    if (page && !page.isClosed()) await page.screenshot({ path: path.join(output, 'failure.png') }).catch(() => {});
    throw error;
} finally { await browser?.close(); }
