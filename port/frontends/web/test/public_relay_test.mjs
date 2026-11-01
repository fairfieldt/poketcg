// Public relay regressions: bad paths, sequences and frames stay peer-local.
// Run: node port/frontends/web/test/public_relay_test.mjs
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(web, 'server/server.js'));
const WebSocket = require('ws');
const fixture = await mkdtemp(path.join(os.tmpdir(), 'tcg-public-relay-'));
await mkdir(path.join(fixture, 'audio'));
await writeFile(path.join(fixture, 'index.html'), '<!doctype html><title>Test</title>');
await writeFile(path.join(fixture, 'audio/test.ogg'), 'OggS');

const reservation = net.createServer();
reservation.listen(0, '127.0.0.1');
await once(reservation, 'listening');
const port = reservation.address().port;
await new Promise((resolve) => reservation.close(resolve));
const proc = spawn(process.execPath, [path.join(web, 'server/server.js'), '--port', String(port), '--dist', fixture],
	{ env: { ...process.env, QUIET: '1' }, stdio: ['ignore', 'pipe', 'pipe'] });
let stderr = '';
proc.stderr.on('data', (data) => { stderr += data; });
const sockets = new Set();

async function bounded(promise) {
	let timer;
	try {
		return await Promise.race([promise, new Promise((_, reject) => {
			timer = setTimeout(() => reject(new Error(`relay timed out: ${stderr}`)), 3000);
		})]);
	} finally { clearTimeout(timer); }
}

function request(urlPath) {
	return bounded(new Promise((resolve, reject) => {
		const req = http.get({ hostname: '127.0.0.1', port, path: urlPath }, (res) => {
			let body = '';
			res.setEncoding('utf8');
			res.on('data', (data) => { body += data; });
			res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body }));
		});
		req.on('error', reject);
	}));
}

async function client() {
	const ws = new WebSocket(`ws://127.0.0.1:${port}/ws`);
	sockets.add(ws);
	ws.on('error', () => {});
	ws.on('close', () => sockets.delete(ws));
	const inbox = [], waiters = [];
	ws.on('message', (data) => {
		const message = JSON.parse(data);
		const index = waiters.findIndex((waiter) => waiter.type === message.t);
		if (index >= 0) waiters.splice(index, 1)[0].resolve(message);
		else inbox.push(message);
	});
	await bounded(once(ws, 'open'));
	return {
		ws,
		send: (message) => ws.send(JSON.stringify(message)),
		next: (type) => {
			const index = inbox.findIndex((message) => message.t === type);
			if (index >= 0) return Promise.resolve(inbox.splice(index, 1)[0]);
			return bounded(new Promise((resolve) => waiters.push({ type, resolve })));
		},
	};
}

async function healthy(label) {
	assert.equal(proc.exitCode, null, `${label}: process remains alive (${stderr})`);
	const response = await request('/health');
	assert.equal(response.status, 200);
	assert.equal(response.body, 'ok');
	const peer = await client();
	peer.send({ t: 'ping' });
	assert.equal((await peer.next('pong')).t, 'pong');
	peer.ws.terminate();
	console.log(`  PASS: ${label}; later HTTP and WebSocket requests succeed`);
}

try {
	await bounded(once(proc.stdout, 'data'));
	for (const badPath of ['/bad%ZZ', '/bad%', '/%C3%28', '/audio/%00.ogg']) {
		assert.equal((await request(badPath)).status, 400, badPath);
	}
	await healthy('malformed-percent, invalid UTF-8 and NUL paths rejected');
	assert.equal((await request('/audio/test.ogg')).headers['content-type'], 'audio/ogg');
	console.log('  PASS: Vorbis assets use audio/ogg');

	const host = await client();
	host.send({ t: 'create', name: 'H', deck: null });
	const created = await host.next('created');
	const guest = await client();
	guest.send({ t: 'join', room: created.room, name: 'G', deck: null });
	await guest.next('joined');
	await host.next('peer-joined');
	host.send({ t: 'setup', setup: { seed: 7 } });
	await guest.next('setup');
	for (const seq of [-1, -4294967296, 0.5, '0', null, 2 ** 53, 4294967295, 4294967296]) {
		host.send({ t: 'answer', seq, rt: 2, v: 1, h: 3 });
		assert.match((await host.next('error')).msg, /invalid answer sequence|sequence gap/);
	}
	host.send({ t: 'answer', seq: 0, rt: 2, v: 1, h: 3 });
	assert.equal((await guest.next('answer')).seq, 0, 'invalid sequences did not advance the log');
	host.send({ t: 'answer', seq: 0, rt: 2, v: 1, h: 3 });
	host.send({ t: 'ping' });
	await host.next('pong');
	guest.send({ t: 'answer', seq: 1, rt: 3, v: 4, h: 5 });
	assert.equal((await host.next('answer')).seq, 1, 'valid answers and duplicates still work');
	await healthy('negative, fractional, unsafe and wrapped sequences rejected');

	for (const [label, payload, closeCode] of [
		['oversized frame', 'x'.repeat(256 * 1024 + 1), 1009],
		['invalid UTF-8 text frame', Buffer.from([0xff]), 1007],
	]) {
		const peer = await client();
		const closed = bounded(once(peer.ws, 'close'));
		peer.ws.send(payload, { binary: false });
		assert.equal((await closed)[0], closeCode, label);
		await healthy(`${label} closes only its peer`);
	}
	assert.equal(stderr, '', 'server emitted no unhandled error');
	console.log('PUBLIC RELAY TEST PASSED');
} finally {
	for (const ws of sockets) ws.terminate();
	proc.kill();
	if (proc.exitCode === null) await once(proc, 'exit');
	await rm(fixture, { recursive: true, force: true });
}
