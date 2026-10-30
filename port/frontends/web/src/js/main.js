// App controller: title menu, deck/opponent selection, duels (vs AI, hot-seat, online), deck builder.
import { Engine, TcgFatal, DuelAborted, TCG_PLAYER, TCG_OPPONENT, TCG_SAMS_PRACTICE_DECK_ID, C, T } from './engine.js';
import { DuelUI, keyOf, K } from './duel_ui.js';
import { DeckBuilder } from './deckbuilder.js';
import { allDecks, preconstructedDecks, validateDeck, deckColors } from './data.js';
import { RelayClient, relayUrl, OnlineDuel, saveSession, loadSession, clearSession } from './net.js';
import { el, clear, art, esym } from './gfx.js';
import {
	SAVE_STORE_DUEL, saveStoreOpen, saveStoreRead, saveStoreWrite, saveStoreRemove, saveStoreReadJSON, saveStoreWriteJSON,
	exportFile, importFile, hasFileSystemAccess,
} from './save_store.js';
import { NPC_DECKS } from './gen/assets.js';

const params = new URLSearchParams(location.search);
const FAST = params.has('fast');           // tests: no pacing delays
const NAME_KEY = 'tcg.playerName';
const SKIP_DELAY_KEY = 'tcg.skipDelayAllowed';
const SAVE_META = 'duel-meta';          // who played the saved duel (names, kinds, mode), JSON

const app = {
	duel: null,
	info: null,
	root: document.getElementById('app'),
	keyHandler: null,
	ui: null,
	client: null,
};
window.tcgApp = app; // for debugging and tests

function playerName() {
	try { return localStorage.getItem(NAME_KEY) || 'MARK'; } catch { return 'MARK'; }
}
function setPlayerName(n) {
	try { localStorage.setItem(NAME_KEY, n); } catch { /* ignore */ }
}

function randomSeed() { return (Math.random() * 0x1000000) >>> 0; }

/* the original's debug option sSkipDelayAllowed (TcgDuelSetup.skip_delay_allowed) */
function skipDelayAllowed() {
	try { return localStorage.getItem(SKIP_DELAY_KEY) === '1'; } catch { return false; }
}
function setSkipDelayAllowed(on) {
	try { localStorage.setItem(SKIP_DELAY_KEY, on ? '1' : '0'); } catch { /* ignore */ }
}

// ---------------------------------------------------------------- the saved duel ("Continue Duel")
/* the saved duel and what we know about it, or null (online/link saves are never offered) */
function savedDuel() {
	const data = saveStoreRead(SAVE_STORE_DUEL);
	if (!data) return null;
	const info = app.duel.savedDuelInfo(data);
	if (!info) return { data, info: null, meta: null };
	if (info.duelType === C.DUELTYPE_LINK) return null;
	let meta = saveStoreReadJSON(SAVE_META);
	const kinds = info.duelistType.map((t) => (t & C.DUELIST_TYPE_AI_OPP ? 'ai' : 'human'));
	if (!meta || !Array.isArray(meta.kinds) || meta.kinds.join() !== kinds.join()) {
		// no side data (e.g. an imported file): derive it from the save
		const aiName = (t) => {
			const deck = t & 0x7f, npc = NPC_DECKS[deck];
			return npc ? app.duel.text(T[npc.name]) : 'OPPONENT';
		};
		const hotseat = kinds[0] === 'human' && kinds[1] === 'human';
		meta = {
			mode: hotseat ? 'hotseat' : 'ai',
			kinds,
			names: info.duelistType.map((t, i) => (t & C.DUELIST_TYPE_AI_OPP ? aiName(t) : hotseat ? `P${i + 1}` : playerName())),
			title: 'Saved duel',
		};
	}
	return { data, info, meta };
}

function clearSavedDuel() {
	saveStoreRemove(SAVE_STORE_DUEL);
	saveStoreRemove(SAVE_META);
}

function toast(msg, ms = 3200) {
	let t = document.querySelector('.toast');
	if (!t) { t = el('div', 'toast'); document.body.appendChild(t); }
	t.textContent = msg;
	t.classList.add('show');
	clearTimeout(t._timer);
	t._timer = setTimeout(() => t.classList.remove('show'), ms);
}

