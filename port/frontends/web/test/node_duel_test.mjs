// Runs complete hot-seat duels in Node with a random policy (like tcgcli's random_answer),
// through the same engine.js API the browser uses, with answers delivered both synchronously
// and asynchronously (Promises -> Asyncify unwind/rewind).
//
//   node test/node_duel_test.mjs [--seeds 1-12] [--max-requests 100000] [--verbose]
//
// Each duel must either finish (TCG_EV_DUEL_END + run() resolves) or stop at a tcg_fatal from a
// not-yet-ported stub (reported, not a failure). Anything else (wasm trap, asyncify error,
// request budget exhausted, inconsistent replay) fails the test.
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const dist = path.join(here, '..', 'dist');
const { Engine, TcgFatal, REQ, EV, C, TCG_CANCEL, TCG_SAVE_SIZE, TCG_CONTINUE_BROKEN, TCG_SAMS_PRACTICE_DECK_ID } = await import(path.join(dist, 'js', 'engine.js'));

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const [s0, s1] = String(opt('--seeds', '1-12')).split('-').map(Number);
const maxRequests = Number(opt('--max-requests', 100000));
const verbose = args.includes('--verbose');
const vsAI = args.includes('--vs-ai'); // P2 is the engine's AI (TCG_AI with its own deck) instead of a random human

function makeRng(seed) {
	let s = BigInt(seed) * 2654435761n + 1n;
	return (n) => {
		s = (s * 6364136223846793005n + 1442695040888963407n) & 0xffffffffffffffffn;
		return n ? Number((s >> 33n) % BigInt(n)) : 0;
	};
}

function randomAnswer(r, rnd) {
	switch (r.type) {
	case REQ.ACK: return 0;
	case REQ.DUEL_MENU: {
		const weights = [40, 30, 1, 8, 6, 15];
		let t = rnd(100);
		for (let i = 0; i < 6; i++) { if (t < weights[i]) return i; t -= weights[i]; }
		return 5;
	}
	case REQ.YES_NO: return rnd(4) !== 0 ? 1 : 0;
	case REQ.CARD_LIST:
		if (r.arg[0] || !r.numItems || rnd(8) === 0) return TCG_CANCEL;
		return rnd(r.numItems);
	case REQ.CARD_ITEM_MENU: return rnd(10) === 0 ? 1 : 0;
	case REQ.PLAY_AREA:
		if (r.arg[1] || rnd(10) === 0) return TCG_CANCEL;
		return r.arg[0] + rnd(r.numItems || 1);
	case REQ.PRIZE: return rnd(6);
	default:
		if (!r.numItems || rnd(10) === 0) return TCG_CANCEL;
		return rnd(r.numItems);
	}
}

const duel = await Engine.load(await readFile(path.join(dist, 'tcg.wasm')));
const info = JSON.parse(await readFile(path.join(dist, 'build-info.json'), 'utf8'));
console.log(`engine build: ${info.config}${info.skipped.length ? ' (not linked: ' + info.skipped.join(', ') + ')' : ''}`);

// sanity checks of the static data API
const bulbasaur = duel.card(C.BULBASAUR);
if (bulbasaur.name !== 'Bulbasaur' || bulbasaur.hp !== 40 || bulbasaur.attacks[0].name !== 'Leech Seed')
	throw new Error('card data API broken: ' + JSON.stringify(bulbasaur));
const deck = duel.deckList(C.CHARMANDER_AND_FRIENDS_DECK_ID + 2);
if (deck.cards.length !== 60 || duel.text(deck.nameText) !== 'Charmander & Friends')
	throw new Error('deck list API broken');

// every pre-constructed deck: the starter decks and all club/opponent decks (*_DECK_ID)
const decks = [C.CHARMANDER_AND_FRIENDS_DECK_ID, C.SQUIRTLE_AND_FRIENDS_DECK_ID, C.BULBASAUR_AND_FRIENDS_DECK_ID];
for (let id = C.LIGHTNING_AND_FIRE_DECK_ID; id <= C.IMAKUNI_DECK_ID; id++) decks.push(id);

