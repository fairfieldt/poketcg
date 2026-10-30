// Deck builder: browse every card, build a 60-card deck under the original deck machine's rules
// (engine/menus/deck_configuration.asm: at most 60 cards, at most 4 cards with the same name
// except basic energy, a deck needs exactly 60 cards and a Basic Pokemon to be saved), save decks
// in localStorage and share them as text codes.
import { C } from './engine.js';
import { el, clear, art, esym, typeIcon, cardTitle, cardPage, COLOR_NAMES, setName, rarityName } from './gfx.js';
import {
	allDecks, loadCustomDecks, upsertCustomDeck, deleteCustomDeck, cannotAdd, validateDeck, sortCards,
	encodeDeck, decodeDeck,
} from './data.js';

const FILTERS = [
	{ key: 'all', label: 'All', test: () => true },
	...[C.GRASS, C.FIRE, C.WATER, C.LIGHTNING, C.FIGHTING, C.PSYCHIC, C.COLORLESS].map((c) => ({
		key: 'c' + c, color: c, label: COLOR_NAMES[c], test: (card) => card.isPokemon && (card.type & 7) === c,
	})),
	{ key: 'trainer', label: 'Trainer', test: (card) => card.isTrainer },
	{ key: 'energy', label: 'Energy', test: (card) => card.isEnergy },
];
const SETS = ['Colosseum', 'Evolution', 'Mystery', 'Laboratory', 'Promotional', 'Energy'];

export class DeckBuilder {
	constructor(root, duel, { onBack, toast }) {
		this.root = root;
		this.duel = duel;
		this.onBack = onBack;
		this.toast = toast || ((m) => alert(m));
		this.cards = duel.cards();
		this.filter = 'all';
		this.set = -1;
		this.search = '';
		this.deck = { id: null, name: 'New Deck', cards: [] };
		this.dirty = false;
		this.build();
	}

	cardFn = (id) => this.duel.card(id);

	build() {
		const r = clear(this.root);
		r.classList.add('deckbuilder');
		const top = el('div', 'db-top');
		const back = el('button', 'btn', '◀ Menu');
		back.addEventListener('click', () => {
			if (this.dirty && !confirm('Quit without saving your changes?')) return;
			this.onBack();
		});
		this.$name = el('input', 'db-name');
		this.$name.maxLength = 24;
		this.$name.placeholder = 'Deck name';
		this.$name.addEventListener('input', () => { this.deck.name = this.$name.value; this.dirty = true; });
		this.$count = el('span', 'db-count');
		top.append(back, this.$name, this.$count);
		const actions = el('div', 'db-actions');
		const mk = (label, fn, cls = '') => { const b = el('button', 'btn ' + cls, label); b.addEventListener('click', fn); actions.appendChild(b); return b; };
		this.$load = el('select', 'db-load');
		this.$load.addEventListener('change', () => { if (this.$load.value) this.loadDeck(this.$load.value); this.$load.value = ''; });
		actions.appendChild(this.$load);
		mk('New', () => this.newDeck());
		mk('Save', () => this.save(false), 'primary');
		mk('Save as new', () => this.save(true));
		this.$delete = mk('Delete', () => this.remove());
		mk('Export code', () => this.exportCode());
		mk('Import code', () => this.importCode());
		mk('Clear', () => { this.deck.cards = []; this.dirty = true; this.refresh(); });
		top.appendChild(actions);
		r.appendChild(top);

		const main = el('div', 'db-main');
		const cat = el('div', 'db-catalog');
		const filters = el('div', 'db-filters');
		this.$filterBtns = FILTERS.map((f) => {
			const b = el('button', 'btn filter');
			if (f.color !== undefined) b.appendChild(esym(f.color));
			b.appendChild(document.createTextNode(f.color !== undefined ? '' : f.label));
			b.title = f.label;
			b.addEventListener('click', () => { this.filter = f.key; this.refresh(); });
			filters.appendChild(b);
			return [f.key, b];
		});
		cat.appendChild(filters);
		const row2 = el('div', 'db-filters2');
		this.$set = el('select', 'db-set');
		this.$set.appendChild(new Option('All sets', '-1'));
		SETS.forEach((s, i) => this.$set.appendChild(new Option(s, String(i))));
		this.$set.addEventListener('change', () => { this.set = Number(this.$set.value); this.refresh(); });
		this.$search = el('input', 'db-search');
		this.$search.placeholder = 'Search by name…';
		this.$search.type = 'search';
		this.$search.addEventListener('input', () => { this.search = this.$search.value.trim().toLowerCase(); this.refresh(); });
		row2.append(this.$set, this.$search);
		cat.appendChild(row2);
		this.$list = el('div', 'db-list');
		cat.appendChild(this.$list);
		main.appendChild(cat);

		const deckPane = el('div', 'db-deck');
		this.$errors = el('div', 'db-errors');
		deckPane.appendChild(this.$errors);
		this.$deckList = el('div', 'db-decklist');
		deckPane.appendChild(this.$deckList);
		main.appendChild(deckPane);
		r.appendChild(main);
		this.$modal = el('div', 'modal hidden');
		r.appendChild(this.$modal);
		this.fillLoadMenu();
		this.refresh();
	}

