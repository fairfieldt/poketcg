import { Engine, C, T, TCG_PLAYER } from './engine.js';
import { AdventureEngine, Cartridge } from './adventure_engine.js';
import { World3D } from './world3d.js';
import { DuelUI, keyOf, K } from './duel_ui.js';
import { el, clear, cardPage, richText, plainText } from './gfx.js';
import { exportFile, importFile } from './save_store.js';
import { AdventureAudio } from './adventure_audio.js';

const fast = new URLSearchParams(location.search).has('fast');
const app = {
	root: document.getElementById('app'),
	world: null,
	engine: null,
	duel: null,
	modal: null,
	view: null,
	profile: 1,
	active: false,
	mode: 'title',
};
window.tcgAdventure = app;
const translate = (text) =>
	String(text || '')
		.replaceAll('Save text to Controller Pak', 'Download text')
		.replaceAll('Controller Pak', 'File')
		.replaceAll('Controller 1', 'Download')
		.replaceAll('Controller 2', 'Download');
function scale() {
	/* Match the duel table's crowded layout, with room for its action bar and
	 * status line. Small screens retain readable text and scrolling. */
	const u = Math.max(1, Math.min((innerWidth - 12) / 240, (innerHeight - 48) / 304));
	document.documentElement.style.setProperty('--u', String(u));
}
function button(text, fn, cls = '') {
	const b = el('button', 'btn ' + cls, text);
	b.type = 'button';
	b.addEventListener('click', fn);
	return b;
}
function closeModal() {
	if (!app.modal) return;
	app.modal.cleanup?.();
	app.modal.element.remove();
	app.modal = null;
}
function modal(title, cls = '') {
	clearMovementInput();
	closeModal();
	const element = el('section', 'adventure-dialog ' + cls);
	element.setAttribute('role', 'dialog');
	if (title) element.appendChild(el('h2', '', translate(title)));
	app.overlay.appendChild(element);
	const state = { element, cleanup: null, key: null };
	app.modal = state;
	return state;
}
function choose(title, items, initial = 0, cancel = true) {
	return new Promise((resolve) => {
		const m = modal(title, 'adventure-menu');
		const list = el('div', 'adventure-options');
		m.element.appendChild(list);
		let current = Math.max(0, Math.min(items.length - 1, initial | 0));
		let done = false;
		const finish = (value) => {
			if (done) return;
			done = true;
			app.audio?.event(1, value < 0 ? C.SFX_CANCEL : C.SFX_CONFIRM);
			closeModal();
			resolve(value);
		};
		const controls = items.map((item, i) => button(translate(item), () => finish(i)));
		controls.forEach((b) => list.appendChild(b));
		const select = (n) => {
			if (n !== current) app.audio?.event(1, C.SFX_CURSOR);
			current = (n + controls.length) % controls.length;
			controls.forEach((b, i) => b.classList.toggle('cur', i === current));
			controls[current]?.scrollIntoView({ block: 'nearest' });
		};
		if (cancel) m.element.appendChild(button('Back', () => finish(-1), 'small adventure-back'));
		m.key = (k) => {
			if (k === K.UP || k === K.LEFT) select(current - 1);
			if (k === K.DOWN || k === K.RIGHT) select(current + 1);
			if (k === K.A) finish(current);
			if (k === K.B && cancel) finish(-1);
		};
		select(current);
	});
}
async function platformMenu(title, items, initial, cancel) {
	if (title === 'Save text to Controller Pak')
		return choose('Download report', ['Download', 'Cancel'], 0, false).then((v) => (v === 0 ? 0 : 2));
	if (title === 'Menu' && items.includes('Diary') && items.at(-1) === 'Return') {
		const selected = await choose(title, [...items, 'Choose diary'], initial, cancel);
		if (selected === items.length) {
			app.chooseDiaryNext = true;
			return items.length - 1;
		}
		return selected;
	}
	return choose(title, items, initial, cancel);
}
function message(title, text) {
	return new Promise((resolve) => {
		const m = modal(title, 'adventure-message'),
			body = el('div', 'adventure-text');
		m.element.appendChild(body);
		const lines = String(text || '')
			.split('\n')
			.flatMap((line) => {
				const out = [];
				while (line.length > 58) {
					let at = line.lastIndexOf(' ', 58);
					if (at < 1) at = 58;
					out.push(line.slice(0, at));
					line = line.slice(at).trimStart();
				}
				out.push(line);
				return out;
			});
		const pages = [];
		for (let i = 0; i < lines.length; i += 4) pages.push(lines.slice(i, i + 4).join('\n'));
		if (!pages.length) pages.push('');
		let page = 0,
			shown = 0,
			tokens = [],
			timer;
		const paint = () => {
			clear(body).appendChild(richText(tokens.slice(0, shown).join('')));
		};
		const reveal = () => {
			const rates = [1, 2, 3, 6, 999];
			shown = Math.min(tokens.length, shown + (fast ? 999 : rates[app.view?.textSpeed ?? 2]));
			paint();
		};
		const begin = () => {
			tokens = pages[page].match(/<[^>]+>|[\s\S]/g) || [];
			shown = 0;
			reveal();
		};
		const finish = () => {
			if (shown < tokens.length) {
				shown = tokens.length;
				paint();
				return;
			}
			if (page + 1 < pages.length) {
				page++;
				begin();
				return;
			}
			closeModal();
			resolve(0);
		};
		m.element.appendChild(button('Continue', finish, 'primary'));
		m.key = (k) => {
			if (k === K.A || k === K.B) finish();
		};
		timer = setInterval(reveal, 33);
		m.cleanup = () => clearInterval(timer);
		begin();
	});
}

