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
	const u = Math.max(1, Math.min((innerWidth - 12) / 240, (innerHeight - 40) / 262));
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
function platformMenu(title, items, initial, cancel) {
	if (title === 'Save text to Controller Pak')
		return choose('Download report', ['Download', 'Cancel'], 0, false).then((v) => (v === 0 ? 0 : 2));
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
async function frame(view) {
	await app.world.update(view);
	app.view = view;
	const selected = view.overworldSelection || 1;
	app.location.textContent =
		view.map === 0 ? app.duel.text(T.OverworldMapMasonLaboratoryText + selected - 1) : '';
	if (selected === 2 && !app.engine.x.web_adv_event(C.EVENT_ISHIHARAS_HOUSE_MENTIONED))
		app.location.textContent = app.duel.text(T.OverworldMapMysteryHouseText);
	if (!fast) await new Promise((r) => setTimeout(r, 33));
}
const padStates = new Map();
let lastClock = 0;
let keyboardBHeld = false;
document.addEventListener('keydown', (e) => {
	if (e.isTrusted && keyOf(e) === K.B) keyboardBHeld = true;
});
document.addEventListener('keyup', (e) => {
	if (e.isTrusted && keyOf(e) === K.B) keyboardBHeld = false;
});
addEventListener('blur', () => {
	keyboardBHeld = false;
});
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
	for (let index = 0; index < pads.length; index++) {
		const pad = pads[index];
		if (!pad) continue;
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
		for (let i = 0; i < active.length; i++)
			if (active[i] && (!previous.active[i] || (i < 4 && time > (previous.repeat[i] || 0)))) {
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
		if (app.mode !== 'duel' && !app.modal) app.padRunning = !!active[5];
		previous.active = active;
		padStates.set(index, previous);
	}
	if (app.mode === 'duel' && app.ui) app.ui.setBHeld(duelB || keyboardBHeld);
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
	if (!app.active || app.engine.busy || app.modal || app.mode === 'duel') return;
	app.engine
		.action(action, arg)
		.then((result) => {
			if (result === 1) showTitle(false);
		})
		.catch(showError);
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
	const directions = { [K.UP]: 0, [K.RIGHT]: 1, [K.DOWN]: 2, [K.LEFT]: 3 };
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
		const b = button(label, () => dispatch(k));
		b.setAttribute('aria-label', label === 'A' ? 'Talk or enter' : label);
		pad.appendChild(b);
	}
	app.root.appendChild(pad);
	for (const b of pad.querySelectorAll('button'))
		if (b.textContent === 'B') {
			b.addEventListener('pointerdown', () => {
				if (!app.modal) app.running = true;
			});
			b.addEventListener('pointerup', () => {
				app.running = false;
			});
			b.addEventListener('pointercancel', () => {
				app.running = false;
			});
		}
}
async function start(profile, fresh) {
	app.profile = profile;
	let result;
	if (fresh) {
		const name = await playerName();
		if (name === null) return;
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
			result === -2 ? 'Finish the saved duel in the other diary first.' : 'This diary could not be read.',
		);
		return diaryMenu();
	}
	await frame(app.engine.view);
}
async function diaryMenu() {
	app.mode = 'world';
	for (;;) {
		const p = await choose(
			'Choose diary',
			['Diary 1', 'Diary 2', 'Duel modes', 'Export cartridge', 'Import cartridge'],
			0,
			false,
		);
		if (p === 2) {
			location.href = 'duel.html';
			return;
		}
		if (p === 3) {
			await exportFile(app.engine.cartridge.export(), 'poketcg-cartridge.json');
			continue;
		}
		if (p === 4) {
			const bytes = await importFile({
				types: [{ description: 'Pokémon TCG cartridge', accept: { 'application/json': ['.json'] } }],
				accept: '.json,application/json',
			});
			if (bytes && !app.engine.importCartridge(bytes))
				await message('Import', 'The cartridge file could not be read.');
			continue;
		}
		const profile = p + 1;
		const owner = app.engine.cartridge.read('duelowner')?.[0];
		if ((owner === 1 || owner === 2) && owner !== profile && app.engine.cartridge.read('duel')) {
			await message('Saved duel', `Finish the saved duel in Diary ${owner} first.`);
			continue;
		}
		if (!app.engine.profile(profile)) {
			await start(profile, true);
			return;
		}
		const option = await choose(
			`Diary ${profile}`,
			['Continue', 'Card Pop!', 'New game', 'Other diary'],
			0,
			false,
		);
		if (option === 3) continue;
		if (option === 2) {
			if ((await choose('Replace this diary?', ['Keep diary', 'Start a new game'], 0, false)) !== 1) continue;
			await start(profile, true);
			return;
		}
		if (option === 1) {
			await app.engine.call('web_adv_load_only', profile);
			await frame(app.engine.view);
			await app.engine.action(7);
			continue;
		}
		await start(profile, false);
		return;
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
		m.key = (k) => {
			if (k === K.A || k === K.START) finish();
		};
		input.addEventListener('keydown', (e) => {
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
			if (setup.duelists[1].kind === 'ai') app.engine.cartridge.commit({ duel: bytes });
		},
	};
	try {
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
	canvas.width = 160;
	canvas.height = 144;
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
	ctx.imageSmoothingEnabled = false;
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
		ctx.drawImage(pictures[imageNames[scene]], 0, 0);
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
			if (t >= 60 && (t - 60) % 63 < 37) ctx.drawImage(pictures.press_start, 35, 94);
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
	requestAnimationFrame(animate);
	document.addEventListener('keydown', (e) => {
		if (app.mode === 'title' || app.mode === 'duel' || e.target.tagName === 'INPUT') return;
		if (e.key === 'Shift') {
			app.running = true;
			return;
		}
		app.running = e.shiftKey;
		const k = keyOf(e);
		if (k) {
			e.preventDefault();
			dispatch(k);
		}
	});
	document.addEventListener('keyup', (e) => {
		if (e.key === 'Shift') app.running = false;
	});
	await showTitle(true);
}
boot().catch(showError);
