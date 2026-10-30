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
const { Engine, TcgFatal, REQ, EV, C, TCG_CANCEL } = await import(path.join(dist, 'js', 'engine.js'));

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
			const viewOnly = r.type === REQ.ACK || (r.type === REQ.CARD_LIST && r.arg[0] === 1);
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
process.exit(failures ? 1 : 0);