function card(id) {
	return new Promise((resolve) => {
		const m = modal(app.duel.card(id)?.name || 'Card', 'adventure-card');
		m.element.appendChild(cardPage(app.duel.card(id)));
		const finish = () => {
			closeModal();
			resolve(0);
		};
		m.element.appendChild(button('Back', finish));
		m.key = (k) => {
			if (k === K.A || k === K.B) finish();
		};
	});
}
const videoFrame = () => fast ? Promise.resolve() : new Promise((resolve) => setTimeout(resolve, 1000 / 30));
function transitionLayer() {
	const layer = el('div', '');
	layer.style.cssText = 'position:absolute;inset:0;pointer-events:none;z-index:5';
	app.root.appendChild(layer);
	return layer;
}
async function warpFade(layer, revealing, map) {
	layer.style.background = map ? 'rgb(26,18,14)' : 'rgb(5,6,14)';
	for (let step = 1; step <= 6; step++) {
		layer.style.opacity = String((revealing ? 6 - step : step) / 6);
		await videoFrame();
	}
}
async function duelShutters(entering) {
	const layer = transitionLayer();
	const bars = [el('div', ''), el('div', '')];
	bars.forEach((bar, index) => {
		bar.style.cssText = `position:absolute;left:0;right:0;${index ? 'bottom' : 'top'}:0;background:#000`;
		layer.appendChild(bar);
	});
	for (let step = 0; step < 16; step++) {
		const progress = entering ? Math.min(step, 11) : Math.max(0, step - 4);
		const inverse = 1 - progress / 11;
		const extent = entering ? 1 - inverse * inverse : inverse * inverse;
		bars.forEach((bar) => { bar.style.height = `${extent * 50}%`; });
		await videoFrame();
	}
	return layer;
}
async function frame(view) {
	const oldMap = app.view?.map;
	const introArrival = oldMap === 0 && view.map === C.MASON_LABORATORY &&
		app.engine.x.web_adv_event(C.EVENT_MASON_LAB_STATE) < C.MASON_LAB_RECEIVED_STARTER_DECK;
	const fade = oldMap !== undefined && oldMap !== view.map && !introArrival && !fast;
	const layer = fade ? transitionLayer() : null;
	try {
		if (layer) await warpFade(layer, false, oldMap);
		await app.world.update(view);
		app.view = view;
		const selected = view.overworldSelection || 1;
		app.location.textContent =
			view.map === 0 ? app.duel.text(T.OverworldMapMasonLaboratoryText + selected - 1) : '';
		if (selected === 2 && !app.engine.x.web_adv_event(C.EVENT_ISHIHARAS_HOUSE_MENTIONED))
			app.location.textContent = app.duel.text(T.OverworldMapMysteryHouseText);
		app.world.draw(view);
		if (layer) await warpFade(layer, true, view.map);
	} finally { layer?.remove(); }
	if (!fast) await new Promise((r) => setTimeout(r, 33));
}
const padStates = new Map();
let lastClock = 0;
let keyboardBHeld = false;
let shiftHeld = false, inputFocused = true, actionInFlight = false, padFocusReset = false;
let pendingAction = null, movementNeedsIdle = false, blockedMovement = false, movementRetryAt = 0;
const heldDirections = new Map(), pointerRunning = new Set(), padNeedsNeutral = new Set();
const directions = { [K.UP]: 0, [K.RIGHT]: 1, [K.DOWN]: 2, [K.LEFT]: 3 };
function runningInput() { app.running = shiftHeld || keyboardBHeld || pointerRunning.size > 0; }
function worldInput() { return inputFocused && app.active && app.mode === 'world' && !app.modal; }
function clearMovementInput() {
	heldDirections.clear();pointerRunning.clear();pendingAction = null;movementRetryAt = 0;
	runningInput();
}
function holdDirection(source, key) {
	if (!worldInput() || heldDirections.get(source) === key) return;
	heldDirections.delete(source);heldDirections.set(source, key);movementRetryAt = 0;
}
function releaseDirection(source) {
	if (heldDirections.delete(source)) movementRetryAt = 0;
}
function pumpActions() {
	if (actionInFlight || !app.engine || app.engine.busy) return;
	if (!app.active || app.mode !== 'world' || app.modal) return;
	const held = inputFocused ? [...heldDirections.values()].at(-1) : null;
	if (movementNeedsIdle && (pendingAction || !held || blockedMovement)) {
		movementNeedsIdle = false;blockedMovement = false;actionInFlight = true;
		app.engine.stopMovement().catch(showError).finally(() => { actionInFlight = false;pumpActions(); });
		return;
	}
	if (!inputFocused) return;
	const command = pendingAction || (held && performance.now() >= movementRetryAt ?
		{ action: directions[held], arg: app.running || app.padRunning ? 1 : 0 } : null);
	if (!command) return;
	pendingAction = null;actionInFlight = true;
	const before = app.engine.view, began = performance.now();
	app.engine.action(command.action, command.arg).then(async result => {
		if (command.action < 4) {
			const after = app.engine.view;
			const moved = after.map !== before.map || after.x !== before.x || after.y !== before.y;
			if (before.map && moved) movementNeedsIdle = true;
			blockedMovement = !!before.map && !moved;
			// Successful room steps chain in this promise, without a RAF or key
			// repeat delay. Blocked retries yield so releases and menus can run.
			movementRetryAt = !before.map ? began + 180 : moved ? 0 : performance.now() + 100;
			// Fast previews omit frame pacing; yield once so keyup/touchend can
			// interrupt a held chain instead of starving behind resolved promises.
			if (fast && moved) await new Promise(resolve => requestAnimationFrame(resolve));
		}
		if (result === 1) { movementNeedsIdle = false;clearMovementInput();return showTitle(false); }
		if (app.chooseDiaryNext) {
			app.chooseDiaryNext = false;
			await app.engine.action(6);app.active = false;movementNeedsIdle = false;clearMovementInput();
			return diaryMenu();
		}
	}).catch(showError).finally(() => { actionInFlight = false;pumpActions(); });
}
document.addEventListener('keydown', (e) => {
	if (e.isTrusted && keyOf(e) === K.B) { keyboardBHeld = true;runningInput(); }
});
document.addEventListener('keyup', (e) => {
	if (e.isTrusted && keyOf(e) === K.B) { keyboardBHeld = false;runningInput(); }
	releaseDirection(`keyboard:${e.code || e.key}`);pumpActions();
});
addEventListener('blur', () => {
	inputFocused = false;keyboardBHeld = false;shiftHeld = false;app.padRunning = false;padFocusReset = true;
	for (const index of padStates.keys()) padNeedsNeutral.add(index);
	clearMovementInput();pumpActions();
});
addEventListener('focus', () => { inputFocused = true; });
let gamepadsUnavailable = false;
function gamepads(time) {
	app.padRunning = false;
	let pads = [];
	if (!gamepadsUnavailable) {
		try {
			pads = navigator.getGamepads?.() || [];
		} catch {
			gamepadsUnavailable = true;
		}
	}
	let duelB = false;
	const connected = new Set();
	for (let index = 0; index < pads.length; index++) {
		const pad = pads[index];
		if (!pad) continue;
		connected.add(index);
		if (app.mode === 'duel' && app.ui?.mode === 'hotseat') {
			const expected = app.ui.req?.duelist === 0xc3 ? 1 : 0;
			if (pads[1] && index !== expected) continue;
		} else if (index) continue;
		const active = [
			pad.buttons[12]?.pressed || pad.axes[1] < -0.5,
			pad.buttons[15]?.pressed || pad.axes[0] > 0.5,
			pad.buttons[13]?.pressed || pad.axes[1] > 0.5,
			pad.buttons[14]?.pressed || pad.axes[0] < -0.5,
			pad.buttons[0]?.pressed,
			pad.buttons[1]?.pressed,
			pad.buttons[9]?.pressed,
		];
		if (app.mode === 'duel') duelB = duelB || !!active[5];
		const previous = padStates.get(index) || { active: [], repeat: [] };
		if (padFocusReset && active.slice(0, 4).some(Boolean)) padNeedsNeutral.add(index);
		if (!active.slice(0, 4).some(Boolean)) padNeedsNeutral.delete(index);
		if (app.mode !== 'duel' && !app.modal) app.padRunning = !!active[5];
		for (let i = 0; i < active.length; i++)
			if (i < 4 && worldInput()) {
				const source = `gamepad:${index}:${i}`;
				if (!active[i]) releaseDirection(source);
				else if (!previous.active[i] && !padNeedsNeutral.has(index)) holdDirection(source, [K.UP, K.RIGHT, K.DOWN, K.LEFT][i]);
			} else if (active[i] && (!previous.active[i] || (i < 4 && time > (previous.repeat[i] || 0)))) {
				previous.repeat[i] = time + 180;
				const key = [K.UP, K.RIGHT, K.DOWN, K.LEFT, K.A, K.B, K.START][i];
				if (app.mode === 'duel')
					document.dispatchEvent(
						new KeyboardEvent('keydown', {
							key: {
								up: 'ArrowUp',
								right: 'ArrowRight',
								down: 'ArrowDown',
								left: 'ArrowLeft',
								a: 'z',
								b: 'x',
								start: 'c',
							}[key],
							bubbles: true,
						}),
					);
				else if (app.mode === 'title')
					document.dispatchEvent(
						new KeyboardEvent('keydown', {
							key: key === K.START ? 'c' : key === K.A ? 'z' : '',
							bubbles: true,
						}),
					);
				else dispatch(key);
			}
		previous.active = active;
		padStates.set(index, previous);
	}
	for (const index of padStates.keys()) if (!connected.has(index)) {
		for (let i = 0; i < 4; i++) releaseDirection(`gamepad:${index}:${i}`);
		padStates.delete(index);padNeedsNeutral.delete(index);
	}
	if (app.mode === 'duel' && app.ui) app.ui.setBHeld(duelB || keyboardBHeld);
	if (inputFocused) padFocusReset = false;
	pumpActions();
}
function animate(time) {
	if (app.active && app.engine && time - lastClock >= 1000) {
		app.engine.x.web_adv_tick();
		lastClock = time;
	}
	if (app.mode === 'portrait') app.world.drawPortrait(time);
	else if (app.view && app.mode !== 'duel') app.world.draw(app.view, time);
	gamepads(time);
	requestAnimationFrame(animate);
}

