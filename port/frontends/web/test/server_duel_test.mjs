// Protocol test of server-run online duels (no browser): two WebSocket clients create and join a
// 'server' room, play a whole duel by answering their own requests at random, reconnect midway
// (missed items resent from `from`), and rejoin from item 0 as a reloaded page would.
//
//   node test/server_duel_test.mjs [--port 8133] [--seed 5]
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, '..', 'server', 'server.js'));
const WebSocket = require('ws');
const dist = path.join(here, '..', 'dist');
const { Engine, REQ, C, TCG_CANCEL } = await import(path.join(dist, 'js', 'engine.js'));
const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const PORT = Number(opt('--port', 8133));
let seed = BigInt(opt('--seed', 5));
const rnd = (n) => { seed = (seed * 6364136223846793005n + 1442695040888963407n) & 0xffffffffffffffffn; return n ? Number((seed >> 33n) % BigInt(n)) : 0; };
let failed = false;
const check = (c, m) => { console.log(`  ${c ? 'ok' : 'FAIL'}: ${m}`); if (!c) failed = true; };

const statics = await Engine.load(await readFile(path.join(dist, 'tcg.wasm')));
const deck = (id) => ({ name: 'deck', cards: statics.deckList(id + 2).cards });

const proc = spawn(process.execPath, [path.join(here, '..', 'server', 'server.js'), '--port', String(PORT), '--dist', dist], { env: { ...process.env, QUIET: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
await new Promise((r) => proc.stdout.once('data', r));
await new Promise((r) => setTimeout(r, 500)); // the engine loads after the port opens

function client() {
	const c = { ws: new WebSocket(`ws://localhost:${PORT}/ws`), inbox: [], waiters: [], items: [] };
	c.ws.on('message', (d) => {
		const m = JSON.parse(d);
		if (m.t === 'item') { if (m.item.n === c.items.length) c.items.push(m.item); }
		c.inbox.push(m);
		for (const w of c.waiters.splice(0)) w();
	});
	c.open = new Promise((r) => c.ws.once('open', r));
	c.send = (m) => c.ws.send(JSON.stringify(m));
	c.next = async (t, ms = 5000) => {
		const end = Date.now() + ms;
		for (;;) {
			const i = c.inbox.findIndex((m) => m.t === t);
			if (i >= 0) return c.inbox.splice(i, 1)[0];
			if (Date.now() > end) throw new Error(`timeout waiting for ${t}`);
			await new Promise((r) => { c.waiters.push(r); setTimeout(r, 50); });
		}
	};
	return c;
}

function randomAnswer(r) {
	switch (r.type) {
	case REQ.DUEL_MENU: return [0, 0, 1, 1, 3, 4, 5, 5][rnd(8)];
	case REQ.YES_NO: return rnd(4) ? 1 : 0;
	case REQ.CARD_LIST: return !r.numItems || rnd(8) === 0 ? TCG_CANCEL : rnd(r.numItems);
	case REQ.CARD_ITEM_MENU: return rnd(10) === 0 ? 1 : 0;
	case REQ.PLAY_AREA: return r.arg[1] || rnd(10) === 0 ? TCG_CANCEL : r.arg[0] + rnd(r.numItems || 1);
	case REQ.PRIZE: return rnd(6);
	default: return !r.numItems || rnd(10) === 0 ? TCG_CANCEL : rnd(r.numItems);
	}
}

try {
	const host = client(), guest = client();
	await Promise.all([host.open, guest.open]);

	host.send({ t: 'create', name: 'HOST', deck: { name: 'bad', cards: [1, 2, 3] }, mode: 'server' });
	check(/deck/i.test((await host.next('error')).msg), 'a deck that is not 60 cards is refused');
	const illegal = deck(C.CHARMANDER_AND_FRIENDS_DECK_ID);
	illegal.cards = illegal.cards.map(() => illegal.cards.find((id) => statics.card(id).isPokemon && statics.card(id).stage === 0));
	host.send({ t: 'create', name: 'HOST', deck: illegal, mode: 'server' });
	check(/More than 4/.test((await host.next('error')).msg), 'a deck that breaks the deck rules is refused');

	host.send({ t: 'create', name: 'HOST', deck: deck(C.CHARMANDER_AND_FRIENDS_DECK_ID), mode: 'server' });
	const created = await host.next('created');
	check(created.mode === 'server' && /^[A-Z]{4}$/.test(created.room), `server room ${created.room} created`);
	guest.send({ t: 'join', room: created.room, name: 'GUEST', deck: deck(C.SQUIRTLE_AND_FRIENDS_DECK_ID) });
	const joined = await guest.next('joined');
	const peerJoined = await host.next('peer-joined');
	check(joined.mode === 'server' && !joined.host.deck && !peerJoined.deck, 'no deck list is passed to the other player');
	const [hs, gs] = [await host.next('start'), await guest.next('start')];
	check(hs.role === 'host' && gs.role === 'guest' && hs.names.join() === 'HOST,GUEST', 'both players are told the duel started');
	guest.send({ t: 'setup', setup: {} });
	check(/setup/.test((await guest.next('error')).msg), 'clients cannot send a setup');

	const players = [host, guest];
	const done = [0, 0];       // items handled per player
	let answers = 0, wrongChecked = false, reconnected = false;
	const end = Date.now() + 120000;
	while (Date.now() < end) {
		let progressed = false;
		for (const v of [0, 1]) {
			const p = players[v];
			while (done[v] < p.items.length) {
				const item = p.items[done[v]++];
				progressed = true;
				if (item.k === 'end') p.end = item;
				if (item.k !== 'req' || !item.act || item.v !== undefined) continue;
				if (!wrongChecked) {
					players[1 - v].send({ t: 'answer', id: item.id, v: 0 });
					check((await players[1 - v].next('error')).msg === 'not your request', 'the other player cannot answer a request');
					wrongChecked = true;
				}
				p.send({ t: 'answer', id: item.id, v: randomAnswer(item.req) });
				answers++;
			}
		}
		if (host.end && guest.end) break;
		if (!reconnected && answers >= 40) {
			// the guest's connection drops; the host keeps playing until it waits on the guest
			reconnected = true;
			const token = joined.token, had = guest.items.length;
			guest.ws.terminate();
			check((await host.next('peer-left')).t === 'peer-left', 'the host is told the guest lost the connection');
			const back = client();
			await back.open;
			back.items = guest.items.slice();
			back.send({ t: 'rejoin', room: created.room, token, from: had });
			const rj = await back.next('rejoined');
			check(rj.mode === 'server' && rj.items.every((it, i) => it.n === had + i), `rejoin resends items from ${had} (${rj.items.length} missed)`);
			for (const it of rj.items) if (it.n === back.items.length) back.items.push(it);
			check((await host.next('peer-back')).t === 'peer-back', 'the host is told the guest is back');
			players[1] = back;
		}
		if (!progressed) await new Promise((r) => setTimeout(r, 5));
	}
	const g = players[1];
	check(host.end && g.end && host.end.result === g.end.result && host.end.turns === g.end.turns, `duel finished for both players (${host.end?.turns} turns, ${answers} answers)`);

	// a reloaded page: the whole stream from item 0 with the answers filled in
	const reload = client();
	await reload.open;
	reload.send({ t: 'rejoin', room: created.room, token: joined.token, from: 0 });
	const full = await reload.next('rejoined');
	const same = full.items.length === g.items.length && full.items.every((it, i) => it.n === i && it.k === g.items[i].k);
	check(same, `rejoin from 0 replays all ${full.items.length} items`);
	check(full.items.filter((it) => it.k === 'req' && it.act).every((it) => it.v !== undefined), 'replayed requests carry their answers');
	const wire = JSON.stringify(full.items);
	check(!/"(seed|rng|setup|cards)"/.test(wire), 'the stream holds no setup, seed or deck lists');
	reload.send({ t: 'leave' });
	host.send({ t: 'leave' });
	for (const c of [host, g, reload]) c.ws.close();
} catch (e) {
	check(false, e.stack || String(e));
} finally {
	proc.kill();
}
console.log(failed ? 'FAILED' : 'PASS');
process.exit(failed ? 1 : 0);