function setScale() {
	const w = Math.min(window.innerWidth, 1100) - 12;
	const h = window.innerHeight - 8;
	let u = Math.min(w / 240, h / 262);
	u = u >= 2 ? Math.floor(u * 2) / 2 : Math.max(1.25, Math.floor(u * 4) / 4);
	document.documentElement.style.setProperty('--u', String(u));
}

/* replace the screen; returns the new screen element */
function screen(cls) {
	if (app.keyHandler) { document.removeEventListener('keydown', app.keyHandler); app.keyHandler = null; }
	if (app.ui) { app.ui.destroy(); app.ui = null; }
	const s = el('div', 'screen ' + cls);
	clear(app.root).appendChild(s);
	window.scrollTo(0, 0);
	return s;
}

/* keyboard navigation for a vertical list of buttons */
function menuKeys(buttons, onBack) {
	let cur = 0;
	const move = (i) => {
		if (!buttons.length) return;
		cur = (i + buttons.length) % buttons.length;
		buttons.forEach((b, k) => b.classList.toggle('cur', k === cur));
		buttons[cur].scrollIntoView({ block: 'nearest' });
	};
	buttons.forEach((b, i) => b.addEventListener('mouseenter', () => move(i)));
	app.keyHandler = (e) => {
		if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA')) return;
		const k = keyOf(e);
		if (!k) return;
		e.preventDefault();
		if (k === K.UP || k === K.LEFT) move(cur - 1);
		else if (k === K.DOWN || k === K.RIGHT) move(cur + 1);
		else if (k === K.A && buttons[cur]) buttons[cur].click();
		else if (k === K.B && onBack) onBack();
	};
	document.addEventListener('keydown', app.keyHandler);
	move(0);
}

// ---------------------------------------------------------------- title
function showTitle() {
	const s = screen('title');
	const logo = el('div', 'logo');
	logo.appendChild(el('div', 'logo-1', 'POKéMON'));
	logo.appendChild(el('div', 'logo-2', 'TRADING CARD GAME'));
	logo.appendChild(el('div', 'logo-3', 'duel engine · web'));
	s.appendChild(logo);
	const arts = el('div', 'title-arts');
	for (const id of [C.CHARIZARD, C.BLASTOISE, C.VENUSAUR_LV67, C.MEWTWO_LV53, C.PIKACHU_LV12]) if (id) arts.appendChild(art(id, 1));
	s.appendChild(arts);
	const nameRow = el('label', 'name-row', 'Your name ');
	const name = el('input', 'name-input');
	name.value = playerName();
	name.maxLength = 12;
	name.addEventListener('change', () => setPlayerName(name.value.trim() || 'MARK'));
	nameRow.appendChild(name);
	s.appendChild(nameRow);
	const menu = el('div', 'menu');
	const btn = (label, fn) => { const b = el('button', 'btn big', label); b.addEventListener('click', fn); menu.appendChild(b); return b; };
	const buttons = [];
	const sess = loadSession();
	if (sess && sess.room) buttons.push(btn(`Rejoin online duel (room ${sess.room})`, () => rejoinOnline(sess)));
	const saved = savedDuel();
	if (saved && saved.info) {
		const i = saved.info, m = saved.meta;
		const b = btn('Continue Duel', () => continueSavedDuel());
		b.classList.add('continue');
		b.appendChild(el('div', 'btn-sub', `${m.names[0]} vs. ${m.names[1]} · turn ${i.turns + 1} · prizes left ${i.prizesLeft[0]}/${i.prizesLeft[1]}`));
		buttons.push(b);
	}
	buttons.push(btn('Duel vs. Computer', () => chooseDeck('Choose your deck', (deck) => chooseOpponent(deck), showTitle)));
	buttons.push(btn('2 Players (same screen)', () => chooseDeck('Player 1: choose a deck', (d1) =>
		chooseDeck('Player 2: choose a deck', (d2) => startLocalDuel('hotseat', d1, d2), showTitle), showTitle)));
	buttons.push(btn('Online duel', () => chooseDeck('Choose your deck', (deck) => onlineLobby(deck), showTitle)));
	buttons.push(btn('Deck builder', () => showDeckBuilder()));
	s.appendChild(menu);
	s.appendChild(saveFileRow(saved));
	const opts = el('label', 'option-row');
	const skip = el('input');
	skip.type = 'checkbox';
	skip.className = 'skip-delay';
	skip.checked = skipDelayAllowed();
	skip.addEventListener('change', () => setSkipDelayAllowed(skip.checked));
	opts.append(skip, document.createTextNode(' Debug option: skip delays while B (X / Esc) is held'));
	s.appendChild(opts);
	const info = el('div', 'build-info');
	if (app.info) {
		const effects = app.info.effects.length ? `${app.info.effects.length} effect file(s)` : 'effect stubs';
		const ai = app.info.ai.length ? `${app.info.ai.length} AI file(s)` : 'AI stubs (not ported yet)';
		info.textContent = `Engine build: ${effects}, ${ai}. Unported routines stop the duel with a message.`;
	}
	s.appendChild(info);
	s.appendChild(el('div', 'help', 'Keys: arrows move · Z / Enter = A · X / Esc = B · C = card details. Mouse and touch work too.'));
	menuKeys(buttons, null);
}