let failures = 0;
const summary = { finished: 0, fatal: 0 };
const fatals = new Map();
const reqTypes = {};
for (let seed = s0; seed <= s1; seed++) {
	const rnd = makeRng(seed);
	const d0 = decks[seed % decks.length], d1 = decks[(seed * 17 + 5) % decks.length];
	const setup = {
		duelists: [
			{ kind: 'human', cards: duel.deckList(d0 + 2).cards, name: 'P1' },
			vsAI ? { kind: 'ai', deckId: d1, name: 'AI' } : { kind: 'human', cards: duel.deckList(d1 + 2).cards, name: 'P2' },
		],
		prizes: 6, seed: seed * 7919,
	};
	const answers = [];
	let texts = 0, ended = null, turns = 0;
	const handlers = {
		onEvent(ev) {
			if (ev.type === EV.TEXT) texts++;
			if (ev.type === EV.TURN) turns++;
			if (ev.type === EV.DUEL_END) ended = ev.arg[0];
			if (verbose && ev.type === EV.TEXT && ev.str) console.log('  | ' + ev.str.replace(/\n/g, ' '));
			// exercise the async event path now and then
			if (ev.type === EV.WAIT && rnd(4) === 0) return Promise.resolve();
		},
		onRequest(r) {
			if (r.seq >= maxRequests) throw new Error('request budget exhausted');
			// the AI never answers requests, except the ones that only show something (ACK, view-only card lists)
			// (TCG_REQ_CONFIRM after an AI's effect shows the human the changed play area)
			const viewOnly = r.type === REQ.ACK || r.type === REQ.CONFIRM || (r.type === REQ.CARD_LIST && r.arg[0] === 1);
			if (vsAI && !viewOnly && r.duelist !== 0xc2) throw new Error(`a request (type ${r.type}) for the AI duelist`);
			const tk = Object.keys(REQ).find((k) => REQ[k] === r.type) + (r.type === REQ.MENU ? `/${r.menu}` : '');
			reqTypes[tk] = (reqTypes[tk] || 0) + 1;
			const a = randomAnswer(r, rnd);
			answers.push(a);
			// half of the answers go through a Promise (Asyncify unwind/rewind)
			return (r.seq & 1) ? new Promise((res) => setImmediate(() => res(a))) : a;
		},
	};
	const t0 = Date.now();
	let outcome;
	try {
		const res = await duel.run(setup, handlers);
		if (ended === null) throw new Error('run() resolved without TCG_EV_DUEL_END');
		outcome = `finished: ${res.result === 0 ? 'P1' : 'P2'} won (wDuelFinished=${res.finished}, turns=${res.turns}, requests=${res.requests})`;
		summary.finished++;
	} catch (e) {
		if (e instanceof TcgFatal) {
			outcome = `stub abort after ${answers.length} requests, ${turns} turns: ${e.message}`;
			summary.fatal++;
			fatals.set(e.message, (fatals.get(e.message) || 0) + 1);
		} else {
			outcome = `FAILED: ${e.stack || e}`;
			failures++;
		}
	}
	// determinism: replaying the same answers synchronously must reproduce the same state
	let replayOk = true;
	if (!outcome.startsWith('FAILED')) {
		const hashBefore = duel.stateHash();
		let i = 0;
		try {
			await duel.run(setup, { onRequest: () => answers[i++] });
		} catch (e) {
			if (!(e instanceof TcgFatal)) { replayOk = false; outcome += ` / replay error ${e}`; }
		}
		if (duel.stateHash() !== hashBefore || i !== answers.length) {
			replayOk = false;
			outcome += ` / REPLAY MISMATCH (${i}/${answers.length} answers)`;
		}
		if (!replayOk) failures++;
	}
	console.log(`seed ${seed} (deck ${d0} vs ${d1}): ${outcome} [${texts} texts, ${Date.now() - t0} ms]`);
}
console.log(`\n${summary.finished} finished, ${summary.fatal} stopped at an unported stub, ${failures} failures`);
for (const [m, n] of fatals) console.log(`  ${n}x ${m}`);
console.log('requests by type:', JSON.stringify(reqTypes));

