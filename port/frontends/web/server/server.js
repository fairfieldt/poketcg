// Relay server for online duels + static file server for the web frontend (dist/).
//
//   node server.js [--port 8080] [--dist ../dist]      (env: PORT, DIST)
//
// Both clients run the same deterministic engine with the same TcgDuelSetup and only exchange
// the answers to request(). The server knows nothing about the game: it pairs two clients in a
// room, forwards the setup and the answers, and keeps a log of them so a client that lost its
// connection (or reloaded the page) can rejoin and replay the duel up to the current point.
//
// Protocol (JSON text frames on /ws):
//   client -> server
//     {t:'create', name, deck}            create a room; the creator is the host (TCG_PLAYER side)
//     {t:'join', room, name, deck}        join as guest (TCG_OPPONENT side)
//     {t:'rejoin', room, token}           re-attach after a disconnect
//     {t:'setup', setup}                  host only: the duel setup, relayed to the guest
//     {t:'answer', seq, rt, v, h}         answer number seq (0-based count of the requests that are
//                                         not ACKs or view-only card lists),
//                                         request type rt, value v, state hash h; relayed + logged
//     {t:'desync', seq, detail}           a peer detected a desync; relayed
//     {t:'leave'}                         leave the room for good
//     {t:'ping'}
//   server -> client
//     {t:'created', room, token, role:'host'}
//     {t:'joined', room, token, role:'guest', host:{name, deck}}
//     {t:'peer-joined', name, deck}       (to the host)
//     {t:'rejoined', room, role, setup, log, peer:{name, deck, connected}}
//     {t:'setup', setup} {t:'answer', ...} {t:'desync', ...}   relayed from the peer
//     {t:'peer-left'} {t:'peer-back'} {t:'peer-quit'}
//     {t:'error', msg} {t:'pong'}
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { WebSocketServer } = require('ws');

const argv = process.argv.slice(2);
const arg = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const PORT = Number(arg('--port', process.env.PORT || 8080));
const DIST = path.resolve(arg('--dist', process.env.DIST || path.join(__dirname, '..', 'dist')));
const ROOM_TTL_MS = 30 * 60 * 1000;
const MAX_LOG = 200000;

const MIME = {
	'.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
	'.css': 'text/css; charset=utf-8', '.json': 'application/json', '.wasm': 'application/wasm', '.png': 'image/png',
	'.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.txt': 'text/plain; charset=utf-8', '.ogg': 'audio/ogg',
};

const server = http.createServer((req, res) => {
	let url, rel;
	try {
		url = new URL(req.url, 'http://x');
		rel = decodeURIComponent(url.pathname);
		if (rel.includes('\0')) throw new URIError('NUL path');
	} catch { res.writeHead(400).end(); return; }
	if (url.pathname === '/health') { res.writeHead(200, { 'content-type': 'text/plain' }).end('ok'); return; }
	if (rel.endsWith('/')) rel += 'index.html';
	const file = path.resolve(DIST, '.' + rel);
	if (!file.startsWith(DIST + path.sep) && file !== DIST) { res.writeHead(403).end(); return; }
	fs.stat(file, (err, st) => {
		if (err || !st.isFile()) { res.writeHead(404, { 'content-type': 'text/plain' }).end('not found'); return; }
		res.writeHead(200, {
			'content-type': MIME[path.extname(file).toLowerCase()] || 'application/octet-stream',
			'content-length': st.size, 'cache-control': 'no-cache',
		});
		if (req.method === 'HEAD') { res.end(); return; }
		fs.createReadStream(file).pipe(res);
	});
});

/** rooms: code -> { code, host, guest, setup, log, touched } ; host/guest = { ws, token, name, deck } */
const rooms = new Map();

function newCode() {
	const letters = 'ABCDEFGHJKLMNPQRSTUVWXYZ';
	for (;;) {
		let c = '';
		for (let i = 0; i < 4; i++) c += letters[crypto.randomInt(letters.length)];
		if (!rooms.has(c)) return c;
	}
}

function send(ws, msg) {
	if (ws && ws.readyState === 1) ws.send(JSON.stringify(msg));
}

function peerOf(room, role) { return role === 'host' ? room.guest : room.host; }

const wss = new WebSocketServer({ server, path: '/ws', maxPayload: 256 * 1024 });

