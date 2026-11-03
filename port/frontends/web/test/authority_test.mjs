// Server-run (authoritative) online duels without a network: two simulated clients rebuild
// their boards from their own item streams (RemoteDuel) and answer their own requests at random,
// from the request data they were sent.
//
//   node test/authority_test.mjs [--seeds 1-24] [--verbose]
//
// Checks, for every duel: it finishes (or stops at an unported stub, reported); at every request
// the server waits on, each client's board equals the server's board as that duelist may see it;
// no client ever holds deck order, prize identities, the opponent's hand order, or the identity
// of an opponent card in their hand, deck or prizes that was never shown to them; no text sent
// outside a duelist's own screens names a card hidden from them; answers from the wrong duelist,
// unknown ids and bad values are rejected or coerced.
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const dist = path.join(here, '..', 'dist');
const { loadAuthority } = await import(path.join(here, '..', 'server', 'authority.mjs'));
const { Engine, REQ, EV, C, TCG_CANCEL, TCG_PLAYER, TCG_OPPONENT } = await import(path.join(dist, 'js', 'engine.js'));
const { RemoteDuel, redactDuelvars, locationVisible, sideOf } = await import(path.join(dist, 'js', 'duel_view.js'));
const { readFile } = await import('node:fs/promises');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const [s0, s1] = String(opt('--seeds', '1-24')).split('-').map(Number);
const verbose = args.includes('--verbose');

const authority = await loadAuthority(dist);
const statics = await Engine.load(await readFile(path.join(dist, 'tcg.wasm')));

let failures = 0;
const fail = (msg) => { failures++; console.log(`  FAIL: ${msg}`); };

function makeRng(seed) {
	let s = BigInt(seed) * 2654435761n + 7n;
	return (n) => {
		s = (s * 6364136223846793005n + 1442695040888963407n) & 0xffffffffffffffffn;
		return n ? Number((s >> 33n) % BigInt(n)) : 0;
	};
}

function randomAnswer(r, rnd) {
	switch (r.type) {
	case REQ.DUEL_MENU: return [0, 0, 1, 1, 3, 4, 5, 5][rnd(8)];
	case REQ.YES_NO: return rnd(4) !== 0 ? 1 : 0;
	case REQ.CARD_LIST: return !r.numItems || rnd(8) === 0 ? TCG_CANCEL : rnd(r.numItems);
	case REQ.CARD_ITEM_MENU: return rnd(10) === 0 ? 1 : 0;
	case REQ.PLAY_AREA: return r.arg[1] || rnd(10) === 0 ? TCG_CANCEL : r.arg[0] + rnd(r.numItems || 1);
	case REQ.PRIZE: return rnd(6);
	default: return !r.numItems || rnd(10) === 0 ? TCG_CANCEL : rnd(r.numItems);
	}
}

// the card names a text could leak
const names = new Map();
for (const card of statics.cards()) if (card.name) names.set(card.id, card.name);

const decks = [C.CHARMANDER_AND_FRIENDS_DECK_ID, C.SQUIRTLE_AND_FRIENDS_DECK_ID, C.BULBASAUR_AND_FRIENDS_DECK_ID];
for (let id = C.LIGHTNING_AND_FIRE_DECK_ID; id <= C.IMAKUNI_DECK_ID; id++) decks.push(id);
const deckCards = (id) => statics.deckList(id + 2).cards;

const totals = { finished: 0, fatal: 0, items: [0, 0], publicSpans: 0, waits: 0, otherTexts: new Map() };