	fillLoadMenu() {
		const s = clear(this.$load);
		s.appendChild(new Option('Load deck…', ''));
		const groups = { custom: 'My decks', starter: 'Starter decks', club: 'Club & opponent decks' };
		for (const [kind, label] of Object.entries(groups)) {
			const g = document.createElement('optgroup');
			g.label = label;
			for (const d of allDecks(this.duel).filter((x) => x.kind === kind)) g.appendChild(new Option(d.name + (d.npc ? ` (${d.npc})` : ''), d.key));
			if (g.children.length) s.appendChild(g);
		}
	}

	loadDeck(key) {
		if (this.dirty && !confirm('Discard your changes?')) return;
		const d = allDecks(this.duel).find((x) => x.key === key);
		if (!d) return;
		this.deck = { id: d.kind === 'custom' ? d.id : null, name: d.kind === 'custom' ? d.name : `${d.name} copy`, cards: sortCards(d.cards) };
		this.dirty = d.kind !== 'custom';
		this.refresh();
	}

	setDeck(deck) {
		this.deck = { id: deck.id || null, name: deck.name, cards: sortCards(deck.cards) };
		this.dirty = false;
		this.refresh();
	}

	newDeck() {
		if (this.dirty && !confirm('Discard your changes?')) return;
		this.deck = { id: null, name: 'New Deck', cards: [] };
		this.dirty = false;
		this.refresh();
	}

	add(id) {
		const why = cannotAdd(this.deck.cards, id, this.cardFn);
		if (why) { this.toast(why); return; }
		this.deck.cards = sortCards([...this.deck.cards, id]);
		this.dirty = true;
		this.refresh();
	}

	removeCard(id) {
		const i = this.deck.cards.indexOf(id);
		if (i < 0) return;
		this.deck.cards.splice(i, 1);
		this.dirty = true;
		this.refresh();
	}

	save(asNew) {
		const errors = validateDeck(this.deck.cards, this.cardFn);
		if (errors.length) { this.toast('Cannot save: ' + errors.join(' ')); return; }
		const name = (this.deck.name || '').trim() || 'My Deck';
		const id = !asNew && this.deck.id ? this.deck.id : `d${Date.now().toString(36)}${Math.floor(Math.random() * 1e4)}`;
		this.deck = { id, name, cards: sortCards(this.deck.cards) };
		upsertCustomDeck({ id, name, cards: this.deck.cards });
		this.dirty = false;
		this.fillLoadMenu();
		this.refresh();
		this.toast(`Saved "${name}".`);
	}

	remove() {
		if (!this.deck.id) return;
		if (!confirm(`Delete "${this.deck.name}"?`)) return;
		deleteCustomDeck(this.deck.id);
		this.deck = { id: null, name: this.deck.name, cards: this.deck.cards };
		this.dirty = true;
		this.fillLoadMenu();
		this.refresh();
	}

	exportCode() {
		const code = encodeDeck(this.deck.name, this.deck.cards);
		this.dialog('Deck code', (box) => {
			const ta = el('textarea', 'code');
			ta.value = code;
			ta.readOnly = true;
			box.appendChild(ta);
			const copy = el('button', 'btn', 'Copy');
			copy.addEventListener('click', async () => {
				ta.select();
				try { await navigator.clipboard.writeText(code); copy.textContent = 'Copied'; } catch { document.execCommand && document.execCommand('copy'); }
			});
			box.appendChild(copy);
			setTimeout(() => ta.select(), 0);
		});
	}

	importCode() {
		this.dialog('Import deck code', (box, close) => {
			const ta = el('textarea', 'code');
			ta.placeholder = 'TCG1-…';
			box.appendChild(ta);
			const err = el('div', 'db-errors');
			box.appendChild(err);
			const ok = el('button', 'btn primary', 'Import');
			ok.addEventListener('click', () => {
				try {
					const d = decodeDeck(ta.value, this.duel.numCards);
					this.deck = { id: null, name: d.name || 'Imported Deck', cards: sortCards(d.cards) };
					this.dirty = true;
					this.refresh();
					close();
				} catch (e) {
					err.textContent = e.message;
				}
			});
			box.appendChild(ok);
			setTimeout(() => ta.focus(), 0);
		});
	}

