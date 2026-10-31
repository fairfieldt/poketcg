// Held movement uses real browser keyboard, gamepad and pointer/touch input.
// Serve only the current input JS over the live build; no rebuild is required.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(import.meta.url);
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const url = new URL(process.env.ADVENTURE_URL || 'http://ms.tdf.sh:8091/');
url.searchParams.delete('fast');
const output = path.join(web, 'build/movement-browser');fs.mkdirSync(output, { recursive: true });
const errors = [], results = [];
let browser, page;
try {
    browser = await chromium.launch({ channel: 'chrome', headless: true,
        args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
    const context = await browser.newContext({ viewport: { width: 1100, height: 850 }, hasTouch: true });
    page = await context.newPage();page.setDefaultTimeout(20000);
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', event => { if (event.type() === 'error') errors.push(event.text()); });
    await page.route('**/js/adventure.js', route => route.fulfill({ path: path.join(web, 'src/js/adventure.js'), contentType: 'text/javascript' }));
    await page.route('**/js/adventure_engine.js', route => route.fulfill({ path: path.join(web, 'src/js/adventure_engine.js'), contentType: 'text/javascript' }));
    await page.addInitScript(() => Object.defineProperty(navigator, 'getGamepads', { value: () => window.movementPad ? [window.movementPad] : [] }));
    await page.goto(url.href);
    await page.waitForSelector('.adventure-title-start:not([disabled])', { timeout: 60000 });
    await page.getByRole('button', { name: 'Start', exact: true }).click();
    await page.getByRole('button', { name: 'Start', exact: true }).click();
    await page.getByRole('button', { name: /^Diary 2(?::|$)/ }).click();
    await page.getByRole('button', { name: 'Continue', exact: true }).click();
    await page.waitForFunction(() => window.tcgAdventure.active && !window.tcgAdventure.engine.busy && !window.tcgAdventure.modal, { timeout: 60000 });
    const installFixture = async () => {
        const app = window.tcgAdventure, engine = app.engine;
        const world = await fetch('world/world.json').then(response => response.json());
        const f = window.movementFixture = { app, engine, world, frames: [], actions: [], enabled: false, releaseAt: Infinity };
        const frame = engine.handlers.frame;
        engine.handlers.frame = async view => {
            f.pendingFrame = view;f.presented = false;await frame(view);f.pendingFrame = null;
        };
        const draw = app.world.draw.bind(app.world);
        app.world.draw = (view, time) => {
            const rendered = draw(view, time);
            if (rendered && f.enabled && view === f.pendingFrame && !f.presented) {
                f.presented = true;f.frames.push({ time: performance.now(), x: view.presentation.player.x,
                    z: view.presentation.player.z, gait: view.presentation.player.gait, lean: view.presentation.player.lean });
            }
            return rendered;
        };
        const action = engine.action.bind(engine);
        engine.action = async (kind, arg = 0) => {
            const entry = { kind, arg, begin: performance.now() };if (f.enabled) f.actions.push(entry);
            try { return await action(kind, arg); } finally { entry.end = performance.now(); }
        };
        f.begin = () => { f.frames = [];f.actions = [];f.releaseAt = Infinity;f.enabled = true; };
        f.release = () => { f.releaseAt = performance.now(); };
        f.setup = async (kind = 'lane') => {
            f.enabled = false;
            const view = engine.view, map = world.maps[view.map];
            const npcs = new Set(view.npcs.filter(npc => npc.visible).map(npc => `${npc.x},${npc.y}`));
            const dirs = [[0, 0, -2], [1, 2, 0], [2, 0, 2], [3, -2, 0]];
            const free = (x, y) => x >= 2 && y >= 2 && x < map.width - 2 && y < map.height - 2 &&
                !(map.permissions[y >> 1][x >> 1] & 128) && !npcs.has(`${x},${y}`) &&
                !map.warps.some(warp => Math.abs(warp.x - x) < 2 && Math.abs(warp.y - y) < 2);
            const queue = [[view.x, view.y, []]], seen = new Set([`${view.x},${view.y}`]);
            let chosen;
            for (let at = 0; at < queue.length && !chosen; at++) {
                const [x, y, route] = queue[at];
                for (const [direction, dx, dy] of dirs) {
                    const steps = kind === 'wall' ? 1 : kind === 'turn' ? 3 : 5;
                    if (!Array.from({ length: steps }, (_, i) => free(x + dx * (i + 1), y + dy * (i + 1))).every(Boolean)) continue;
                    if (kind === 'wall') {
                        const bx = x + dx * 2, by = y + dy * 2;
                        if (bx < 0 || by < 0 || bx >= map.width || by >= map.height || !(map.permissions[by >> 1][bx >> 1] & 128)) continue;
                    }
                    const turn = kind === 'turn' ? dirs.find(([, tx, ty]) => dx * tx + dy * ty === 0 &&
                        [1, 2].every(along => [1, 2].every(across => free(x + dx * along + tx * across, y + dy * along + ty * across)))) : null;
                    if (kind === 'turn' && !turn) continue;
                    chosen = { x, y, direction, dx, dy, route, turn };break;
                }
                for (const [direction, dx, dy] of dirs) {
                    const nx = x + dx, ny = y + dy, key = `${nx},${ny}`;
                    if (seen.has(key) || !free(nx, ny)) continue;
                    seen.add(key);queue.push([nx, ny, [...route, direction]]);
                }
            }
            if (!chosen) throw new Error(`No source-grid ${kind} fixture`);
            for (const direction of chosen.route) await engine.action(direction);
            await engine.stopMovement();f.start = chosen;f.begin();return chosen;
        };
        f.progress = () => {
            const pose = engine.view.presentation.player, s = f.start;
            return (pose.x - s.x) * s.dx / 2 + (pose.z - s.y) * s.dy / 2;
        };
    };
    await page.evaluate(installFixture);
    const setup = kind => page.evaluate(kind => window.movementFixture.setup(kind), kind);
    const keys = ['ArrowUp', 'ArrowRight', 'ArrowDown', 'ArrowLeft'];
    const labels = ['↑', '→', '↓', '←'];
    const waitDistance = distance => page.waitForFunction(distance => window.movementFixture.progress() >= distance - .0001, distance);
    const release = () => page.evaluate(() => window.movementFixture.release());
    const settled = () => page.waitForFunction(() => {
        const e = window.tcgAdventure.engine, p = e.view.presentation.player;
        return !e.busy && Math.abs(p.gait) < .0001 && Math.abs(p.lean) < .0001;
    });
    async function verify(label, running = false) {
        await settled();await page.waitForTimeout(120);
        const data = await page.evaluate(() => {
            const f = window.movementFixture;f.enabled = false;
            return { frames: f.frames, actions: f.actions, releaseAt: f.releaseAt, start: f.start, view: f.engine.view };
        });
        const held = data.frames.filter(frame => frame.time < data.releaseAt);
        assert(held.length >= (running ? 12 : 24), `${label}: at least three tiles were presented`);
        for (let i = 1; i < held.length; i++)
            assert(Math.hypot(held[i].x - held[i - 1].x, held[i].z - held[i - 1].z) > .001, `${label}: no idle presentation frame between tiles`);
        const moves = data.actions.filter(action => action.kind < 4 && action.begin < data.releaseAt);
        for (let i = 1; i < moves.length; i++) assert(moves[i].begin - moves[i - 1].end < 8, `${label}: successful tiles chain without RAF delay`);
        assert(moves.every(action => action.arg === +running), `${label}: running modifier preserved`);
        assert.equal(data.actions.filter(action => action.kind === 12).length, 1, `${label}: one terminal idle pose`);
        assert.equal(data.view.x % 2, 0);assert.equal(data.view.y % 2, 0);
        const position = [data.view.x, data.view.y];await page.waitForTimeout(180);
        assert.deepEqual(await page.evaluate(() => { const v = window.tcgAdventure.engine.view;return [v.x, v.y]; }), position, `${label}: release stops at a legal tile`);
        results.push({ label, frames: held.length, tiles: moves.length, stoppedAt: position,
            maxChainGapMs: Math.max(0, ...moves.slice(1).map((action, i) => action.begin - moves[i].end)) });
    }

    let start = await setup('lane');
    await page.keyboard.down(keys[start.direction]);await waitDistance(6);await release();await page.keyboard.up(keys[start.direction]);
    await verify('keyboard');

    start = await setup('lane');
    await page.keyboard.down('Shift');await page.keyboard.down(keys[start.direction]);await waitDistance(6);await release();
    await page.keyboard.up(keys[start.direction]);await page.keyboard.up('Shift');await verify('keyboard-run', true);

    start = await setup('turn');
    await page.keyboard.down(keys[start.direction]);await waitDistance(2);
    await page.keyboard.down(keys[start.turn[0]]);await page.keyboard.up(keys[start.direction]);
    await page.waitForFunction(() => {
        const f = window.movementFixture, p = f.engine.view.presentation.player, [, dx, dy] = f.start.turn;
        return (p.x - f.start.x) * dx / 2 + (p.z - f.start.y) * dy / 2 >= 4 - .0001;
    });
    await release();await page.keyboard.up(keys[start.turn[0]]);await verify('held-turn');
    const turned = await page.evaluate(() => window.movementFixture.actions.filter(action => action.kind < 4).map(action => action.kind));
    assert(turned.includes(start.direction) && turned.includes(start.turn[0]), 'A turn pressed while busy is retained');

    start = await setup('lane');
    await page.evaluate(direction => {
        const buttons = Array.from({ length: 16 }, () => ({ pressed: false }));buttons[[12, 15, 13, 14][direction]].pressed = true;
        window.movementPad = { buttons, axes: [0, 0] };
    }, start.direction);
    await waitDistance(6);await release();await page.evaluate(() => { window.movementPad = null; });await verify('gamepad');

    start = await setup('lane');
    const arrow = page.getByRole('button', { name: labels[start.direction], exact: true });
    let box = await arrow.boundingBox();await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);await page.mouse.down();
    await waitDistance(6);await release();await page.mouse.up();await verify('pointer');

    start = await setup('lane');
    box = await page.getByRole('button', { name: labels[start.direction], exact: true }).boundingBox();
    const runBox = await page.getByRole('button', { name: 'B', exact: true }).boundingBox();
    const touch = await context.newCDPSession(page);
    await touch.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [
        { x: runBox.x + runBox.width / 2, y: runBox.y + runBox.height / 2, id: 1 },
        { x: box.x + box.width / 2, y: box.y + box.height / 2, id: 2 },
    ] });
    await waitDistance(6);await release();await touch.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await verify('touch-run', true);await touch.detach();

    start = await setup('wall');
    await page.keyboard.down(keys[start.direction]);await waitDistance(2);await page.waitForTimeout(350);
    await page.keyboard.up(keys[start.direction]);await settled();
    const wall = await page.evaluate(() => {
        const f = window.movementFixture;f.enabled = false;return { distance: f.progress(), stops: f.actions.filter(action => action.kind === 12).length };
    });
    assert.equal(wall.distance, 2);assert.equal(wall.stops, 1);results.push({ label: 'wall', ...wall });

    start = await setup('lane');
    await page.keyboard.down(keys[start.direction]);await waitDistance(1);
    await page.getByRole('button', { name: 'Menu', exact: true }).click();
    await page.getByRole('heading', { name: 'Menu', exact: true }).waitFor();
    const pausedAt = await page.evaluate(() => { const v = window.tcgAdventure.engine.view;return [v.x, v.y]; });
    await page.waitForTimeout(250);
    assert.deepEqual(await page.evaluate(() => { const v = window.tcgAdventure.engine.view;return [v.x, v.y]; }), pausedAt);
    await page.getByRole('button', { name: 'Return', exact: true }).click();await settled();await page.waitForTimeout(250);
    assert.deepEqual(await page.evaluate(() => { const v = window.tcgAdventure.engine.view;return [v.x, v.y]; }), pausedAt, 'Held direction is suppressed across a modal');
    await page.keyboard.up(keys[start.direction]);results.push({ label: 'modal', stoppedAt: pausedAt });

    start = await setup('lane');
    await page.keyboard.down(keys[start.direction]);await waitDistance(1);
    await page.evaluate(() => dispatchEvent(new Event('blur')));await settled();
    const blurAt = await page.evaluate(() => { const v = window.tcgAdventure.engine.view;return [v.x, v.y]; });
    await page.waitForTimeout(250);
    assert.deepEqual(await page.evaluate(() => { const v = window.tcgAdventure.engine.view;return [v.x, v.y]; }), blurAt);
    await page.keyboard.up(keys[start.direction]);await page.evaluate(() => dispatchEvent(new Event('focus')));
    results.push({ label: 'blur', stoppedAt: blurAt });

    const fastURL = new URL(url);fastURL.searchParams.set('fast', '1');
    await page.goto(fastURL.href);
    await page.waitForSelector('.adventure-title-start:not([disabled])', { timeout: 60000 });
    await page.getByRole('button', { name: 'Start', exact: true }).click();
    await page.getByRole('button', { name: 'Start', exact: true }).click();
    await page.getByRole('button', { name: /^Diary 2(?::|$)/ }).click();
    await page.getByRole('button', { name: 'Continue', exact: true }).click();
    await page.waitForFunction(() => window.tcgAdventure.active && !window.tcgAdventure.engine.busy && !window.tcgAdventure.modal);
    await page.evaluate(installFixture);start = await setup('lane');
    await page.keyboard.down(keys[start.direction]);await page.keyboard.up(keys[start.direction]);
    await page.waitForFunction(() => window.movementFixture.actions.some(action => action.kind === 12 && action.end));
    const fastTap = await page.evaluate(() => {
        const f = window.movementFixture;f.enabled = false;return { distance: f.progress(), moves: f.actions.filter(action => action.kind < 4).length };
    });
    assert(fastTap.distance >= 2 && fastTap.distance <= 4, 'Fast-mode release interrupts the held chain');
    assert(fastTap.moves <= 2);results.push({ label: 'fast-tap', ...fastTap });
    assert.deepEqual(errors, []);
    fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify({ passed: true, url: url.href, results, errors }, null, 2));
    console.log('MOVEMENT BROWSER PASS: held keyboard/gamepad/pointer/touch, continuous pose/action boundaries, run modifiers, legal release, wall/modal/blur suppression');
} catch (error) {
    if (page && !page.isClosed()) {
        await page.screenshot({ path: path.join(output, 'failure.png') }).catch(() => {});
        const fixture = await page.evaluate(() => {
            const f = window.movementFixture;return f ? { frames: f.frames, actions: f.actions, start: f.start, releaseAt: f.releaseAt, view: f.engine.view } : null;
        }).catch(() => null);
        fs.writeFileSync(path.join(output, 'failure.json'), JSON.stringify(fixture, null, 2));
    }
    throw error;
} finally { await browser?.close(); }