/* save file management: export / import (File System Access API or download / file input), delete */
function saveFileRow(saved) {
	const row = el('div', 'save-row');
	row.appendChild(el('span', 'save-label', saved ? (saved.info ? 'Saved duel:' : 'Saved duel (broken):') : 'No saved duel.'));
	const mk = (label, fn) => { const b = el('button', 'btn small', label); b.addEventListener('click', fn); row.appendChild(b); return b; };
	if (saved) {
		mk('Export save file', async () => {
			try { if (await exportFile(saved.data, 'duel.sav')) toast(hasFileSystemAccess() ? 'Save file written.' : 'Save file downloaded.'); }
			catch (e) { toast(`Could not export: ${e.message}`); }
		});
	}
	mk('Import save file', async () => {
		let data;
		try { data = await importFile(); } catch (e) { toast(`Could not read the file: ${e.message}`); return; }
		if (!data) return;
		const info = app.duel.savedDuelInfo(data);
		if (!info) { toast('This is not a valid duel save file.'); return; }
		if (info.duelType === C.DUELTYPE_LINK) { toast('Link duels cannot be continued.'); return; }
		if (!saveStoreWrite(SAVE_STORE_DUEL, data)) { toast('Could not store the save file.'); return; }
		saveStoreRemove(SAVE_META);
		toast('Save file imported: choose Continue Duel.');
		showTitle();
	});
	if (saved) mk('Delete', () => { if (confirm('Delete the saved duel?')) { clearSavedDuel(); showTitle(); } });
	return row;
}

// ---------------------------------------------------------------- deck & opponent selection
function deckTile(deck, sub) {
	const b = el('button', 'deck-tile');
	const cardFn = (id) => app.duel.card(id);
	const poke = deck.cards.map(cardFn).filter((c) => c.isPokemon).sort((a, b) => b.stage - a.stage || b.hp - a.hp)[0];
	if (poke) b.appendChild(art(poke.id, 0.75));
	const t = el('div', 'dt-text');
	const name = el('div', 'dt-name');
	deckColors(deck.cards, cardFn).forEach((c) => name.appendChild(esym(c)));
	name.appendChild(document.createTextNode(' ' + deck.name));
	t.appendChild(name);
	if (sub) t.appendChild(el('div', 'dt-sub', sub));
	b.appendChild(t);
	return b;
}

function chooseDeck(title, onPick, onBack) {
	const s = screen('choose');
	const head = el('div', 'screen-head');
	const back = el('button', 'btn', '◀ Back');
	back.addEventListener('click', onBack);
	head.append(back, el('h2', '', title));
	s.appendChild(head);
	const decks = allDecks(app.duel);
	const buttons = [];
	const groups = { custom: 'My decks', starter: 'Starter decks', club: 'Club & opponent decks' };
	for (const [kind, label] of Object.entries(groups)) {
		const list = decks.filter((d) => d.kind === kind);
		if (!list.length) continue;
		s.appendChild(el('h3', '', label));
		const grid = el('div', 'deck-grid');
		for (const d of list) {
			const errors = validateDeck(d.cards, (id) => app.duel.card(id));
			const b = deckTile(d, kind === 'custom' ? `${d.cards.length} cards` : kind === 'starter' ? 'Starter deck' : d.npc ? `${d.npc}'s deck` : 'Club deck');
			if (errors.length) { b.disabled = true; b.title = errors.join(' '); }
			b.addEventListener('click', () => onPick(d));
			grid.appendChild(b);
			buttons.push(b);
		}
		s.appendChild(grid);
	}
	menuKeys(buttons.filter((b) => !b.disabled), onBack);
}