	dialog(title, fill) {
		const m = this.$modal;
		clear(m);
		const box = el('div', 'dialog');
		box.appendChild(el('div', 'dlg-title', title));
		const close = () => { m.className = 'modal hidden'; clear(m); };
		fill(box, close);
		const x = el('button', 'btn', 'Close');
		x.addEventListener('click', close);
		box.appendChild(x);
		m.appendChild(box);
		m.className = 'modal';
		m.onclick = (e) => { if (e.target === m) close(); };
	}

	showCard(card) {
		this.dialog(cardTitle(card), (box) => box.appendChild(cardPage(card)));
	}

	refresh() {
		this.$name.value = this.deck.name;
		const n = this.deck.cards.length;
		this.$count.textContent = `${n}/${C.DECK_SIZE}`;
		this.$count.classList.toggle('ok', n === C.DECK_SIZE);
		this.$delete.disabled = !this.deck.id;
		for (const [k, b] of this.$filterBtns) b.classList.toggle('cur', k === this.filter);
		// catalog
		const f = FILTERS.find((x) => x.key === this.filter);
		const list = clear(this.$list);
		const counts = new Map();
		for (const id of this.deck.cards) counts.set(id, (counts.get(id) || 0) + 1);
		const shown = this.cards.filter((c) => f.test(c) && (this.set < 0 || (c.set >> 4) === this.set) &&
			(!this.search || c.name.toLowerCase().includes(this.search)));
		for (const c of shown) {
			const row = el('div', 'db-card');
			row.dataset.id = c.id;
			row.appendChild(art(c.id, 0.5));
			const info = el('div', 'db-info');
			const name = el('div', 'db-cname');
			name.append(typeIcon(c), document.createTextNode(' ' + cardTitle(c)));
			info.appendChild(name);
			info.appendChild(el('div', 'db-sub', `${c.isPokemon ? `HP ${c.hp} · ${['Basic', 'Stage 1', 'Stage 2', 'Stage 2'][c.stage]} · ` : ''}${setName(c)} ${rarityName(c)}`));
			info.addEventListener('click', () => this.showCard(c));
			row.appendChild(info);
			const cnt = counts.get(c.id) || 0;
			const ctl = el('div', 'db-ctl');
			const minus = el('button', 'btn small', '−');
			minus.disabled = !cnt;
			minus.addEventListener('click', () => this.removeCard(c.id));
			const num = el('span', 'db-num', cnt ? `×${cnt}` : '');
			const plus = el('button', 'btn small add', '+');
			const why = cannotAdd(this.deck.cards, c.id, this.cardFn);
			plus.disabled = !!why;
			plus.title = why || 'Add to deck';
			plus.addEventListener('click', () => this.add(c.id));
			ctl.append(minus, num, plus);
			row.appendChild(ctl);
			list.appendChild(row);
		}
		if (!shown.length) list.appendChild(el('div', 'cl-empty', 'No cards match.'));
		// deck
		const dl = clear(this.$deckList);
		const groups = [['Pokémon', (c) => c.isPokemon], ['Trainers', (c) => c.isTrainer], ['Energy', (c) => c.isEnergy]];
		for (const [label, test] of groups) {
			const ids = [...counts.keys()].filter((id) => test(this.cardFn(id)));
			const total = ids.reduce((s, id) => s + counts.get(id), 0);
			const g = el('div', 'db-group');
			g.appendChild(el('div', 'db-gtitle', `${label} (${total})`));
			for (const id of ids) {
				const c = this.cardFn(id);
				const row = el('div', 'db-drow');
				const nm = el('span', 'db-dname');
				nm.append(typeIcon(c), document.createTextNode(' ' + cardTitle(c)));
				nm.addEventListener('click', () => this.showCard(c));
				const minus = el('button', 'btn small', '−');
				minus.addEventListener('click', () => this.removeCard(id));
				const plus = el('button', 'btn small', '+');
				plus.disabled = !!cannotAdd(this.deck.cards, id, this.cardFn);
				plus.addEventListener('click', () => this.add(id));
				row.append(el('span', 'db-dnum', `${counts.get(id)}`), nm, minus, plus);
				g.appendChild(row);
			}
			dl.appendChild(g);
		}
		const errors = validateDeck(this.deck.cards, this.cardFn);
		clear(this.$errors);
		if (errors.length) errors.forEach((e) => this.$errors.appendChild(el('div', 'err', e)));
		else this.$errors.appendChild(el('div', 'ok', 'This deck is ready to duel.'));
	}
}
