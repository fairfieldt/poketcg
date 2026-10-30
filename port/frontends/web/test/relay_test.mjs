// Protocol test of the relay server (no browser): rooms, setup relay, answer sequencing
// (gaps rejected, duplicates tolerated), rejoin with the answer log, peer notifications.
//
//   node test/relay_test.mjs [--port 8132]
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const require = createRequire(path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'server', 'server.js'));
const WebSocket = require('ws');
const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const PORT = Number(args.includes('--port') ? args[args.indexOf('--port') + 1] : 8132);
let failed = false;
const check = (c, m) => { console.log(`  ${c ? 'ok' : 'FAIL'}: ${m}`); if (!c) failed = true; };

const proc = spawn(process.execPath, [path.join(here, '..', 'server', 'server.js'), '--port', String(PORT)], { env: { ...process.env, QUIET: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
await new Promise((r) => proc.stdout.once('data', r));

function client() {
	const ws = new WebSocket(`ws://localhost:${PORT}/ws`);
	const inbox = [];
	const waiters = [];
	ws.on('message', (d) => {
		const m = JSON.parse(d);
		const i = waiters.findIndex((w) => w.t === m.t);
		if (i >= 0) waiters.splice(i, 1)[0].resolve(m); else inbox.push(m);
	});
	return {
		ws,
		open: () => new Promise((r) => ws.once('open', r)),
		send: (m) => ws.send(JSON.stringify(m)),
		next: (t, ms = 2000) => {
			const i = inbox.findIndex((m) => m.t === t);
			if (i >= 0) return Promise.resolve(inbox.splice(i, 1)[0]);
			return new Promise((resolve, reject) => {
				waiters.push({ t, resolve });
				setTimeout(() => reject(new Error(`timeout waiting for ${t}`)), ms);
			});
		},
		close: () => ws.close(),
		pending: (t) => inbox.filter((m) => m.t === t).length,
	};
}

try {
	const host = client(), guest = client();
	await Promise.all([host.open(), guest.open()]);
	host.send({ t: 'create', name: 'H', deck: { name: 'd', cards: [] } });
	const created = await host.next('created');
	check(/^[A-Z]{4}$/.test(created.room), `room ${created.room} created`);
	guest.send({ t: 'join', room: created.room.toLowerCase(), name: 'G', deck: { cards: [1, 2] } });
	const joined = await guest.next('joined');
	const pj = await host.next('peer-joined');
	check(joined.role === 'guest' && pj.name === 'G' && pj.deck.cards.length === 2, 'guest joined, host told with the deck');
	host.send({ t: 'setup', setup: { seed: 7 } });
	check((await guest.next('setup')).setup.seed === 7, 'setup relayed to the guest');
	host.send({ t: 'answer', seq: 0, rt: 2, v: 1, h: 123 });
	const a0 = await guest.next('answer');
	check(a0.seq === 0 && a0.v === 1 && a0.h === 123, 'answer relayed');
	guest.send({ t: 'answer', seq: 1, rt: 3, v: 4, h: 5 });
	check((await host.next('answer')).seq === 1, 'answer relayed the other way');
	guest.send({ t: 'answer', seq: 3, rt: 3, v: 4, h: 5 });
	check(/sequence gap/.test((await guest.next('error')).msg), 'sequence gap rejected');
	guest.send({ t: 'answer', seq: 1, rt: 3, v: 4, h: 5 });
	await new Promise((r) => setTimeout(r, 100));
	host.send({ t: 'ping' });
	await host.next('pong');
	check(host.pending('answer') === 0, 'duplicate answer ignored (not relayed twice)');
	guest.close();
	await host.next('peer-left');
	check(true, 'host told the guest disconnected');
	host.send({ t: 'answer', seq: 2, rt: 2, v: 5, h: 9 });
	const g2 = client();
	await g2.open();
	g2.send({ t: 'rejoin', room: created.room, token: joined.token });
	const rj = await g2.next('rejoined');
	check(rj.role === 'guest' && rj.setup.seed === 7 && rj.log.length === 3 && rj.log[2].v === 5, 'rejoin returns setup + full log (incl. answers sent while away)');
	await host.next('peer-back');
	check(true, 'host told the guest is back');
	g2.send({ t: 'rejoin', room: created.room, token: 'nope' });
	check(/cannot rejoin/.test((await g2.next('error')).msg), 'bad token rejected');
	host.send({ t: 'leave' });
	await g2.next('peer-quit');
	check(true, 'guest told the host left');
	const x = client();
	await x.open();
	x.send({ t: 'join', room: 'ZZZZ', name: 'X' });
	check(/no such room/.test((await x.next('error')).msg), 'joining an unknown room fails');
	host.close(); g2.close(); x.close();
} catch (e) {
	check(false, e.message);
}
proc.kill();
console.log(failed ? 'RELAY TEST FAILED' : 'RELAY TEST PASSED');
process.exit(failed ? 1 : 0);