// ---------------------------------------------------------------- save files / Continue Duel
// Stop a duel at a human's duel menu (right after the engine saved it, like the original
// writes sCurrentDuel each time it prints the duel menu), then continue it from the save:
// the continued duel must be at the same state (tcg_state_hash) when its duel menu is
// printed again, tcg_saved_duel_info must describe the stop point, and it must play on to the end.
class Stop extends Error {}
const popcount = (v) => { let n = 0; for (; v; v >>= 1) n += v & 1; return n; };
const check = (ok, msg) => { if (!ok) { failures++; console.log('  FAIL: ' + msg); } return ok; };
async function saveContinueCase(seed, ai) {
	const rnd = makeRng(seed);
	const d0 = decks[seed % decks.length], d1 = decks[(seed * 11 + 7) % decks.length];
	const setup = {
		duelists: [{ kind: 'human', cards: duel.deckList(d0 + 2).cards, name: 'P1' },
			ai ? { kind: 'ai', deckId: d1, name: 'AI' } : { kind: 'human', cards: duel.deckList(d1 + 2).cards, name: 'P2' }],
		prizes: 6, seed: seed * 104729,
	};
	let save = null, saves = 0, stop = null;
	try {
		await duel.run(setup, {
			onSave(d) { saves++; save = d; },
			onRequest(r) {
				if (r.type === REQ.DUEL_MENU && duel.w('wDuelTurns') >= 4 && save) {
					const v = (d) => duel.duelvars(d);
					stop = { hash: duel.stateHash(), turns: duel.w('wDuelTurns'), whose: duel.h('hWhoseTurn'),
						prizes: [popcount(v(0xc2)[C.DUELVARS_PRIZES]), popcount(v(0xc3)[C.DUELVARS_PRIZES])] };
					throw new Stop();
				}
				return randomAnswer(r, rnd);
			},
		});
		return `seed ${seed}: the duel ended before turn 4 (not a save test)`;
	} catch (e) {
		if (!(e instanceof Stop)) {
			if (e instanceof TcgFatal) return `seed ${seed}: stub abort before the stop point: ${e.message}`;
			throw e;
		}
	}
	const label = `save/continue ${ai ? 'vs AI' : 'hot-seat'} seed ${seed}`;
	check(save && save.length === TCG_SAVE_SIZE, `${label}: save image of ${TCG_SAVE_SIZE} bytes`);
	const info = duel.savedDuelInfo(save);
	check(info && info.turns === stop.turns && info.whoseTurn === stop.whose && info.prizesLeft.join() === stop.prizes.join()
		&& info.duelistType[0] === C.DUELIST_TYPE_PLAYER && (ai ? info.duelistType[1] === (C.DUELIST_TYPE_AI_OPP | d1) : info.duelistType[1] === C.DUELIST_TYPE_PLAYER),
		`${label}: saved duel info ${JSON.stringify(info)} matches the stop point ${JSON.stringify(stop)}`);
	let firstMenuHash = null, saves2 = 0, ended = false;
	let res;
	try {
		res = await duel.continueDuel({ duelists: [{ name: 'P1' }, { name: ai ? 'AI' : 'P2' }] }, save, {
			onSave() { saves2++; },
			onEvent(ev) { if (ev.type === EV.DUEL_END) ended = true; },
			onRequest(r) {
				if (r.seq >= maxRequests) throw new Error('request budget exhausted');
				if (r.type === REQ.DUEL_MENU && firstMenuHash === null) firstMenuHash = duel.stateHash();
				return (r.seq & 1) ? Promise.resolve(randomAnswer(r, rnd)) : randomAnswer(r, rnd);
			},
		});
	} catch (e) {
		if (!(e instanceof TcgFatal)) throw e;
		res = { fatal: e.message };
	}
	check(firstMenuHash === stop.hash, `${label}: continued duel resumes at the saved state (hash ${(firstMenuHash >>> 0).toString(16)} vs ${stop.hash.toString(16)})`);
	check(saves2 > 0, `${label}: the continued duel keeps saving`);
	check(res.fatal || (ended && !res.broken), `${label}: continued duel ${res.fatal ? 'stopped at a stub' : 'played to the end'}`);
	return `${label}: stopped at turn ${stop.turns + 1} after ${saves} saves, continued ${res.fatal ? 'until a stub: ' + res.fatal : `to the end (${res.turns} turns, ${saves2} saves)`}`;
}
for (const [seed, ai] of [[3, false], [5, true], [8, false], [13, true]]) console.log(await saveContinueCase(seed, ai));

