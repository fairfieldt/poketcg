// DOM helpers: card art from the generated sprite sheet, TX_SYMBOL tiles, rich text.
import { artSheet, artName } from './rom_art.js';
import { CARD_SHEET, NO_BUNDLED_ART } from './gen/assets.js';
import { C } from './engine.js';

export const COLOR_NAMES = ['Fire', 'Grass', 'Lightning', 'Water', 'Fighting', 'Psychic', 'Colorless'];
export const STATUS_NAMES = { [C.CONFUSED]: 'Confused', [C.ASLEEP]: 'Asleep', [C.PARALYZED]: 'Paralyzed' };

export function el(tag, cls, text) {
	const e = document.createElement(tag);
	if (cls) e.className = cls;
	if (text !== undefined && text !== null) e.textContent = text;
	return e;
}

export function clear(node) {
	while (node.firstChild) node.removeChild(node.firstChild);
	return node;
}

/* card art (64x48 GB pixels) of card id, `scale` times the current pixel unit (--u) */
export function art(id, scale = 1, cls = '') {
	const e = el('div', 'art' + (cls ? ' ' + cls : ''));
	if (NO_BUNDLED_ART) {
		const sheet = artSheet();
		e.style.backgroundImage = sheet ? `url(${sheet})` : 'none';
		e.classList.toggle('art-placeholder', !sheet);
		e.setAttribute('role', 'img');
		e.setAttribute('aria-label', artName(id));
		if (!sheet) e.textContent = artName(id);
	}
	e.style.setProperty('--col', id % CARD_SHEET.cols);
	e.style.setProperty('--row', Math.floor(id / CARD_SHEET.cols));
	e.style.setProperty('--sc', scale);
	return e;
}

/* a TX_SYMBOL tile (SYM_* index), 1em square */
export function sym(s, cls = '') {
	const e = el('span', 'sym' + (cls ? ' ' + cls : ''));
	if (NO_BUNDLED_ART) e.textContent = ({ 8: '☠', 9: 'Z', 10: '?', 11: '!', 13: 'P', 16: 'HP', 18: 'E', 20: '+', 21: 'D', 22: '●', 23: '○', 48: '◆' })[s] || '•';
	e.style.setProperty('--sx', s % 8);
	e.style.setProperty('--sy', s >> 3);
	return e;
}

/* colored energy symbol of a color (FIRE..COLORLESS = 0..6) */
export function esym(color) {
	const e = el('span', 'sym esym');
	e.style.setProperty('--sx', color);
	e.style.setProperty('--sy', 7);
	e.title = COLOR_NAMES[color] || '';
	if (NO_BUNDLED_ART) { e.textContent = ['F', 'G', 'L', 'W', 'F', 'P', 'C'][color] || '?'; e.style.color = ['#b83424', '#237523', '#946b00', '#245bb5', '#89451d', '#863da2', '#555'][color]; }
	return e;
}

/* the symbol shown next to a card name: the Pokemon's color, the energy's color, or T for trainers */
export function typeIcon(card) {
	if (!card || card.isTrainer) {
		const t = el('span', 'sym trainer-icon', 'T');
		t.title = 'Trainer';
		return t;
	}
	return esym(card.isPokemon ? card.type & 7 : card.type & 7);
}

const SYMBOL_TOKENS = {
	FIRE: 0, GRASS: 1, LIGHTNING: 2, WATER: 3, FIGHTING: 4, PSYCHIC: 5, COLORLESS: 6,
};

/* engine text -> DOM fragment: <FIRE> etc. become energy symbols, <Lv>/<No> plain text */
export function richText(str) {
	const frag = document.createDocumentFragment();
	if (!str) return frag;
	// eslint-disable-next-line no-control-regex
	const s = String(str).replace(/[\x00-\x09\x0b-\x1f]/g, '');
	const re = /<(\w+)>/g;
	let last = 0, m;
	const pushText = (t) => {
		const lines = t.split('\n');
		lines.forEach((line, i) => {
			if (i) frag.appendChild(el('br'));
			if (line) frag.appendChild(document.createTextNode(line));
		});
	};
	while ((m = re.exec(s))) {
		pushText(s.slice(last, m.index));
		const tok = m[1];
		if (tok in SYMBOL_TOKENS) frag.appendChild(esym(SYMBOL_TOKENS[tok]));
		else if (tok === 'Lv') frag.appendChild(document.createTextNode('Lv'));
		else if (tok === 'No') frag.appendChild(document.createTextNode('No.'));
		else frag.appendChild(document.createTextNode(m[0]));
		last = m.index + m[0].length;
	}
	pushText(s.slice(last));
	return frag;
}

export function plainText(str) {
	// eslint-disable-next-line no-control-regex
	return String(str || '').replace(/[\x00-\x09\x0b-\x1f]/g, '').replace(/<Lv>/g, 'Lv').replace(/<No>/g, 'No.').replace(/<(\w+)>/g, '$1');
}

/* Card rules were authored as narrow Game Boy lines. Reflow only these
 * descriptions, retaining paragraphs, tokens, punctuation and every word. */
export function reflowRules(str) {
	return String(str || '').replace(/-\n(?=[^\n])/g, '-').replace(/(?<!\n)\n(?!\n)/g, ' ');
}

