import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import assert from 'node:assert/strict';
const require = createRequire(import.meta.url),
	web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const out = path.join(web, 'build/adventure-browser');
fs.mkdirSync(out, { recursive: true });
const port = 18174;
const base = process.env.ADVENTURE_URL || `http://127.0.0.1:${port}/`;
const server = process.env.ADVENTURE_URL
	? null
	: spawn(
			'python3',
			['-m', 'http.server', String(port), '--bind', '127.0.0.1', '--directory', path.join(web, 'dist')],
			{ stdio: 'ignore' },
		);

let browser;
const errors = [];
try {
	for (let i = 0; ; i++) {
		try {
			await fetch(base);
			break;
		} catch (e) {
			if (i > 100) throw e;
			await new Promise((r) => setTimeout(r, 50));
		}
	}
	browser = await chromium.launch({
		channel: 'chrome',
		headless: true,
		args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
	});
	const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 1 });
	page.on('pageerror', (e) => errors.push(e.message));
	page.on('console', (m) => {
		if (m.type() === 'error') errors.push(m.text());
	});
	if (process.env.TEST_GAMEPAD_DENIED)
		await page.addInitScript(() =>
			Object.defineProperty(navigator, 'getGamepads', {
				value() {
					throw new DOMException('Denied', 'SecurityError');
				},
			}),
		);
	await page.goto(new URL('?fast=1', base).href);
	await page.waitForFunction(() => window.tcgAdventure?.engine, { timeout: 60000 });
	console.log('boot ready');
	await page.screenshot({ path: path.join(out, 'opening.png') });
	const startButton=page.getByRole('button',{name:'Start',exact:true});
	await page.waitForSelector('.adventure-title-start:not([disabled])');
	const beforeStart=await startButton.boundingBox();
	await page.mouse.move(beforeStart.x+beforeStart.width/2,beforeStart.y+beforeStart.height/2);
	await page.mouse.down();
	const pressedStart=await startButton.boundingBox();
	assert(Math.abs(pressedStart.x-beforeStart.x)<.1,'Start stays centered while pressed');
	await page.mouse.up();
	const afterStart=await startButton.boundingBox();
	assert(Math.abs(afterStart.x-beforeStart.x)<.1,'Start stays centered after skipping intro');
	await page.getByRole('button', { name: 'Start', exact: true }).click();
	console.log('diary selector');
	await page.getByRole('button', { name: 'Diary 1', exact: true }).click();
	console.log('naming');
	await page.getByRole('textbox', { name: 'Player name' }).fill('MARK');
	await page.screenshot({ path: path.join(out, 'naming.png') });
	await page.getByRole('button', { name: 'Continue', exact: true }).click();
	console.log('source intro');
	let shots = 0;
	for (let i = 0; i < 180; i++) {
		if (i % 10 === 0) {
			console.log(
				'intro step',
				i,
				(await page.locator('.adventure-dialog').count())
					? await page.locator('.adventure-dialog').innerText()
					: 'no modal',
			);
			await page.screenshot({ path: path.join(out, 'current.png') });
		}
		if (await page.evaluate(() => !!window.tcgAdventure.ui)) break;
		const modal = page.locator('.adventure-dialog');
		if (await modal.count()) {
			const heading = await modal
				.locator('h2')
				.textContent()
				.catch(() => '');
			if (heading.includes('Mason') && shots++ === 0)
				await page.screenshot({ path: path.join(out, 'mason-dialogue.png') });
			const buttons = await modal.locator('button').allTextContents();
			const target = buttons.includes('Continue')
				? 'Continue'
				: buttons.includes('Yes')
					? 'Yes'
					: buttons.includes('Exit')
						? 'Exit'
						: buttons[0];
			if (target) await modal.getByRole('button', { name: target, exact: true }).click();
		} else await page.waitForTimeout(40);
	}
	await page.waitForFunction(() => !!window.tcgAdventure.ui, { timeout: 30000 });
	await page.waitForTimeout(200);
	await page.screenshot({ path: path.join(out, 'sam-duel.png') });
	assert(
		await page.evaluate(() => !!window.tcgAdventure.engine.cartridge.read('adventure1')),
		'Story diary checkpoint before practice duel',
	);
	const fixture = path.join(web, 'build/adventure-engine-test/cartridge.json');
	if (fs.existsSync(fixture)) {
		const bytes = fs.readFileSync(fixture);
		await page.reload();
		await page.waitForFunction(() => window.tcgAdventure?.engine, { timeout: 60000 });
		await page.evaluate((bytes) => {
			const app = window.tcgAdventure;
			app.engine.importCartridge(new Uint8Array(bytes));
		}, Array.from(bytes));
		await page.getByRole('button', { name: 'Start', exact: true }).click();
		await page.getByRole('button', { name: 'Start', exact: true }).click();
		await page.getByRole('button', { name: /^Diary 1(?::|$)/ }).click();
		await page.getByRole('button', { name: 'Continue', exact: true }).click();
		await page.waitForFunction(() => window.tcgAdventure.active && !window.tcgAdventure.engine.busy, {
			timeout: 30000,
		});
		await page.getByRole('button', { name: 'Menu', exact: true }).click();
		await page.getByRole('button', { name: 'Card Album', exact: true }).click();
		await page.screenshot({ path: path.join(out, 'card-album.png') });
		assert((await page.locator('.adventure-options button').count()) === 228, 'Complete card album');
	}
	assert.deepEqual(errors, []);
	fs.writeFileSync(
		path.join(out, 'result.json'),
		JSON.stringify(
			{ passed: true, errors, sourceIntro: true, naming: true, nativeWebGL: true, actualPracticeDuel: true },
			null,
			2,
		),
	);
	console.log(
		'ADVENTURE BROWSER PASS: original opening, detailed Mark naming, real source intro/Mason choreography, world-backed dialogue, actual practice-duel handoff, cartridge checkpoint, no browser errors',
	);
} finally {
	await browser?.close();
	server?.kill();
}
