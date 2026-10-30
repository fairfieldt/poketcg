// Deck catalog (pre-constructed decks from the engine's DeckPointers), custom deck storage,
// deck rules (replicating the original deck machine) and deck codes.
import { C, T } from './engine.js';
import { NPC_DECKS } from './gen/assets.js';

const STARTERS = ['CHARMANDER_AND_FRIENDS_DECK_ID', 'SQUIRTLE_AND_FRIENDS_DECK_ID', 'BULBASAUR_AND_FRIENDS_DECK_ID'];
const STORAGE_KEY = 'tcg.customDecks';

/* every pre-constructed deck: the 3 starter decks and every AI/club deck (*_DECK = *_DECK_ID + 2) */
export function preconstructedDecks(duel) {
	const out = [];
	const starterIds = STARTERS.map((n) => C[n]);
	for (let deckId = 0; deckId + 2 < duel.numDecks; deckId++) {
		const label = duel.deckList(deckId + 2).label;
		if (/Extra|Practice/.test(label) && deckId !== C.SAMS_NORMAL_DECK_ID) continue;
		if (deckId === C.PRACTICE_PLAYER_DECK_ID) continue;
		const list = duel.deckList(deckId + 2);
		const npc = NPC_DECKS[deckId];
		out.push({
			key: `pre:${deckId}`, deckId, cards: list.cards.slice(),
			name: duel.text(list.nameText),
			npc: npc ? duel.text(T[npc.name]) : '',
			theme: npc ? npc.theme : 0,
			kind: starterIds.includes(deckId) ? 'starter' : 'club',
		});
	}
	return out;
}

export function loadCustomDecks() {
	try {
		const v = JSON.parse(localStorage.getItem(STORAGE_KEY) || '[]');
		return Array.isArray(v) ? v.filter((d) => d && Array.isArray(d.cards) && typeof d.name === 'string') : [];
	} catch {
		return [];
	}
}

export function saveCustomDecks(decks) {
	try { localStorage.setItem(STORAGE_KEY, JSON.stringify(decks)); return true; } catch { return false; }
}

export function upsertCustomDeck(deck) {
	const decks = loadCustomDecks();
	const i = decks.findIndex((d) => d.id === deck.id);
	if (i >= 0) decks[i] = deck; else decks.push(deck);
	saveCustomDecks(decks);
	return decks;
}

export function deleteCustomDeck(id) {
	const decks = loadCustomDecks().filter((d) => d.id !== id);
	saveCustomDecks(decks);
	return decks;
}

export function allDecks(duel) {
	const custom = loadCustomDecks().map((d) => ({ ...d, key: `custom:${d.id}`, kind: 'custom' }));
	return [...custom, ...preconstructedDecks(duel)];
}

// ---------------------------------------------------------------- rules (engine/menus/deck_configuration.asm)

/* basic energy cards have no same-name limit; Double Colorless Energy has (TryAddCardToDeck) */
function isUnlimited(card) {
	const t = card.type;
	if (t === C.TYPE_ENERGY_DOUBLE_COLORLESS) return false;
	return (t & C.TYPE_ENERGY) === C.TYPE_ENERGY;
}

export function sameNameCount(cards, card, cardFn) {
	return cards.filter((id) => id && cardFn(id).nameText === card.nameText).length;
}

/* TryAddCardToDeck: refuse if the deck is full or the same-name limit (MAX_NUM_SAME_NAME_CARDS) is reached.
 * returns null if the card can be added, else the reason. */
export function cannotAdd(cards, id, cardFn) {
	const n = cards.filter(Boolean).length;
	if (n >= C.DECK_SIZE) return `The deck already has ${C.DECK_SIZE} cards.`;
	const card = cardFn(id);
	if (!isUnlimited(card) && sameNameCount(cards, card, cardFn) >= C.MAX_NUM_SAME_NAME_CARDS)
		return `You can't add more than ${C.MAX_NUM_SAME_NAME_CARDS} cards named ${card.name}.`;
	return null;
}