wss.on('connection', (ws) => {
	// ws closes invalid/oversized frames; contain its error event to this peer.
	ws.on('error', () => {});
	let room = null, role = null; // what this socket is attached to
	const attach = (r, ro) => { room = r; role = ro; r[ro].ws = ws; r.touched = Date.now(); };
	const fail = (msg) => send(ws, { t: 'error', msg });

	ws.on('message', (data) => {
		let m;
		try { m = JSON.parse(data); } catch { fail('bad json'); return; }
		if (!m || typeof m.t !== 'string') { fail('bad message'); return; }
		if (room) room.touched = Date.now();
		switch (m.t) {
		case 'ping':
			send(ws, { t: 'pong' });
			break;
		case 'create': {
			if (room) { fail('already in a room'); return; }
			const r = { code: newCode(), host: null, guest: null, setup: null, log: [], touched: Date.now() };
			r.host = { ws, token: crypto.randomBytes(16).toString('hex'), name: String(m.name || 'Host').slice(0, 24), deck: m.deck || null };
			rooms.set(r.code, r);
			attach(r, 'host');
			send(ws, { t: 'created', room: r.code, token: r.host.token, role: 'host' });
			log(`room ${r.code} created by ${r.host.name}`);
			break;
		}
		case 'join': {
			if (room) { fail('already in a room'); return; }
			const r = rooms.get(String(m.room || '').toUpperCase());
			if (!r) { fail('no such room'); return; }
			if (r.guest) { fail('the room is full'); return; }
			r.guest = { ws, token: crypto.randomBytes(16).toString('hex'), name: String(m.name || 'Guest').slice(0, 24), deck: m.deck || null };
			attach(r, 'guest');
			send(ws, { t: 'joined', room: r.code, token: r.guest.token, role: 'guest', host: { name: r.host.name, deck: r.host.deck } });
			send(r.host.ws, { t: 'peer-joined', name: r.guest.name, deck: r.guest.deck });
			log(`room ${r.code}: ${r.guest.name} joined`);
			break;
		}
		case 'rejoin': {
			const r = rooms.get(String(m.room || '').toUpperCase());
			const ro = r && ['host', 'guest'].find((k) => r[k] && r[k].token === m.token);
			if (!ro) { fail('cannot rejoin: unknown room or token'); return; }
			if (r[ro].ws && r[ro].ws !== ws) { try { r[ro].ws.close(4000, 'replaced'); } catch { /* ignore */ } }
			attach(r, ro);
			const p = peerOf(r, ro);
			send(ws, {
				t: 'rejoined', room: r.code, role: ro, setup: r.setup, log: r.log,
				peer: p ? { name: p.name, deck: p.deck, connected: !!(p.ws && p.ws.readyState === 1) } : null,
			});
			if (p && p.left) send(ws, { t: 'peer-quit' });
			else if (p) send(p.ws, { t: 'peer-back' });
			log(`room ${r.code}: ${ro} rejoined (log ${r.log.length})`);
			break;
		}
		case 'setup':
			if (!room || role !== 'host') { fail('only the host sends the setup'); return; }
			room.setup = m.setup;
			room.log = [];
			send(room.guest && room.guest.ws, { t: 'setup', setup: m.setup });
			break;
		case 'answer': {
			if (!room || !room.setup) { fail('no duel in progress'); return; }
			const seq = m.seq;
			if (!Number.isSafeInteger(seq) || seq < 0) { fail('invalid answer sequence'); return; }
			const entry = { t: 'answer', seq, rt: m.rt | 0, v: m.v | 0, h: m.h >>> 0, from: role };
			if (seq < room.log.length) {
				const e = room.log[seq];
				if (e.v !== entry.v || e.rt !== entry.rt || e.from !== role) fail(`answer ${seq} conflicts with the log`);
				return; // duplicate (resent after a reconnect)
			}
			if (seq !== room.log.length) { fail(`sequence gap: got ${seq}, expected ${room.log.length}`); return; }
			if (room.log.length >= MAX_LOG) { fail('duel too long'); return; }
			room.log.push(entry);
			const p = peerOf(room, role);
			send(p && p.ws, entry);
			break;
		}
		case 'desync': {
			if (!room) return;
			const p = peerOf(room, role);
			send(p && p.ws, { t: 'desync', seq: m.seq, detail: String(m.detail || '').slice(0, 200) });
			log(`room ${room.code}: desync reported by ${role} at ${m.seq}: ${m.detail}`);
			break;
		}
		case 'leave': {
			if (!room) return;
			const p = peerOf(room, role);
			send(p && p.ws, { t: 'peer-quit' });
			room[role].ws = null;
			room[role].left = true;
			if (!p || p.left) rooms.delete(room.code); // else kept until the TTL: a reconnecting peer learns that we left
			room = null;
			role = null;
			break;
		}
		default:
			fail(`unknown message ${m.t}`);
		}
	});

	ws.on('close', () => {
		if (!room || !room[role] || room[role].ws !== ws) return;
		room[role].ws = null;
		const p = peerOf(room, role);
		send(p && p.ws, { t: 'peer-left' });
		room.touched = Date.now();
		log(`room ${room.code}: ${role} disconnected`);
	});
});

setInterval(() => {
	const now = Date.now();
	for (const [code, r] of rooms) {
		const live = ['host', 'guest'].some((k) => r[k] && r[k].ws && r[k].ws.readyState === 1);
		if (!live && now - r.touched > ROOM_TTL_MS) rooms.delete(code);
	}
}, 60 * 1000).unref();

function log(msg) {
	if (!process.env.QUIET) console.log(`[relay] ${msg}`);
}

server.listen(PORT, () => {
	console.log(`[relay] serving ${DIST} on http://localhost:${PORT}/ (WebSocket relay on /ws)`);
	if (!fs.existsSync(path.join(DIST, 'index.html'))) console.log('[relay] warning: dist/ is not built yet (run ../build.sh)');
});