function chooseOpponent(deck) {
	const s = screen('choose');
	const head = el('div', 'screen-head');
	const back = el('button', 'btn', '◀ Back');
	back.addEventListener('click', () => chooseDeck('Choose your deck', (d) => chooseOpponent(d), showTitle));
	head.append(back, el('h2', '', 'Choose your opponent'));
	s.appendChild(head);
	if (app.info && !app.info.ai.length)
		s.appendChild(el('div', 'warn-box', 'The AI has not been ported to C yet in this build: a duel against the computer stops as soon as the AI has to act. Use 2 Players or Online in the meantime.'));
	const buttons = [];
	const other = el('div', 'deck-grid');
	const mk = (title, sub, fn) => {
		const b = el('button', 'deck-tile other-tile');
		const t = el('div', 'dt-text');
		t.append(el('div', 'dt-name', title), el('div', 'dt-sub', sub));
		b.appendChild(t);
		b.addEventListener('click', fn);
		other.appendChild(b);
		buttons.push(b);
	};
	mk('Practice duel with Sam', 'The tutorial duel: Dr. Mason explains each step (you play the practice deck)', () => startPracticeDuel());
	mk('Online opponent', 'Create or join a room on this server', () => onlineLobby(deck));
	mk('Second player (same screen)', 'Hot-seat: pick the second deck', () => chooseDeck('Player 2: choose a deck', (d2) => startLocalDuel('hotseat', deck, d2), showTitle));
	s.appendChild(other);
	s.appendChild(el('h3', '', 'Computer opponents'));
	const grid = el('div', 'deck-grid');
	for (const d of preconstructedDecks(app.duel)) {
		const b = deckTile(d, d.npc ? `${d.npc}` : d.kind === 'starter' ? 'Starter deck' : 'Club deck');
		b.addEventListener('click', () => startLocalDuel('ai', deck, d));
		grid.appendChild(b);
		buttons.push(b);
	}
	s.appendChild(grid);
	menuKeys(buttons, () => chooseDeck('Choose your deck', (d) => chooseOpponent(d), showTitle));
}

// ---------------------------------------------------------------- duels
function duelScreen(title, onQuit) {
	const s = screen('duel-screen');
	const bar = el('div', 'duel-bar');
	const quit = el('button', 'btn small', 'Quit');
	quit.addEventListener('click', onQuit);
	bar.append(quit, el('span', 'duel-title', title));
	s.appendChild(bar);
	const root = el('div', 'duel-root');
	s.appendChild(root);
	return root;
}

function startLocalDuel(mode, deck1, deck2, seed = randomSeed()) {
	const me = playerName();
	const names = mode === 'ai' ? [me, deck2.npc || deck2.name] : ['P1', 'P2'];
	const setup = {
		duelists: [
			{ kind: 'human', cards: deck1.cards, name: names[0] },
			mode === 'ai' ? { kind: 'ai', deckId: deck2.deckId, name: names[1] } : { kind: 'human', cards: deck2.cards, name: names[1] },
		],
		prizes: 6, seed, music: mode === 'ai' ? deck2.theme : 0, skipDelayAllowed: skipDelayAllowed(),
	};
	const title = mode === 'ai' ? `${me} vs. ${names[1]} (${deck2.name})` : `${deck1.name} vs. ${deck2.name}`;
	playLocalDuel({ mode, setup, names, title, rematch: () => startLocalDuel(mode, deck1, deck2) });
}

/* the practice duel with Sam: the engine plays it with the practice decks, 2 prizes */
function startPracticeDuel() {
	const names = [playerName(), app.duel.text(T.SamNPCName) || 'SAM'];
	const setup = {
		duelists: [{ kind: 'human', name: names[0] }, { kind: 'ai', deckId: TCG_SAMS_PRACTICE_DECK_ID, name: names[1] }],
		prizes: 2, seed: randomSeed(), music: C.MUSIC_DUEL_THEME_1, skipDelayAllowed: skipDelayAllowed(),
	};
	playLocalDuel({ mode: 'ai', setup, names, title: `Practice duel with ${names[1]}`, rematch: startPracticeDuel });
}