for (let seed = s0; seed <= s1; seed++) {
	const rnd = makeRng(seed);
	const deckIds = [decks[seed % decks.length], decks[(seed * 7 + 3) % decks.length]];
	const clients = [0, 1].map(() => ({ remote: new RemoteDuel(statics), seen: new Set(), queue: [] }));
	let duel = null;
	const emit = (v, item) => clients[v].queue.push(JSON.parse(JSON.stringify(item))); // over the wire
	duel = authority.createDuel({ decks: deckIds.map(deckCards), names: ['HOST', 'GUEST'], emit, random: makeRng(seed * 977) });
	const truthSide = (side) => duel.duel.duelvars(sideOf(side));
	const trueId = (side, idx) => duel.duel.cardId(sideOf(side), idx);
	const where = `seed ${seed} (${deckIds.join(' vs ')})`;
	let errors = 0;
	const err = (m) => { if (errors++ < 5) fail(`${where}: ${m}`); };

	/* opponent cards a viewer has had a legitimate look at: in a public zone or shown by an item */
	const everShown = [new Set(), new Set()];
	const knownNames = [0, 1].map((v) => new Set(deckCards(deckIds[v]).map((id) => names.get(id))));
	const notePublic = (v) => {
		for (let side = 0; side < 2; side++) for (let idx = 0; idx < 60; idx++)
			if (locationVisible(truthSide(side)[idx], side === v, duel.revealed)) everShown[v].add(side * 64 + idx);
	};

	/* a client processes its items; returns the item that needs its answer, if any */
	const drain = (v) => {
		const cl = clients[v];
		let actionable = null;
		while (cl.queue.length) {
			const item = cl.queue.shift();
			if (item.n !== cl.seen.size) err(`viewer ${v}: item ${item.n} out of order`);
			cl.seen.add(item.n);
			totals.items[v]++;
			if (JSON.stringify(item).match(/"(seed|rng|setup|cards)"/)) err(`viewer ${v}: item ${item.n} carries setup data`);
			cl.remote.apply(item);
			// identities shown by this item count as legitimately seen
			if (item.k === 'ev' && item.ev.type === EV.CARD && item.ev.arg[0] < 60) everShown[v].add((item.ev.arg[1] === TCG_OPPONENT ? 64 : 0) + item.ev.arg[0]);
			if (item.k === 'req' && item.own) for (let i = 0; i + 2 < (item.c || []).length; i += 3) everShown[v].add(item.c[i] * 64 + item.c[i + 1]);
			if (item.k === 'ev' && item.l) for (const idx of item.l) everShown[v].add((item.ev.arg[1] === TCG_OPPONENT ? 64 : 0) + idx);
			if (item.k === 'wait') totals.waits++;
			// identities sent while the card is in play or discarded
			for (let i = 0; i + 2 < (item.c || []).length; i += 3) {
				const loc = cl.remote.dv[item.c[i]][item.c[i + 1]];
				if ((loc & C.CARD_LOCATION_PLAY_AREA) || loc === C.CARD_LOCATION_DISCARD_PILE) everShown[v].add(item.c[i] * 64 + item.c[i + 1]);
			}
			if (item.k === 'req' && item.act) actionable = item;
			if (item.k === 'end') cl.end = item;
		}
		return actionable;
	};

	/* the client's board equals the server's board as this duelist may see it */
	const compare = (v) => {
		const r = clients[v].remote;
		for (let side = 0; side < 2; side++) {
			const want = redactDuelvars(truthSide(side), side === v, duel.revealed);
			const have = r.dv[side];
			for (let off = 0; off < 256; off++) if (want[off] !== have[off]) { err(`viewer ${v} side ${side} duelvar ${off.toString(16)}: ${have[off]} != ${want[off]}`); break; }
			for (let idx = 0; idx < 60; idx++) {
				const id = r.ids[side][idx], loc = truthSide(side)[idx];
				if (locationVisible(loc, side === v, duel.revealed) && id !== trueId(side, idx)) err(`viewer ${v}: visible card ${side}/${idx} is ${id}, not ${trueId(side, idx)}`);
				if (id && id !== trueId(side, idx)) err(`viewer ${v}: card ${side}/${idx} has the wrong identity`);
				if (side !== v && id && !locationVisible(loc, false, duel.revealed) && !everShown[v].has(side * 64 + idx))
					err(`viewer ${v}: knows hidden opponent card ${idx} (${names.get(id)}) at location ${loc} it never saw`);
			}
			for (let i = 0; i < 6; i++) if (have[C.DUELVARS_PRIZE_CARDS + i] !== 0xff) err('prize identities sent');
			for (let i = 0; i < 60; i++) if (have[C.DUELVARS_DECK_CARDS + i] !== 0xff) { err('deck order sent'); break; }
			if (side !== v) for (let i = 0; i < 60; i++) if (have[C.DUELVARS_HAND + i] !== 0xff) { err('opponent hand order sent'); break; }
		}
	};

	// text leak audit on the spans this viewer does not own
	const origDeliver = duel.deliver.bind(duel);
	duel.deliver = (v, span, term, end) => {
		const before = duel.logs[v].length;
		origDeliver(v, span, term, end);
		const owner = term && (term.req.duelist === TCG_OPPONENT ? 1 : 0) === v;
		const added = duel.logs[v].slice(before);
		if (!owner && added.some((i) => i.k === 'ev')) totals.publicSpans++;
		const known = knownNames[v]; // names this viewer has been sent, or put in their own deck
		for (const i of added) for (let k = 0; k + 2 < (i.c || []).length; k += 3) known.add(names.get(i.c[k + 2]));
		if (owner) return;
		// texts in spans this viewer does not own: names of cards hidden from them
		const hiddenNames = new Set();
		const o = 1 - v;
		for (let idx = 0; idx < 60; idx++) {
			const id = trueId(o, idx);
			if (!locationVisible(end.dv[o][idx], false, end.revealed) && !known.has(names.get(id))) hiddenNames.add(names.get(id));
		}
		for (const i of added) {
			if (i.k !== 'ev' || i.ev.type !== EV.TEXT || !i.ev.str) continue;
			totals.otherTexts.set(i.ev.text, (totals.otherTexts.get(i.ev.text) || 0) + 1);
			let text = i.ev.str.replace(/\s+/g, ' ');
			const template = statics.text(i.ev.text) || ''; // names in the fixed wording ("Fire Energy from the Hand")
			for (const n of [...known].filter(Boolean).sort((a, b) => b.length - a.length)) text = text.split(n).join('~');
			for (const n of hiddenNames) if (n && !template.includes(n) && new RegExp(`\\b${n.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`).test(text))
				err(`viewer ${v}: text "${text}" names hidden card ${n}`);
		}
	};

	const done = duel.start();
	let steps = 0;
	for (;;) {
		await new Promise((r) => setImmediate(r));
		if (duel.result) break;
		const p = duel.pending;
		if (!p) continue;
		for (const v of [0, 1]) notePublic(v);
		const acts = [drain(0), drain(1)];
		for (const v of [0, 1]) compare(v);
		const v = p.side;
		const item = acts[v];
		if (!item || item.id !== p.id) { err(`viewer ${v} has no actionable item for request ${p.id}`); duel.abort('test'); break; }
		if (acts[1 - v]) err(`viewer ${1 - v} was asked request ${acts[1 - v].id} it does not own`);
		if (steps === 3) {
			if (duel.answer(1 - v, p.id, 0) !== 'not your request') err('answer from the wrong duelist accepted');
			if (duel.answer(v, p.id + 5, 0) !== 'no such request') err('answer to an unknown request accepted');
		}
		const value = steps === 7 ? 'x' : randomAnswer(item.req, rnd);
		if (duel.answer(v, item.id, value) !== null) err('valid answer rejected');
		if (duel.answer(v, item.id, value) !== null) err('duplicate answer not ignored');
		if (++steps > 40000) { err('too many requests'); duel.abort('test'); break; }
	}
	await done;
	drain(0); drain(1);
	const res = duel.result || {};
	for (const v of [0, 1]) if (!clients[v].end && !res.aborted) err(`viewer ${v} got no end item`);
	if (res.error) {
		totals.fatal++;
		console.log(`  ${where}: engine stopped: ${res.error}`);
	} else if (res.finished !== undefined) {
		totals.finished++;
		if (verbose) console.log(`  ${where}: finished in ${res.turns} turns, ${steps} answers, items ${duel.logs[0].length}/${duel.logs[1].length}`);
	}
}

console.log(`duels: ${totals.finished} finished, ${totals.fatal} stopped at stubs; items host ${totals.items[0]}, guest ${totals.items[1]}; spans shown to the waiting duelist ${totals.publicSpans}; waits ${totals.waits}`);
if (verbose) {
	const { T } = await import(path.join(dist, 'js', 'engine.js'));
	const tname = Object.fromEntries(Object.entries(T).map(([k, v]) => [v, k]));
	console.log('texts outside own screens:', [...totals.otherTexts].sort((a, b) => b[1] - a[1]).map(([id, n]) => `${tname[id] || id} ${n}`).join(', '));
}
console.log(failures ? `FAILED (${failures})` : 'PASS');
process.exit(failures ? 1 : 0);
