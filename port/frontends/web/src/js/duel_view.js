// What one duelist may see of a duel the server runs (authoritative online mode), and the
// client-side mirror built from it. Shared by the server (server/authority.mjs) and the browser.
//
// The server runs the only engine. Each duelist receives a stream of items (see authority.mjs):
// engine events and requests, each with the changes to that duelist's redacted copy of the
// board since the previous item:
//   s: [side, offset, value, ...]   duel variable bytes (side 0 = TCG_PLAYER, 1 = TCG_OPPONENT)
//   c: [side, deckIndex, cardId, ...] card identities the duelist may now know
//   l: [deckIndex, ...]             wDuelTempList shown by a TCG_SCREEN_CARD_LIST event
// The redacted copy never holds deck order, prize identities, the opponent's hand order or
// (until the first turn starts) the opponent's face-down setup. Card identities are sent only
// for cards the duelist can see: both play areas (after setup), both discard piles, their own
// hand, and cards an event or request shows them.
import { C, W, TCG_PLAYER, TCG_OPPONENT, EV, SCREEN } from './engine.js';

export const sideIndex = (d) => (d === TCG_OPPONENT ? 1 : 0);
export const sideOf = (i) => (i ? TCG_OPPONENT : TCG_PLAYER);

const LOC_HAND = C.CARD_LOCATION_HAND, LOC_DISCARD = C.CARD_LOCATION_DISCARD_PILE;
const LOC_PLAY_AREA = C.CARD_LOCATION_PLAY_AREA, LOC_JUST_DRAWN = C.CARD_LOCATION_JUST_DRAWN;
// per-slot play area fields (arena + 5 bench, arena status), cleared while the opponent's setup
// is face down; the counts (prizes, hand, discard pile) stay
const SLOT_FIELDS = [
	[C.DUELVARS_ARENA_CARD_FLAGS, C.DUELVARS_ARENA_CARD_SUBSTATUS3 + 1],
	[C.DUELVARS_ARENA_CARD_STATUS, C.DUELVARS_ARENA_CARD_STATUS + 1],
	[C.DUELVARS_ARENA_CARD_DISABLED_ATTACK_INDEX, C.DUELVARS_ARENA_CARD_LAST_TURN_EFFECT + 1],
];

/* can the duelist on side `viewer` see the identity of a card at `loc` on side `side`? */
export function locationVisible(loc, own, revealed) {
	if (loc & LOC_PLAY_AREA) return own || revealed;
	if (loc === LOC_DISCARD) return true;
	if (loc === LOC_HAND || (loc & LOC_JUST_DRAWN)) return own;
	return false;
}

/* the duel variables of `side` as `viewer` may see them (both side indexes 0/1) */
export function redactDuelvars(dv, own, revealed) {
	const out = Uint8Array.from(dv);
	out.fill(0xff, C.DUELVARS_PRIZE_CARDS, C.DUELVARS_PRIZE_CARDS + 6);
	out.fill(0xff, C.DUELVARS_DECK_CARDS, C.DUELVARS_DECK_CARDS + C.DECK_SIZE);
	if (!own) out.fill(0xff, C.DUELVARS_HAND, C.DUELVARS_HAND + C.DECK_SIZE);
	if (!own && !revealed) {
		for (const [a, b] of SLOT_FIELDS) out.fill(0, a, b);
		out.fill(0xff, C.DUELVARS_ARENA_CARD, C.DUELVARS_BENCH + 5);
		out[C.DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA] = 0;
		for (let j = 0; j < C.DECK_SIZE; j++) if (out[j] & LOC_PLAY_AREA) out[j] = LOC_HAND;
	}
	return out;
}

/** The live-state half of engine.Duel for a duel the server runs: what DuelUI reads. */
export class RemoteDuel {
	constructor(statics) {
		this.statics = statics; // a loaded engine.Duel: static card data and texts only
		this.dv = [new Uint8Array(256), new Uint8Array(256)];
		this.ids = [new Uint16Array(C.DECK_SIZE), new Uint16Array(C.DECK_SIZE)];
		this.tempList = new Uint8Array(0x2000).fill(0xff);
	}

	card(id) {
		if (id) return this.statics.card(id);
		return { id: 0, type: C.TYPE_TRAINER, name: '?', nameText: 0, isPokemon: false, isEnergy: false, isTrainer: true, attacks: [], description: '' };
	}
	text(id) { return this.statics.text(id); }
	cards() { return this.statics.cards(); }
	duelvars(d) { return this.dv[sideIndex(d)]; }
	cardId(d, idx) { return idx < C.DECK_SIZE ? this.ids[sideIndex(d)][idx] : 0; }
	/* only wDuelTempList is meaningful (TCG_SCREEN_CARD_LIST) */
	wram() { return this.tempList; }

	apply(item) {
		const s = item.s, c = item.c;
		if (s) for (let i = 0; i + 2 < s.length; i += 3) this.dv[s[i] & 1][s[i + 1] & 0xff] = s[i + 2];
		if (c) for (let i = 0; i + 2 < c.length; i += 3) if (c[i + 1] < C.DECK_SIZE) this.ids[c[i] & 1][c[i + 1]] = c[i + 2];
		if (item.l) {
			const off = W.wDuelTempList - 0xc000;
			this.tempList.fill(0xff, off, off + C.DECK_SIZE + 1);
			this.tempList.set(item.l.slice(0, C.DECK_SIZE), off);
		}
	}
}

export const PUBLIC_SIGNALS = new Set([EV.COIN, EV.DAMAGE, EV.TURN, EV.DUEL_END, EV.ANIM]);
export const isBoxMessage = (ev) => ev.type === EV.SCREEN && ev.arg[0] === SCREEN.BOX_MESSAGE;