/* a hot-seat or vs-AI duel, saved like the original saves it (Continue Duel); `save` = resume it */
function playLocalDuel({ mode, setup, names, title, rematch, save = null, info = null }) {
	const root = duelScreen(title, () => {
		if (!confirm('Quit this duel? You can continue it later from the title screen.')) return;
		app.duel.abort('quit');
		showTitle();
	});
	const ui = new DuelUI(root, app.duel, { mode, localSide: TCG_PLAYER, pacing: !FAST, skipDelay: !!setup.skipDelayAllowed });
	ui.names = names;
	app.ui = ui;
	app.online = null;
	if (info) { // resuming mid-turn: no TCG_EV_TURN until the next turn
		ui.turnHolder = info.whoseTurn;
		ui.turnNumber = info.turns;
		if (mode === 'hotseat') ui.perspective = info.whoseTurn;
		ui.render();
	}
	if (!save) {
		clearSavedDuel(); // a new duel replaces the saved one
		saveStoreWriteJSON(SAVE_META, { mode, names, kinds: setup.duelists.map((d) => d.kind), title });
	}
	app.saves = 0;
	const handlers = {
		onEvent: (ev) => ui.onEvent(ev),
		onRequest: (req) => ui.onRequest(req),
		onSave: (data) => { app.saves++; saveStoreWrite(SAVE_STORE_DUEL, data); },
	};
	runDuel(ui, setup, handlers, {
		rematch,
		describe: (res) => (res.broken ? 'The saved duel could not be continued.' : describeResult(mode, res, names)),
		onEnd: (returned) => { if (returned) clearSavedDuel(); }, // the original clears the saved duel once it returns
		start: save ? () => app.duel.continueDuel(setup, save, handlers) : null,
	});
}

/* "Continue Duel": names and kinds come from the side data kept with the save */
function continueSavedDuel() {
	const saved = savedDuel();
	if (!saved || !saved.info) { toast('There is no saved duel to continue.'); showTitle(); return; }
	const { data, info, meta } = saved;
	const setup = {
		duelists: meta.kinds.map((kind, i) => ({ kind, name: meta.names[i] })),
		skipDelayAllowed: skipDelayAllowed(),
	};
	playLocalDuel({ mode: meta.mode, setup, names: meta.names, title: `${meta.title || 'Saved duel'} (continued)`, rematch: null, save: data, info });
}

function describeResult(mode, res, names, localSide = TCG_PLAYER) {
	if (res.finished === C.TURN_PLAYER_TIED) return 'The duel was a draw.';
	const winner = res.result === C.DUEL_WIN ? 0 : 1;
	if (mode === 'hotseat') return `${names[winner]} won the duel!`;
	const localIndex = localSide === TCG_PLAYER ? 0 : 1;
	return winner === localIndex ? 'You won the duel!' : `${names[winner]} won the duel.`;
}

async function runDuel(ui, setup, handlers, { rematch, describe, onEnd, start }) {
	try {
		const res = await (start ? start() : app.duel.run(setup, handlers));
		if (onEnd) onEnd(true);
		resultDialog(ui, describe(res), res.broken ? '' : `${res.turns} turns`, rematch);
	} catch (e) {
		if (onEnd) onEnd(false);
		if (e instanceof DuelAborted) return;
		if (e instanceof TcgFatal) {
			resultDialog(ui, 'The duel stopped', `The engine reached a part of the original game that is not ported to C yet:\n${e.message}`, rematch, 'fatal');
			return;
		}
		console.error(e);
		resultDialog(ui, 'The duel stopped', String(e && e.message || e), rematch, 'fatal');
	}
}

function resultDialog(ui, title, detail, rematch, cls = '') {
	if (!ui.root.isConnected) return;
	const m = ui.$modal;
	clear(m);
	const box = el('div', 'dialog result ' + cls);
	box.appendChild(el('div', 'dlg-title', title));
	if (detail) box.appendChild(el('div', 'dlg-detail', detail));
	const row = el('div', 'dlg-buttons');
	const buttons = [];
	if (rematch) { const b = el('button', 'btn primary', 'Duel again'); b.addEventListener('click', rematch); row.appendChild(b); buttons.push(b); }
	const menu = el('button', 'btn', 'Main menu');
	menu.addEventListener('click', showTitle);
	row.appendChild(menu);
	buttons.push(menu);
	box.appendChild(row);
	m.appendChild(box);
	m.className = 'modal';
	ui.controls = [];
	menuKeys(buttons, null);
}