function runAction(action, arg = 0) {
	if (!worldInput()) return;
	if (action >= 4) { heldDirections.clear();movementRetryAt = 0; }
	pendingAction = { action, arg };pumpActions();
}
function showError(error) {
	console.error(error);
	app.active = false;
	message('Game stopped', error.message || String(error));
}
function dispatch(k) {
	if (app.mode === 'duel') return;
	if (app.modal?.key) {
		app.modal.key(k);
		return;
	}
	if (!app.active) return;
	if (k in directions) runAction(directions[k], app.running || app.padRunning ? 1 : 0);
	else if (k === K.A) runAction(4);
	else if (k === K.START) runAction(5);
}
function controls() {
	const pad = el('div', 'adventure-controls');
	for (const [label, k] of [
		['↑', K.UP],
		['←', K.LEFT],
		['↓', K.DOWN],
		['→', K.RIGHT],
		['A', K.A],
		['B', K.B],
		['Menu', K.START],
	]) {
		let pointerConsumed = false;
		const b = button(label, () => {
			if (pointerConsumed) { pointerConsumed = false;return; }
			dispatch(k);
		});
		b.setAttribute('aria-label', label === 'A' ? 'Talk or enter' : label);
		if (k in directions || k === K.B) {
			b.style.touchAction = 'none';
			b.addEventListener('pointerdown', e => {
				pointerConsumed = false;
				if (e.button !== 0 || !worldInput()) return;
				e.preventDefault();b.setPointerCapture(e.pointerId);pointerConsumed = true;
				if (k === K.B) { pointerRunning.add(e.pointerId);runningInput(); }
				else holdDirection(`pointer:${e.pointerId}`, k);
				pumpActions();
			});
			const release = e => {
				releaseDirection(`pointer:${e.pointerId}`);pointerRunning.delete(e.pointerId);runningInput();pumpActions();
			};
			b.addEventListener('pointerup', release);
			b.addEventListener('lostpointercapture', release);
			b.addEventListener('pointercancel', e => { pointerConsumed = false;release(e); });
		}
		pad.appendChild(b);
	}
	app.root.appendChild(pad);
}
async function start(profile, fresh) {
	app.profile = profile;
	let result;
	if (fresh) {
		const name = await playerName();
		if (name === null) return false;
		app.mode = 'world';
		app.active = true;
		result = await app.engine.start(profile, { fresh: true, name });
	} else {
		app.mode = 'world';
		app.active = true;
		result = await app.engine.start(profile);
	}
	if (result < 0) {
		app.active = false;
		await message(
			'Diary',
			result === -3 ? 'This diary could not be saved.' : 'This diary could not be read.',
		);
		return false;
	}
	await frame(app.engine.view);
	return true;
}
const diaryLabel = (diary) => `Diary ${diary.id}${diary.name ? `: ${diary.name}` : ''}`;
async function choosePeer({ activeId, diaries }) {
	const others = diaries.filter((diary) => diary.id !== activeId);
	if (!others.length) {
		await message('Other diary', 'Create another diary first.');
		return 0;
	}
	const selected = await choose('Choose other diary', others.map(diaryLabel), 0, true);
	return selected < 0 ? 0 : others[selected].id;
}
async function diaryMenu() {
	app.mode = 'world';
	for (;;) {
		const diaries = app.engine.diaries();
		if (!diaries.some((diary) => diary.id === 1)) diaries.unshift({ id: 1, name: '' });
		const actions = ['New diary', 'Duel modes', 'Export cartridge', 'Import cartridge'];
		const p = await choose(
			'Choose diary',
			[...diaries.map(diaryLabel), ...actions],
			0,
			false,
		);
		const action = p - diaries.length;
		if (action === 0) {
			if (await start(app.engine.nextDiaryId(), true)) return;
			continue;
		}
		if (action === 1) {
			location.href = 'duel.html';
			return;
		}
		if (action === 2) {
			await exportFile(app.engine.cartridge.export(), 'poketcg-cartridge.json');
			continue;
		}
		if (action === 3) {
			const bytes = await importFile({
				types: [{ description: 'Pokémon TCG cartridge', accept: { 'application/json': ['.json'] } }],
				accept: '.json,application/json',
			});
			if (bytes && !app.engine.importCartridge(bytes))
				await message('Import', 'The cartridge file could not be read.');
			continue;
		}
		const profile = diaries[p].id;
		if (!app.engine.profile(profile)) {
			if (await start(profile, true)) return;
			continue;
		}
		const option = await choose(
			`Diary ${profile}`,
			['Continue', 'Card Pop!', 'New game', 'Choose diary'],
			0,
			false,
		);
		if (option === 3) continue;
		if (option === 2) {
			if ((await choose('Replace this diary?', ['Keep diary', 'Start a new game'], 0, false)) !== 1) continue;
			if (await start(profile, true)) return;
			continue;
		}
		if (option === 1) {
			await app.engine.loadProfile(profile);
			await frame(app.engine.view);
			await app.engine.action(7);
			continue;
		}
		if (await start(profile, false)) return;
	}
}
async function playerName() {
	app.mode = 'portrait';
	await app.world.portrait();
	return new Promise((resolve) => {
		const m = modal('Your name', 'adventure-name');
		const input = el('input', 'name-input');
		input.maxLength = 6;
		input.autocomplete = 'off';
		input.setAttribute('aria-label', 'Player name');
		input.placeholder = 'MARK';
		m.element.appendChild(input);
		const finish = () => {
			const name =
				input.value
					.toUpperCase()
					.replace(/[^A-Z0-9 !?&+.\-]/g, '')
					.trim() || 'MARK';
			closeModal();
			resolve(name.slice(0, 6));
		};
		m.element.appendChild(button('Continue', finish, 'primary'));
		const back = () => { closeModal(); resolve(null); };
		m.element.appendChild(button('Back', back, 'small'));
		m.key = (k) => {
			if (k === K.A || k === K.START) finish();
			if (k === K.B) back();
		};
		input.addEventListener('keydown', (e) => {
			if (e.key === 'Escape') { e.preventDefault(); back(); }
			if (e.key === 'Enter') {
				e.preventDefault();
				finish();
			}
		});
		input.focus();
	});
}
async function playDuel(setup, save) {
	closeModal();
	const closing = await duelShutters(true);
	app.mode = 'duel';
	app.duelLayer.classList.remove('hidden');
	clear(app.duelLayer);
	const root = el('div', 'screen duel-screen');
	app.duelLayer.appendChild(root);
	const bar = el('div', 'duel-bar');
	bar.appendChild(el('span', 'duel-title', `${setup.duelists[0].name} vs. ${setup.duelists[1].name}`));
	const quit = button(
		'Save and quit',
		() => {
			if (setup.duelists[1].kind === 'ai') {
				location.reload();
			}
		},
		'small',
	);
	if (setup.duelists[1].kind === 'ai') bar.appendChild(quit);
	root.appendChild(bar);
	const board = el('div', '');
	root.appendChild(board);
	const ui = new DuelUI(board, app.duel, {
		mode: setup.duelists[1].kind === 'ai' ? 'ai' : 'hotseat',
		localSide: TCG_PLAYER,
		pacing: !fast,
		skipDelay: setup.skipDelayAllowed,
	});
	ui.names = setup.duelists.map((d) => d.name);
	app.ui = ui;
	const handlers = {
		onEvent: async (ev) => {
			if (ev.type === 7) app.audio.event(1, ev.arg[0]);
			if (ev.type === 8) app.audio.event(0, ev.arg[0]);
			await ui.onEvent(ev);
		},
		onRequest: (r) => ui.onRequest(r),
		onSave: (bytes) => {
			if (setup.duelists[1].kind === 'ai') app.engine.saveDuel(bytes);
		},
	};
	try {
		closing.remove();
		const result = await (save
			? app.duel.continueDuel(setup, save, handlers)
			: app.duel.run(setup, handlers));
		app.engine.setDuelRng([app.duel.w('wRNG1'), app.duel.w('wRNG2'), app.duel.w('wRNGCounter')]);
		return result.result;
	} finally {
		ui.destroy();
		app.ui = null;
		app.duelLayer.classList.add('hidden');
		app.mode = 'world';
		const opening = await duelShutters(false);
		opening.remove();
	}
}