// a broken save: the engine prints the "backup is broken" text and returns TCG_CONTINUE_BROKEN
{
	let save = null;
	await duel.run({ duelists: [{ kind: 'human', cards: duel.deckList(decks[0] + 2).cards }, { kind: 'human', cards: duel.deckList(decks[1] + 2).cards }], seed: 1 },
		{ onSave(d) { save = d; }, onRequest(r) { if (save && r.type === REQ.DUEL_MENU) throw new Stop(); return randomAnswer(r, makeRng(r.seq)); } }).catch((e) => { if (!(e instanceof Stop)) throw e; });
	const bad = save.slice();
	bad[700] ^= 0x5a;
	const texts = [];
	const res = await duel.continueDuel(null, bad, { onEvent(ev) { if (ev.type === EV.TEXT) texts.push(ev.str); }, onRequest: () => 0 });
	check(duel.savedDuelInfo(bad) === null && res.broken && res.result === TCG_CONTINUE_BROKEN,
		`broken save: tcg_saved_duel_info rejects it and Continue returns TCG_CONTINUE_BROKEN (texts: ${JSON.stringify(texts)})`);
	const res2 = await duel.continueDuel(null, save.slice(0, 100), { onRequest: () => 0 });
	check(res2.broken, 'short save: TCG_CONTINUE_BROKEN');
	console.log(`broken save: ${JSON.stringify(texts.join(' / '))} -> TCG_CONTINUE_BROKEN`);
}

// ---------------------------------------------------------------- the practice duel with Sam
// (random answers stray from Dr. Mason's script, so he keeps explaining: the duel must keep
// asking without errors; with enough requests it ends by the turn limit or a knock out)
{
	const rnd = makeRng(77);
	const texts = new Set();
	let res, out;
	try {
		res = await duel.run({ duelists: [{ kind: 'human', name: 'MARK' }, { kind: 'ai', deckId: TCG_SAMS_PRACTICE_DECK_ID, name: 'SAM' }], prizes: 2, music: C.MUSIC_DUEL_THEME_1, seed: 5 }, {
			onEvent(ev) { if (ev.type === EV.TEXT && ev.str) texts.add(ev.str.split('\n')[0]); },
			onRequest(r) {
				if (r.seq >= 30000) throw new Stop();
				if (r.type !== REQ.ACK && r.type !== REQ.CONFIRM && !(r.type === REQ.CARD_LIST && r.arg[0]) && r.duelist !== 0xc2) throw new Error('practice: a request for Sam');
				return randomAnswer(r, rnd);
			},
		});
		out = `finished (${res.result === 0 ? 'player' : 'Sam'} won, ${res.turns} turns, ${res.requests} requests)`;
	} catch (e) {
		if (e instanceof Stop) out = 'still going after 30000 random answers';
		else if (e instanceof TcgFatal) out = `stub abort: ${e.message}`;
		else { failures++; out = `FAILED: ${e.stack || e}`; }
	}
	console.log(`practice duel: ${out}; ${texts.size} different texts, e.g. ${JSON.stringify([...texts].find((t) => /Mason|practice|Sam/i.test(t)) || [...texts][0])}`);
}

console.log(failures ? `${failures} failures` : 'all save/continue and practice checks passed');
process.exit(failures ? 1 : 0);