// ---------------------------------------------------------------- online
function onlineLobby(deck) {
	const s = screen('lobby');
	const head = el('div', 'screen-head');
	const back = el('button', 'btn', '◀ Back');
	head.append(back, el('h2', '', 'Online duel'));
	s.appendChild(head);
	s.appendChild(el('p', '', `Your deck: ${deck.name}. Both players need to open this page from the same server.`));
	const box = el('div', 'lobby-box');
	const create = el('button', 'btn big', 'Create a room');
	const joinRow = el('div', 'join-row');
	const code = el('input', 'room-code');
	code.placeholder = 'ROOM';
	code.maxLength = 4;
	const join = el('button', 'btn big', 'Join room');
	joinRow.append(code, join);
	box.append(create, joinRow);
	s.appendChild(box);
	const status = el('div', 'lobby-status');
	s.appendChild(status);
	const client = new RelayClient(relayUrl());
	app.client = client;
	back.addEventListener('click', () => { client.send({ t: 'leave' }); client.close(); clearSession(); showTitle(); });
	const name = playerName();
	const deckMsg = { name: deck.name, cards: deck.cards };
	const setStatus = (t, cls = '') => { status.textContent = t; status.className = 'lobby-status ' + cls; };
	const connect = async () => {
		if (client.connected) return true;
		setStatus('Connecting…');
		try { await client.connect(); return true; } catch (e) { setStatus(e.message + ' (is the relay server running?)', 'err'); return false; }
	};
	client.on('error', (m) => setStatus(m.msg, 'err'));
	client.on('created', (m) => {
		saveSession({ room: m.room, token: m.token, role: 'host', name });
		create.disabled = join.disabled = true;
		setStatus(`Room ${m.room} created. Give this code to your opponent and wait for them to join…`, 'ok');
		const big = el('div', 'room-display', m.room);
		box.appendChild(big);
	});
	client.on('peer-joined', (m) => {
		const errors = validateDeck(m.deck && m.deck.cards || [], (id) => app.duel.card(id));
		const sess = loadSession();
		if (errors.length) { setStatus(`${m.name}'s deck is not valid: ${errors.join(' ')}`, 'err'); return; }
		const setup = {
			duelists: [
				{ kind: 'human', cards: deck.cards, name },
				{ kind: 'human', cards: m.deck.cards, name: m.name },
			],
			prizes: 6, seed: randomSeed(), skipDelayAllowed: skipDelayAllowed(),
		};
		client.send({ t: 'setup', setup });
		startOnline({ client, setup, role: 'host', peerName: m.name, session: sess, log: [] });
	});
	client.on('joined', (m) => {
		saveSession({ room: m.room, token: m.token, role: 'guest', name });
		create.disabled = join.disabled = true;
		setStatus(`Joined ${m.host.name}'s room ${m.room}. Waiting for the duel to start…`, 'ok');
		client.on('setup', (x) => startOnline({ client, setup: x.setup, role: 'guest', peerName: m.host.name, session: loadSession(), log: [] }));
	});
	create.addEventListener('click', async () => { if (await connect()) client.send({ t: 'create', name, deck: deckMsg }); });
	join.addEventListener('click', async () => {
		const room = code.value.trim().toUpperCase();
		if (!/^[A-Z]{4}$/.test(room)) { setStatus('Enter the 4-letter room code.', 'err'); return; }
		if (await connect()) client.send({ t: 'join', room, name, deck: deckMsg });
	});
	code.addEventListener('keydown', (e) => { if (e.key === 'Enter') join.click(); });
}

async function rejoinOnline(sess) {
	const s = screen('lobby');
	s.appendChild(el('h2', '', `Rejoining room ${sess.room}…`));
	const status = el('div', 'lobby-status');
	s.appendChild(status);
	const back = el('button', 'btn', 'Give up and return to the menu');
	back.addEventListener('click', () => { client.send({ t: 'leave' }); client.close(); clearSession(); showTitle(); });
	s.appendChild(back);
	const client = new RelayClient(relayUrl());
	app.client = client;
	client.on('error', (m) => { status.textContent = m.msg; status.className = 'lobby-status err'; });
	client.on('rejoined', (m) => {
		if (!m.setup) {
			status.textContent = 'The duel has not started yet. Create a new room instead.';
			clearSession();
			return;
		}
		startOnline({ client, setup: m.setup, role: m.role, peerName: m.peer ? m.peer.name : 'opponent', session: sess, log: m.log });
	});
	try {
		await client.connect();
		client.send({ t: 'rejoin', room: sess.room, token: sess.token });
	} catch (e) {
		status.textContent = e.message;
		status.className = 'lobby-status err';
	}
}