/* "Name Lv12" for Pokemon, "Name" otherwise */
export function cardTitle(card) {
	return card.isPokemon && card.level ? `${card.name} Lv${card.level}` : card.name;
}

/* energy cost (7 counts, FIRE..COLORLESS) -> symbols */
export function costIcons(cost) {
	const frag = el('span', 'cost');
	cost.forEach((n, color) => { for (let i = 0; i < n; i++) frag.appendChild(esym(color)); });
	if (!frag.childNodes.length) frag.appendChild(el('span', 'nocost', '-'));
	return frag;
}

/* W/R bit field -> color list (WR_FIRE = $80 ... WR_PSYCHIC = $04) */
export function wrColors(wr) {
	const out = [];
	for (let c = 0; c < 6; c++) if (wr & (0x80 >> c)) out.push(c);
	return out;
}

export function setName(card) {
	const set = card.set >> 4;
	return ['Colosseum', 'Evolution', 'Mystery', 'Laboratory', 'Promotional', 'Energy'][set] || '?';
}

export function rarityName(card) {
	return { [C.CIRCLE]: '●', [C.DIAMOND]: '◆', [C.STAR]: '★', [C.PROMOSTAR]: 'Promo ★' }[card.rarity] || '';
}

export function stageName(card) {
	return ['Basic Pokémon', 'Stage 1', 'Stage 2', 'Stage 2'][card.stage] || '';
}

/* full card page (used by the duel's Check and by the deck builder) */
export function cardPage(card, extra = {}) {
	const page = el('div', 'card-page');
	const head = el('div', 'cp-head');
	head.appendChild(typeIcon(card));
	head.appendChild(el('span', 'cp-name', cardTitle(card)));
	if (card.isPokemon) head.appendChild(el('span', 'cp-hp', `HP ${extra.hp !== undefined ? extra.hp + '/' : ''}${card.hp}`));
	page.appendChild(head);
	const body = el('div', 'cp-body');
	const left = el('div', 'cp-left');
	left.appendChild(art(card.id, 2));
	const meta = el('div', 'cp-meta');
	if (card.isPokemon) {
		meta.appendChild(el('div', '', stageName(card) + (card.preEvoName ? ` (evolves from ${card.preEvoName})` : '')));
		if (card.category) meta.appendChild(el('div', '', `${plainText(card.category)} Pokémon`));
		meta.appendChild(el('div', '', `No. ${card.pokedex}  Lv${card.level}`));
	} else {
		meta.appendChild(el('div', '', card.isTrainer ? 'Trainer' : 'Energy'));
	}
	meta.appendChild(el('div', 'cp-set', `${setName(card)} ${rarityName(card)}`));
	left.appendChild(meta);
	body.appendChild(left);
	const right = el('div', 'cp-right');
	right.appendChild(el('div', 'cp-section-title', card.isPokemon ? 'Attacks & Powers' : 'Rules'));
	if (card.isPokemon) {
		for (const atk of card.attacks) {
			const a = el('div', 'cp-attack');
			const line = el('div', 'cp-atk-line');
			if (atk.isPower) line.appendChild(el('span', 'cp-power', 'PKMN POWER'));
			else line.appendChild(costIcons(atk.cost));
			line.appendChild(el('span', 'cp-atk-name', atk.name));
			if (atk.damage) line.appendChild(el('span', 'cp-atk-dmg', String(atk.damage) + ['', '+', '-', '×', ''][atk.category & 7]));
			a.appendChild(line);
			if (atk.description) {
				const d = el('div', 'cp-atk-desc');
				d.appendChild(richText(reflowRules(atk.description)));
				a.appendChild(d);
			}
			right.appendChild(a);
		}
		const wr = el('div', 'cp-wr');
		const cell = (label, colors, extraText) => {
			const c = el('span', 'cp-wr-cell');
			c.appendChild(el('span', 'cp-wr-label', label));
			colors.forEach((x) => c.appendChild(esym(x)));
			if (extraText) c.appendChild(document.createTextNode(extraText));
			if (!colors.length && !extraText) c.appendChild(document.createTextNode('-'));
			return c;
		};
		wr.appendChild(cell('Weakness', wrColors(card.weakness)));
		wr.appendChild(cell('Resistance', wrColors(card.resistance)));
		const rc = el('span', 'cp-wr-cell');
		rc.appendChild(el('span', 'cp-wr-label', 'Retreat'));
		if (card.retreat === C.UNABLE_RETREAT) rc.appendChild(document.createTextNode('unable'));
		else if (!card.retreat) rc.appendChild(document.createTextNode('-'));
		else for (let i = 0; i < card.retreat; i++) rc.appendChild(esym(6));
		wr.appendChild(rc);
		right.appendChild(wr);
		if (card.description) {
			const d = el('div', 'cp-desc');
			d.appendChild(el('div', 'cp-section-title', 'Pokédex'));
			d.appendChild(richText(reflowRules(card.description)));
			right.appendChild(d);
		}
	} else if (card.description) {
		const d = el('div', 'cp-desc');
		d.appendChild(richText(reflowRules(card.description)));
		right.appendChild(d);
	}
	body.appendChild(right);
	page.appendChild(body);
	return page;
}