async function showTitle(opening = true) {
	app.active = false;
	app.mode = 'title';
	app.view = null;
	app.location.textContent = '';
	closeModal();
	const title = el('div', 'adventure-title');
	const canvas = el('canvas', '');
	const artScale = Math.max(2, Math.min(8, Math.ceil(window.innerHeight * .92 / 144 * (window.devicePixelRatio || 1))));
	canvas.width = 160 * artScale;
	canvas.height = 144 * artScale;
	title.appendChild(canvas);
	const startButton = button('Start', () => advance(), 'adventure-title-start');
	startButton.disabled = true;
	title.appendChild(startButton);
	app.overlay.appendChild(title);
	const metadata = await fetch('intro/intro_assets.json').then((r) => r.json());
	const pictures = {};
	await Promise.all(
		metadata.assets.map(
			(a) =>
				new Promise((resolve, reject) => {
					const image = new Image();
					image.onload = () => {
						pictures[a.name] = image;
						resolve();
					};
					image.onerror = reject;
					image.src = `intro/${a.name}.png`;
				}),
		),
	);
	const ctx = canvas.getContext('2d');
	ctx.scale(artScale, artScale);
	ctx.imageSmoothingEnabled = true;
	let began = performance.now(),
		showOpening = opening,
		done = false;
	let lastScene = -1,
		idleLimit = 2043,
		idleSpawn = -1,
		idleNext = 0;
	let scattered = false,
		titleMusic = false;
	const idleStarted = Array(7).fill(-1);
	const idleLength = metadata.animations[25].reduce((total, frame) => total + Math.max(0, frame[0]), 0);
	const imageNames = ['charizard', 'scyther', 'aerodactyl', 'title'],
		orbNames = ['grass', 'fire', 'water', 'colorless', 'lightning', 'psychic', 'fighting'];
	function orb(id, x, y, time) {
		const frames = metadata.animations[id - 191];
		let visible = id !== 216;
		for (let i = 0, guard = 0; guard < 8192; guard++, i++) {
			if (i >= frames.length) break;
			const [duration, image, dx, dy] = frames[i];
			if (!duration) {
				i = -1;
				continue;
			}
			x = (x + dx) & 255;
			y = (y + dy) & 255;
			if (x === 152) visible = true;
			if (duration < 0 || time < duration) return image >= 0 && visible ? [x - 8, y - 16] : null;
			time -= duration;
		}
		return null;
	}
	function draw(now) {
		if (done) return;
		const elapsed = (now - began) * 0.06;
		let scene = 3,
			t = elapsed;
		if (showOpening) {
			if (t < 118) scene = 0;
			else if (t < 236) {
				scene = 1;
				t -= 118;
			} else if (t < 440) {
				scene = 2;
				t -= 236;
			} else {
				scene = 3;
				t -= 440;
				if (t > 249) {
					idleLimit = 1983;
					showOpening = false;
					began = now;
					t = 0;
				}
			}
		}
		if (scene !== lastScene) {
			lastScene = scene;
			if (scene < 3) app.audio.event(1, scene === 2 ? C.SFX_INTRO_ORB_SWOOP : C.SFX_INTRO_ORB);
			else if (showOpening) app.audio.event(1, C.SFX_INTRO_ORB_TITLE);
			else app.audio.event(0, C.MUSIC_TITLESCREEN);
		}
		if (showOpening && scene === 3) {
			if (t >= 128 && !scattered) {
				scattered = true;
				app.audio.event(1, C.SFX_INTRO_ORB_SCATTER);
			}
			if (t >= 189 && !titleMusic) {
				titleMusic = true;
				app.audio.event(0, C.MUSIC_TITLESCREEN);
			}
		}
		if (!showOpening && t >= idleLimit) {
			showOpening = true;
			began = now;
			lastScene = -1;
			scattered = titleMusic = false;
			idleStarted.fill(-1);
			idleSpawn = -1;
			idleNext = 0;
			app.audio.stop();
			requestAnimationFrame(draw);
			return;
		}
		ctx.fillStyle = '#e7e7c6';
		ctx.fillRect(0, 0, 160, 144);
		ctx.imageSmoothingEnabled = true;
		ctx.drawImage(pictures[imageNames[scene]], 0, 0);
		if (scene === 3) {
			ctx.imageSmoothingEnabled = false;
			ctx.drawImage(pictures.title, 0, 110, 160, 34, 0, 110, 160, 34);
			ctx.imageSmoothingEnabled = true;
		}
		if (showOpening) {
			let opacity = 0;
			if (scene < 3) {
				const end = scene === 2 ? 144 : 88;
				opacity = t < 44 ? 1 : t < 60 ? (60 - t) / 16 : t < end ? 0 : t < end + 16 ? (t - end) / 16 : 1;
			} else opacity = t < 112 ? 1 : t < 128 ? (128 - t) / 16 : 0;
			if (opacity) {
				ctx.fillStyle = `rgba(255,255,255,${opacity})`;
				ctx.fillRect(0, 0, 160, 144);
			}
			const stage = scene < 3 ? scene : t < 128 ? 3 : 4,
				local = scene < 3 ? t : t < 128 ? t : t - 128;
			for (let i = 0; i < 7; i++) {
				const pair = metadata.orb_coordinates[stage][i],
					p = orb(metadata.orb_animation_ids[stage][i], (pair[0] + 8) & 255, (pair[1] + 16) & 255, local);
				if (p) ctx.drawImage(pictures[`orb_${orbNames[i]}`], ...p);
			}
		} else {
			const spawn = Math.floor(t / 64);
			if (spawn !== idleSpawn) {
				idleSpawn = spawn;
				for (let i = 0; i < 7; i++) {
					const index = (idleNext + i) % 7;
					if (idleStarted[index] < 0 || t - idleStarted[index] >= idleLength) {
						idleStarted[index] = t;
						idleNext = (index + 1) % 7;
						break;
					}
				}
			}
			for (let i = 0; i < 7; i++)
				if (idleStarted[i] >= 0) {
					const position = orb(216, 248, 14, t - idleStarted[i]);
					if (position) ctx.drawImage(pictures[`orb_${orbNames[i]}`], ...position);
				}
			if (t >= 60 && (t - 60) % 63 < 37) {
				ctx.imageSmoothingEnabled = false;
				ctx.drawImage(pictures.press_start, 35, 94);
			}
		}
		requestAnimationFrame(draw);
	}
	async function advance() {
		await app.audio.unlock();
		if (showOpening) {
			showOpening = false;
			began = performance.now();
			idleLimit = 2043;
			app.audio.event(1, 0);
			app.audio.event(0, C.MUSIC_TITLESCREEN);
			return;
		}
		done = true;
		document.removeEventListener('keydown', onKey);
		title.remove();
		await diaryMenu();
	}
	function onKey(e) {
		if (e.target.tagName === 'BUTTON') return;
		if (keyOf(e) === K.A || keyOf(e) === K.START) {
			e.preventDefault();
			advance();
		}
	}
	document.addEventListener('keydown', onKey);
	startButton.disabled = false;
	requestAnimationFrame(draw);
}
async function boot() {
	document.body.classList.add('adventure');
	scale();
	addEventListener('resize', scale);
	clear(app.root);
	app.canvas = el('canvas', 'adventure-world');
	app.canvas.setAttribute('aria-label', 'Game world');
	app.overlay = el('div', 'adventure-overlay');
	app.duelLayer = el('section', 'adventure-duel hidden');
	app.location = el('div', 'adventure-location');
	app.root.append(app.canvas, app.location, app.overlay, app.duelLayer);
	controls();
	app.audio = new AdventureAudio();
	app.duel = await Engine.load();
	app.world = await new World3D(app.canvas, { baseURL: 'world/' }).load();
	app.engine = await AdventureEngine.load(
		app.duel.module,
		{
			frame,
			message,
			menu: platformMenu,
			card,
			duel: playDuel,
			peer: choosePeer,
			report: async (name, text) => {
				await exportFile(new TextEncoder().encode(text), name.trim() || 'tcg-report.txt');
				return 0;
			},
			sound: (kind, id) => {
				app.audio.event(kind, id).catch(console.warn);
			},
			audioWait: () => (fast ? Promise.resolve() : app.audio.wait()),
		},
		new Cartridge(),
	);
	if (!Object.hasOwn(app.engine.cartridge.files, 'adventure2')) {
		const response = await fetch('saves/diary2-after-tutorial.json');
		if (!response.ok) throw new Error('Diary 2 could not be loaded.');
		const diary = await response.json();
		if (diary.version !== 1 || diary.profile !== 2 || typeof diary.save !== 'string' ||
			!app.engine.installDiary(2, Uint8Array.from(atob(diary.save), (c) => c.charCodeAt(0))))
			throw new Error('Diary 2 could not be saved.');
	}
	requestAnimationFrame(animate);
	document.addEventListener('keydown', (e) => {
		if (app.mode === 'title' || app.mode === 'duel' || e.target.tagName === 'INPUT') return;
		if (e.key === 'Shift') {
			shiftHeld = true;runningInput();
			return;
		}
		shiftHeld = e.shiftKey;runningInput();
		const k = keyOf(e);
		if (k) {
			e.preventDefault();
			if (k in directions && worldInput()) {
				if (!e.repeat) holdDirection(`keyboard:${e.code || e.key}`, k);
				pumpActions();
			} else dispatch(k);
		}
	});
	document.addEventListener('keyup', (e) => {
		if (e.key === 'Shift') { shiftHeld = false;runningInput(); }
	});
	await showTitle(true);
}
boot().catch(showError);