function startOnline({ client, setup, role, peerName, session, log }) {
	const localSide = role === 'host' ? TCG_PLAYER : TCG_OPPONENT;
	const names = [setup.duelists[0].name, setup.duelists[1].name];
	let finished = false;
	const root = duelScreen(`${names[0]} vs. ${names[1]} · online (${role === 'host' ? 'host' : 'guest'}, room ${session ? session.room : '?'})`, () => {
		if (!confirm('Leave this online duel? Your opponent will be told you left.')) return;
		finished = true;
		client.send({ t: 'leave' });
		client.close();
		clearSession();
		app.duel.abort('left');
		showTitle();
	});
	const ui = new DuelUI(root, app.duel, { mode: 'online', localSide, pacing: !FAST, remoteAckDelay: FAST ? 0 : 1400, skipDelay: !!setup.skipDelayAllowed });
	ui.names = names;
	app.ui = ui;
	const online = new OnlineDuel({
		ui, duel: app.duel, client, role, peerName, log,
		onDesync: (e) => ui.setStatus(e.message, 'err'),
		onStatus: (t, k) => ui.setStatus(t, k),
	});
	app.online = online;
	client.on('answer', (m) => online.receive(m));
	client.on('peer-left', () => ui.setStatus(`${peerName} lost the connection. Waiting for them to come back…`, 'warn'));
	client.on('peer-back', () => ui.setStatus(`${peerName} is back.`, 'ok'));
	client.on('peer-quit', () => {
		if (finished) return;
		finished = true;
		online.fail(new Error('peer quit'));
		app.duel.abort('peer quit');
		clearSession();
		resultDialog(ui, `${peerName} left the duel.`, '', null);
	});
	client.on('desync', (m) => {
		if (finished) return;
		finished = true;
		app.duel.abort('desync');
		resultDialog(ui, 'Desync', `${peerName}'s game went out of sync with yours (${m.detail}).`, null, 'fatal');
	});
	client.on('error', (m) => ui.setStatus(m.msg, 'err'));
	client.on('rejoined', (m) => {
		online.addLog(m.log);
		online.resendFrom(m.log.length);
		ui.setStatus(m.peer && m.peer.connected ? 'Reconnected.' : `Reconnected. ${peerName} is not connected right now.`, 'ok');
	});
	let delayMs = 1000;
	const reconnect = async () => {
		if (finished || client.closedByUser) return;
		ui.setStatus('Connection to the server lost. Reconnecting…', 'warn');
		try {
			await client.connect();
			delayMs = 1000;
			client.send({ t: 'rejoin', room: session.room, token: session.token });
		} catch {
			delayMs = Math.min(delayMs * 2, 10000);
			setTimeout(reconnect, delayMs);
		}
	};
	client.on('close', () => setTimeout(reconnect, 300));
	runDuel(ui, setup, { onEvent: (ev) => online.onEvent(ev), onRequest: (req) => online.onRequest(req) }, {
		rematch: null,
		describe: (res) => describeResult('online', res, names, localSide),
		onEnd: () => {
			if (finished) return;
			finished = true;
			if (!online.failed) { client.send({ t: 'leave' }); clearSession(); client.close(); }
		},
	});
}

// ---------------------------------------------------------------- deck builder
function showDeckBuilder() {
	const s = screen('deckbuilder-screen');
	const root = el('div', 'db-root');
	s.appendChild(root);
	app.builder = new DeckBuilder(root, app.duel, { onBack: showTitle, toast });
}

// ---------------------------------------------------------------- boot
async function boot() {
	setScale();
	saveStoreOpen('tcg.save.');
	window.addEventListener('resize', setScale);
	try {
		app.duel = await Engine.load(new URL('../tcg.wasm', import.meta.url));
	} catch (e) {
		clear(app.root).appendChild(el('div', 'boot-error', `Cannot load the engine: ${e.message}`));
		throw e;
	}
	try { app.info = await (await fetch(new URL('../build-info.json', import.meta.url))).json(); } catch { app.info = null; }
	showTitle();
}

boot();