/* CheckIfThereAreAnyBasicCardsInDeck: type has no TYPE_ENERGY bit and the stage byte is 0 */
export function hasBasicPokemon(cards, cardFn) {
	return cards.some((id) => {
		if (!id) return false;
		const c = cardFn(id);
		return (c.type & C.TYPE_ENERGY) === 0 && c.raw[C.CARD_DATA_STAGE] === 0;
	});
}

/* SaveDeckConfiguration: exactly 60 cards and at least one Basic Pokemon (plus the same-name limit,
 * which the original enforces while adding cards) */
export function validateDeck(cards, cardFn) {
	const errors = [];
	const list = cards.filter(Boolean);
	if (list.length !== C.DECK_SIZE) errors.push(`The deck must include ${C.DECK_SIZE} cards (it has ${list.length}).`);
	if (!hasBasicPokemon(list, cardFn)) errors.push('There are no Basic Pokémon in this deck. You must include a Basic Pokémon in the deck.');
	const seen = new Map();
	for (const id of list) {
		const c = cardFn(id);
		if (isUnlimited(c)) continue;
		const k = c.nameText;
		seen.set(k, (seen.get(k) || 0) + 1);
		if (seen.get(k) === C.MAX_NUM_SAME_NAME_CARDS + 1) errors.push(`More than ${C.MAX_NUM_SAME_NAME_CARDS} cards named ${c.name}.`);
	}
	return errors;
}

export function sortCards(cards) {
	return cards.filter(Boolean).sort((a, b) => a - b);
}

// ---------------------------------------------------------------- deck codes
// TCG1-<base64url(name UTF-8, 0, (count, card id) pairs, checksum)>

function b64url(bytes) {
	let s = '';
	for (const b of bytes) s += String.fromCharCode(b);
	return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function unb64url(str) {
	const s = atob(str.replace(/-/g, '+').replace(/_/g, '/'));
	return Uint8Array.from(s, (ch) => ch.charCodeAt(0));
}

export function encodeDeck(name, cards) {
	const counts = new Map();
	for (const id of sortCards(cards)) counts.set(id, (counts.get(id) || 0) + 1);
	const bytes = [...new TextEncoder().encode(String(name).slice(0, 24)), 0];
	for (const [id, n] of counts) bytes.push(n, id);
	let sum = 0;
	for (const b of bytes) sum = (sum + b) & 0xff;
	bytes.push(sum);
	return 'TCG1-' + b64url(bytes);
}

export function decodeDeck(code, numCards) {
	const m = /^TCG1-([A-Za-z0-9_-]+)$/.exec(String(code).trim());
	if (!m) throw new Error('not a deck code (expected TCG1-...)');
	const bytes = unb64url(m[1]);
	let sum = 0;
	for (let i = 0; i < bytes.length - 1; i++) sum = (sum + bytes[i]) & 0xff;
	if (sum !== bytes[bytes.length - 1]) throw new Error('corrupted deck code (bad checksum)');
	const z = bytes.indexOf(0);
	if (z < 0) throw new Error('corrupted deck code');
	const name = new TextDecoder().decode(bytes.subarray(0, z));
	const cards = [];
	for (let i = z + 1; i + 1 < bytes.length - 1; i += 2) {
		const n = bytes[i], id = bytes[i + 1];
		if (!id || id > numCards) throw new Error(`invalid card id ${id}`);
		for (let k = 0; k < n; k++) cards.push(id);
	}
	if (cards.length > C.DECK_SIZE) throw new Error('too many cards');
	return { name, cards };
}

/* the main colors of a deck (for icons), most frequent first */
export function deckColors(cards, cardFn) {
	const n = new Map();
	for (const id of cards) {
		const c = cardFn(id);
		if (!c.isPokemon) continue;
		n.set(c.type & 7, (n.get(c.type & 7) || 0) + 1);
	}
	return [...n.entries()].sort((a, b) => b[1] - a[1]).slice(0, 3).map((e) => e[0]);
}
