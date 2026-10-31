// The duel screen: renders the engine's events and answers its requests with menus modeled on
// the original duel screens (main scene + HUDs, duel menu, card lists, play area, ...).
//
//   const ui = new DuelUI(rootElement, duel, { mode, localSide, pacing });
//   duel.run(setup, { onEvent: (ev) => ui.onEvent(ev), onRequest: (req) => ui.onRequest(req) });
//
// Board state is read straight from engine memory (duel.duelvars) whenever something changes.
import { C, T, W, EV, TEXT_MODE, SCREEN, REQ, MENU, TCG_PLAYER, TCG_OPPONENT, TCG_CANCEL } from './engine.js';
import { el, clear, art, sym, esym, typeIcon, richText, plainText, reflowRules, cardTitle, costIcons, cardPage, COLOR_NAMES, STATUS_NAMES } from './gfx.js';
import { duelBanner } from './duel_banners.js';

export const K = { UP: 'up', DOWN: 'down', LEFT: 'left', RIGHT: 'right', A: 'a', B: 'b', START: 'start' };

export function keyOf(e) {
	switch (e.key) {
	case 'ArrowUp': return K.UP;
	case 'ArrowDown': return K.DOWN;
	case 'ArrowLeft': return K.LEFT;
	case 'ArrowRight': return K.RIGHT;
	case 'z': case 'Z': case 'Enter': case ' ': return K.A;
	case 'x': case 'X': case 'Escape': case 'Backspace': return K.B;
	case 'c': case 'C': case 'Shift': return K.START;
	default: return null;
	}
}

const other = (d) => (d === TCG_PLAYER ? TCG_OPPONENT : TCG_PLAYER);
const SYM_POKEMON = 0x0d, SYM_PRIZE = 0x30, SYM_HP_OK = 0x16, SYM_HP_NOK = 0x17, SYM_PLUSPOWER = 0x14, SYM_DEFENDER = 0x15;
const STATUS_SYM = { [C.CONFUSED]: 0x0a, [C.ASLEEP]: 0x09, [C.PARALYZED]: 0x0b };
const POISON_SYM = 0x08;
const COLOR_MENU_TEXTS = ['ChoosePokemonWishToColorChangeText', 'ChooseWeaknessYouWishToChangeText', 'ChooseResistanceYouWishToChangeText'];
const COLOR_MENU_ORDER = [C.GRASS, C.FIRE, C.WATER, C.LIGHTNING, C.FIGHTING, C.PSYCHIC]; // ShiftListItemToColor
// duel menu: item index at grid position (row, col), as DuelMenuData places them
const DUEL_MENU_GRID = [[0, 2, 4], [1, 3, 5]];
const DUEL_MENU_TEXTS = ['HandText', 'AttackText', 'CheckText', 'PKMNPowerText', 'RetreatText', 'DoneText'];

function delay(ms) { return new Promise((r) => setTimeout(r, ms)); }

function popcount(v) { let n = 0; for (; v; v >>= 1) n += v & 1; return n; }

export class DuelUI {
	constructor(root, duel, opts = {}) {
		this.root = root;
		this.duel = duel;
		this.mode = opts.mode || 'hotseat';
		this.localSide = opts.localSide || TCG_PLAYER;
		this.perspective = this.localSide;
		this.pacing = opts.pacing !== false;
		this.remoteAckDelay = opts.remoteAckDelay ?? 1400;
		this.skipping = false; // fast-forward (online replay): no pacing, auto ACK
		this.skipDelay = !!opts.skipDelay; // sSkipDelayAllowed: holding B skips pacing and animations
		this.animationsDisabled = !!opts.animationsDisabled;
		this.onCoinSound = opts.onCoinSound;
		this.bHeld = false;
		this.screen = SCREEN.MAIN;
		this.screenArg = [0, 0, 0, 0];
		this.lastTextId = 0;
		this.text = '';
		this.practiceChecklist = '';
		this.practiceReading = false;
		this.practicePages = [];
		this.practicePage = 0;
		this.playAreaExclude = 0;
		this.overlayKind = null;
		this.pendingCardSelection = null;
		this.controls = []; // input controller stack, top gets the keys
		this.coins = [];
		this.ended = null;
		this.build();
		this.onKeyDown = (e) => {
			if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) return;
			const k = keyOf(e);
			if (!k || !this.root.isConnected) return;
			if (k === K.B) this.setBHeld(true);
			// a held A/B is one press (auto-repeat would answer menu after menu)
			if (e.repeat && (k === K.A || k === K.B)) { e.preventDefault(); return; }
			const c = this.controls[this.controls.length - 1];
			if (c && c.key && c.key(k) !== false) e.preventDefault();
		};
		this.onKeyUp = (e) => { if (keyOf(e) === K.B) this.setBHeld(false); };
		this.onBlur = () => this.setBHeld(false);
		document.addEventListener('keydown', this.onKeyDown);
		document.addEventListener('keyup', this.onKeyUp);
		window.addEventListener('blur', this.onBlur);
	}

	destroy() {
		document.removeEventListener('keydown', this.onKeyDown);
		document.removeEventListener('keyup', this.onKeyUp);
		window.removeEventListener('blur', this.onBlur);
		this.controls = [];
	}

	/* B held with the skip-delay option on: no pacing waits, no animations (like the original's
	 * debug option, which cuts the draw/shuffle/prize animations and delays while B is held) */
	setBHeld(held) {
		this.bHeld = held;
		this.root.classList.toggle('fast-forward', this.skipDelay && held);
	}

	// ---------------------------------------------------------------- layout
	build() {
		const r = clear(this.root);
		r.classList.add('duel');
		this.$fieldWrap = el('div', 'field-wrap');
		this.$field = el('div', 'field');
		this.$fieldWrap.appendChild(this.$field);
		this.$overlay = el('div', 'overlay hidden');
		this.$fieldWrap.appendChild(this.$overlay);
		this.$banner = el('div', 'banner hidden');
		this.$fieldWrap.appendChild(this.$banner);
		this.$fx = el('div', 'fx-layer');
		this.$fieldWrap.appendChild(this.$fx);
		this.$modal = el('div', 'modal hidden');
		this.$fieldWrap.appendChild(this.$modal);
		this.$practice = el('section', 'practice-reading hidden');
		this.$practice.setAttribute('aria-label', 'Dr. Mason');
		this.$fieldWrap.appendChild(this.$practice);
		r.appendChild(this.$fieldWrap);
		this.$textbox = el('div', 'textbox');
		this.$text = el('div', 'tb-text');
		this.$menu = el('div', 'tb-menu');
		this.$ack = el('div', 'tb-ack hidden', '▼');
		this.$textbox.append(this.$text, this.$menu, this.$ack);
		this.$practiceHint = el('div', 'practice-continue hidden', 'A/B: continue');
		this.$textbox.appendChild(this.$practiceHint);
		r.appendChild(this.$textbox);
		this.$status = el('div', 'status-line');
		r.appendChild(this.$status);
		this.$textbox.addEventListener('click', () => {
			const c = this.controls[this.controls.length - 1];
			if (c && c.textboxClick) c.textboxClick();
		});
		this.$overlay.addEventListener('click', (e) => {
			const c = this.controls[this.controls.length - 1];
			if (c && c.overlayClick && e.target === this.$overlay) c.overlayClick();
		});
		this.$practice.addEventListener('click', () => {
			this.controls[this.controls.length - 1]?.textboxClick?.();
		});
		this.render();
	}

	setStatus(text, kind = '') {
		this.$status.textContent = text || '';
		this.$status.className = 'status-line' + (kind ? ' ' + kind : '');
	}

	name(d) {
		return this.names ? this.names[d === TCG_OPPONENT ? 1 : 0] : (d === TCG_OPPONENT ? 'Opponent' : 'Player');
	}

	// ---------------------------------------------------------------- board state
	sideState(d) {
		const v = this.duel.duelvars(d);
		const n = Math.min(v[C.DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA], 6);
		const slots = [];
		for (let i = 0; i < n; i++) {
			const idx = v[C.DUELVARS_ARENA_CARD + i];
			if (idx === 0xff || idx >= 60) { slots.push(null); continue; }
			const card = this.duel.card(this.duel.cardId(d, idx));
			const energies = [0, 0, 0, 0, 0, 0, 0];
			const attached = [];
			for (let j = 0; j < 60; j++) {
				if (v[C.DUELVARS_CARD_LOCATIONS + j] !== (C.CARD_LOCATION_PLAY_AREA | i)) continue;
				const c = this.duel.card(this.duel.cardId(d, j));
				if (c.isEnergy) {
					energies[c.type & 7] += (c.type & 7) === 6 ? 2 : 1;
					attached.push(j);
				}
			}
			const changed = v[C.DUELVARS_ARENA_CARD_CHANGED_TYPE + i]; // ARENA_CARD_CHANGED_TYPE, BENCHx_CARD_CHANGED_COLOR
			slots.push({
				slot: i, deckIndex: idx, card, hp: v[C.DUELVARS_ARENA_CARD_HP + i],
				color: changed & C.HAS_CHANGED_COLOR ? changed & 0xf : (card.isPokemon ? card.type & 7 : 6),
				energies, attached,
				pluspower: v[C.DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER + i],
				defender: v[C.DUELVARS_ARENA_CARD_ATTACHED_DEFENDER + i],
				status: i === 0 ? v[C.DUELVARS_ARENA_CARD_STATUS] : 0,
			});
		}
		return {
			d, slots, count: n,
			hand: v[C.DUELVARS_NUMBER_OF_CARDS_IN_HAND],
			deck: 60 - v[C.DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK],
			discard: v[C.DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE],
			prizes: popcount(v[C.DUELVARS_PRIZES]), prizeMask: v[C.DUELVARS_PRIZES],
		};
	}

	/* deck indexes of a duelist's cards at a location, in deck index order */
	cardsAt(d, loc) {
		const v = this.duel.duelvars(d), out = [];
		for (let j = 0; j < 60; j++) if (v[C.DUELVARS_CARD_LOCATIONS + j] === loc) out.push(j);
		return out;
	}

	handCards(d) {
		const v = this.duel.duelvars(d), out = [];
		for (let i = 0; i < v[C.DUELVARS_NUMBER_OF_CARDS_IN_HAND] && i < 60; i++) out.push(v[C.DUELVARS_HAND + i]);
		return out;
	}

	// ---------------------------------------------------------------- rendering the main scene
	render() {
		const f = clear(this.$field);
		const me = this.perspective, opp = other(me);
		const s = { [me]: this.sideState(me), [opp]: this.sideState(opp) };
		this.state = s;
		f.appendChild(this.renderInfo(s[opp], 'opp'));
		f.appendChild(this.renderBench(s[opp], 'opp'));
		const oppRow = el('div', 'arena-row opp');
		oppRow.append(this.renderHUD(s[opp], 'opp'), this.renderActive(s[opp], 'opp'));
		f.appendChild(oppRow);
		const sep = el('div', 'separator');
		f.appendChild(sep);
		const meRow = el('div', 'arena-row me');
		meRow.append(this.renderActive(s[me], 'me'), this.renderHUD(s[me], 'me'));
		f.appendChild(meRow);
		f.appendChild(this.renderBench(s[me], 'me'));
		f.appendChild(this.renderInfo(s[me], 'me'));
	}

	renderInfo(st, cls) {
		const row = el('div', 'info-row ' + cls);
		const name = el('span', 'duelist-name', this.name(st.d));
		if (this.turnHolder === st.d) name.classList.add('turn');
		row.appendChild(name);
		const icons = el('span', 'counts');
		const pk = el('span', 'count');
		pk.title = 'Pokémon in play';
		pk.append(sym(SYM_POKEMON), document.createTextNode(String(st.count)));
		const pr = el('span', 'count prizes');
		pr.title = 'Prizes left';
		pr.append(sym(SYM_PRIZE), document.createTextNode(String(st.prizes)));
		icons.append(pk, pr);
		icons.appendChild(el('span', 'count', `Hand ${st.hand}`));
		for (const [label, value] of [['Deck', st.deck], ['Discard', st.discard]]) {
			const pile = el('span', 'count pile-count');
			pile.append(el('span', 'pile-back'), document.createTextNode(`${label} ${value}`));
			icons.appendChild(pile);
		}
		row.appendChild(icons);
		return row;
	}

	renderBench(st, cls) {
		const row = el('div', 'bench-row ' + cls);
		for (let i = 1; i <= 5; i++) {
			const slot = st.slots[i];
			const b = el('div', 'bench-slot' + (slot ? '' : ' empty'));
			b.dataset.side = st.d;
			b.dataset.slot = i;
			if (slot) {
				b.appendChild(art(slot.card.id, 0.5));
				const line = el('div', 'bench-line');
				line.appendChild(esym(slot.color));
				line.appendChild(el('span', 'bench-hp', `${slot.hp}`));
				const en = el('span', 'bench-energy');
				slot.energies.forEach((n, c) => { if (n) { en.appendChild(esym(c)); if (n > 1) en.appendChild(el('small', '', String(n))); } });
				line.appendChild(en);
				b.appendChild(line);
				b.title = `${cardTitle(slot.card)}  HP ${slot.hp}/${slot.card.hp}`;
			}
			b.addEventListener('click', () => this.fieldClick(st.d, i));
			row.appendChild(b);
		}
		return row;
	}

	renderActive(st, cls) {
		const a = el('div', 'active ' + cls);
		a.dataset.side = st.d;
		a.dataset.slot = 0;
		const slot = st.slots[0];
		if (slot) a.appendChild(art(slot.card.id, 1));
		else a.classList.add('empty');
		a.addEventListener('click', () => this.fieldClick(st.d, 0));
		return a;
	}

	hpBar(slot) {
		const bar = el('span', 'hpbar');
		const max = slot.card.hp || 1, cur = Math.max(0, Math.min(slot.hp, max));
		bar.classList.toggle('low', cur <= max / 4);
		bar.classList.toggle('worn', cur > max / 4 && cur <= max / 2);
		bar.setAttribute('role', 'meter');
		bar.setAttribute('aria-label', 'HP');
		bar.setAttribute('aria-valuemin', '0');
		bar.setAttribute('aria-valuemax', String(max));
		bar.setAttribute('aria-valuenow', String(cur));
		const fill = el('span', 'hp-fill');
		fill.style.width = `${cur * 100 / max}%`;
		bar.appendChild(fill);
		return bar;
	}

	renderHUD(st, cls) {
		const hud = el('div', 'hud ' + cls);
		const slot = st.slots[0];
		if (!slot) return hud;
		const nameLine = el('div', 'hud-name');
		nameLine.appendChild(esym(slot.color));
		nameLine.appendChild(el('span', '', cardTitle(slot.card)));
		hud.appendChild(nameLine);
		const eLine = el('div', 'hud-energy');
		eLine.appendChild(sym(0x12));
		const totalEnergy = slot.energies.reduce((a, b) => a + b, 0);
		slot.energies.forEach((n, c) => {
			if (!n) return;
			if (totalEnergy <= 12) for (let i = 0; i < n; i++) eLine.appendChild(esym(c));
			else {
				const group = el('span', 'energy-group');
				group.append(esym(c), document.createTextNode(`×${n}`));
				eLine.appendChild(group);
			}
		});
		hud.appendChild(eLine);
		const hpLine = el('div', 'hud-hp');
		hpLine.appendChild(sym(0x10));
		hpLine.appendChild(this.hpBar(slot));
		hpLine.appendChild(el('span', 'hp-num', `${slot.hp}/${slot.card.hp}`));
		hud.appendChild(hpLine);
		const stLine = el('div', 'hud-status');
		const status = slot.status;
		if (status & C.CNF_SLP_PRZ) {
			const s = sym(STATUS_SYM[status & C.CNF_SLP_PRZ] || 0);
			s.title = STATUS_NAMES[status & C.CNF_SLP_PRZ] || '';
			stLine.append(s, el('span', 'status-name', STATUS_NAMES[status & C.CNF_SLP_PRZ] || ''));
		}
		if (status & C.POISONED) {
			stLine.appendChild(sym(POISON_SYM));
			if ((status & C.DOUBLE_POISONED) === C.DOUBLE_POISONED) stLine.appendChild(sym(POISON_SYM));
			stLine.appendChild(el('span', 'status-name', (status & C.DOUBLE_POISONED) === C.DOUBLE_POISONED ? 'Double Poison' : 'Poisoned'));
		}
		if (slot.pluspower) { stLine.appendChild(sym(SYM_PLUSPOWER)); stLine.appendChild(el('span', 'pp', `PlusPower ${slot.pluspower}`)); }
		if (slot.defender) { stLine.appendChild(sym(SYM_DEFENDER)); stLine.appendChild(el('span', 'pp', `Defender ${slot.defender}`)); }
		hud.appendChild(stLine);
		return hud;
	}

	slotElement(d, slot) {
		return this.$field.querySelector(`[data-side="${d}"][data-slot="${slot}"]`);
	}

	fieldClick(d, slot) {
		const c = this.controls[this.controls.length - 1];
		if (c && c.fieldClick && c.fieldClick(d, slot)) return;
		const st = this.sideState(d).slots[slot];
		if (st && !this.ended) this.showCardPage(st.card, { hp: st.hp });
	}

	// ---------------------------------------------------------------- text box
	setText(str) {
		this.text = str || '';
		clear(this.$text).appendChild(richText(this.text));
	}

	hidePracticeReading() {
		this.practiceReading = false;
		this.$practice.classList.add('hidden');
		this.$practiceHint.classList.add('hidden');
		this.$textbox.classList.remove('practice-reading-active');
	}

	showPracticeReading(ev) {
		this.practiceReading = true;
		let message = ev.str || '';
		if (ev.text === T.LetsPlayTheGamePracticeDuelText && this.practiceChecklist.includes('\n'))
			message += '\n\n' + this.practiceChecklist.slice(this.practiceChecklist.indexOf('\n')).trim();
		const lines = message.split('\n');
		this.practicePages = [];
		for (let i = 0; i < lines.length; i += 11) this.practicePages.push(lines.slice(i, i + 11).join('\n'));
		if (!this.practicePages.length) this.practicePages.push('');
		this.practicePage = 0;
		this.paintPracticeReading();
	}

	paintPracticeReading() {
		clear(this.$practice);
		const heading = el('header', 'practice-heading');
		heading.appendChild(el('h2', '', 'Dr. Mason'));
		if (this.practiceChecklist) heading.appendChild(el('span', '', this.practiceChecklist.split('\n')[0]));
		const message = el('div', 'practice-message');
		message.appendChild(richText(this.practicePages[this.practicePage]));
		this.$practice.append(heading, message);
		this.$practice.classList.remove('hidden');
		this.$practiceHint.classList.remove('hidden');
		this.$practiceHint.textContent = 'A/B: continue' + (this.practicePages.length > 1 ?
			`    ${this.practicePage + 1} / ${this.practicePages.length}` : '');
		this.$textbox.classList.add('practice-reading-active');
	}

	clearMenu() {
		clear(this.$menu);
		this.$textbox.classList.remove('with-menu', 'menu-only');
	}

	// ---------------------------------------------------------------- overlays
	setPerspective(d) {
		if (this.mode !== 'hotseat' || d === this.perspective || (d !== TCG_PLAYER && d !== TCG_OPPONENT)) return;
		this.perspective = d;
		clear(this.$fx);
		this.render();
	}

	showOverlay(kind, node) {
		this.overlayKind = kind;
		this.$textbox.classList.remove('coin-present');
		this.$fieldWrap.appendChild(this.$overlay);
		clear(this.$overlay).appendChild(node);
		this.$overlay.className = 'overlay ov-' + kind;
	}

	hideOverlay() {
		this.overlayKind = null;
		this.$textbox.classList.remove('coin-present');
		this.$fieldWrap.appendChild(this.$overlay);
		clear(this.$overlay);
		this.$overlay.className = 'overlay hidden';
	}

	showBanner(boxmsg) {
		clear(this.$banner);
		const b = duelBanner(boxmsg);
		if (!b) { this.hideBanner(); return; }
		this.$banner.appendChild(b);
		this.$banner.className = 'banner';
	}

	hideBanner() { this.$banner.className = 'banner hidden'; }

	pushControl(c) { this.controls.push(c); return c; }
	popControl(c) { const i = this.controls.lastIndexOf(c); if (i >= 0) this.controls.splice(i, 1); }

	/* UI-only card page (no engine involvement); closes on A/B/click */
	showCardPage(card, extra = {}) {
		return new Promise((resolve) => {
			const page = cardPage(card, extra);
			page.classList.add('modal-page');
			page.tabIndex = 0;
			const close = el('button', 'btn close-btn', 'Close');
			page.appendChild(close);
			clear(this.$modal).appendChild(page);
			this.$modal.className = 'modal';
			const ctl = this.pushControl({
				key: (k) => {
					if (k === K.A || k === K.B || k === K.START) done();
					else if (k === K.UP || k === K.DOWN) page.scrollBy({ top: (k === K.UP ? -1 : 1) * 48 });
					else if (k === K.LEFT || k === K.RIGHT) page.scrollBy({ top: (k === K.LEFT ? -1 : 1) * page.clientHeight * 0.8 });
					return true;
				},
			});
			const done = () => {
				this.popControl(ctl);
				this.$modal.className = 'modal hidden';
				clear(this.$modal);
				resolve();
			};
			close.addEventListener('click', (e) => { e.stopPropagation(); done(); });
			this.$modal.onclick = (e) => { if (e.target === this.$modal) done(); };
		});
	}

	largeCard(id, caption) {
		const card = this.duel.card(id);
		const box = el('div', 'large-card');
		const header = el('div', 'card-header');
		header.style.setProperty('--i', card.isTrainer ? 0 : card.isEnergy ? 1 : 2);
		box.appendChild(header);
		box.appendChild(art(id, 2));
		const name = el('div', 'large-name');
		name.append(typeIcon(card), document.createTextNode(' ' + cardTitle(card)));
		box.appendChild(name);
		if (card.isPokemon) box.appendChild(el('div', 'large-sub', `HP ${card.hp}`));
		if (caption) box.appendChild(el('div', 'large-caption', caption));
		const details = el('button', 'btn small', 'Details');
		details.addEventListener('click', (e) => { e.stopPropagation(); this.showCardPage(card); });
		box.appendChild(details);
		return box;
	}


	cardPreview(card) {
		const p = el('div', 'clp');
		const name = el('div', 'clp-name');
		name.append(typeIcon(card), document.createTextNode(cardTitle(card)));
		p.appendChild(name);
		p.appendChild(art(card.id, 1));
		p.appendChild(el('div', 'clp-sub', card.isPokemon
			? `HP ${card.hp} · ${COLOR_NAMES[card.type & 7]} · ${['Basic', 'Stage 1', 'Stage 2', 'Stage 2'][card.stage]}`
			: card.isTrainer ? 'Trainer' : `${COLOR_NAMES[card.type & 7]} Energy`));
		const effects = el('div', 'clp-effects');
		if (card.isPokemon) {
			for (const attack of card.attacks) {
				const section = el('div', 'clp-attack');
				const line = el('div', 'clp-attack-line');
				line.append(attack.isPower ? el('span', 'clp-power', 'Power') : costIcons(attack.cost), el('strong', '', attack.name));
				if (attack.damage) line.appendChild(el('span', 'clp-damage', String(attack.damage) + ['', '+', '−', '×', ''][attack.category & 7]));
				section.appendChild(line);
				if (attack.description) {
					const description = el('div', 'clp-effect');
					description.appendChild(richText(reflowRules(attack.description)));
					section.appendChild(description);
				}
				effects.appendChild(section);
			}
		} else if (card.description) {
			const description = el('div', 'clp-effect');
			description.appendChild(richText(reflowRules(card.description)));
			effects.appendChild(description);
		}
		p.appendChild(effects);
		const details = el('button', 'btn small', 'Details');
		details.addEventListener('click', e => { e.stopPropagation(); this.showCardPage(card); });
		p.appendChild(details);
		return p;
	}

	/* generic list of cards (deck indexes of duelist d) */
	cardListView({ d, items, header, cursor = 0, selectable = true, onSelect, onCancel, onMove }) {
		const wrap = el('div', 'card-list');
		if (header) wrap.appendChild(el('div', 'cl-header', plainText(header)));
		const body = el('div', 'cl-body');
		const list = el('div', 'cl-items');
		const preview = el('div', 'cl-preview');
		body.append(list, preview);
		wrap.appendChild(body);
		const rows = items.map((idx, i) => {
			const card = this.duel.card(this.duel.cardId(d, idx));
			const row = el('div', 'cl-item');
			row.append(typeIcon(card), el('span', 'cl-name', cardTitle(card)));
			if (card.isPokemon) row.appendChild(el('span', 'cl-stage', ['', '1', '2', '2'][card.stage] ? `St${['', '1', '2', '2'][card.stage]}` : 'Basic'));
			// Hover never changes the card that a later confirm will use.
			row.addEventListener('click', () => {
				ctl.move(i);
				if (selectable) { if (ctl.pointerSelect) ctl.pointerSelect(i); else ctl.select(); }
			});
			list.appendChild(row);
			return { row, card };
		});
		if (!items.length) list.appendChild(el('div', 'cl-empty', 'No cards.'));
		const ctl = {
			cursor: Math.min(cursor, Math.max(0, items.length - 1)),
			move: (i) => {
				if (!rows.length) return;
				ctl.cursor = (i + rows.length) % rows.length;
				rows.forEach((r, k) => r.row.classList.toggle('cur', k === ctl.cursor));
				const c = rows[ctl.cursor].card;
				clear(preview);
				preview.appendChild(this.cardPreview(c));
				rows[ctl.cursor].row.scrollIntoView({ block: 'nearest' });
				if (onMove) onMove(ctl.cursor);
			},
			select: () => { if (rows.length && onSelect) onSelect(ctl.cursor); },
			key: (k) => {
				if (k === K.UP) ctl.move(ctl.cursor - 1);
				else if (k === K.DOWN) ctl.move(ctl.cursor + 1);
				else if (k === K.LEFT) ctl.move(Math.max(0, ctl.cursor - 5));
				else if (k === K.RIGHT) ctl.move(Math.min(rows.length - 1, ctl.cursor + 5));
				else if (k === K.A) { if (selectable) ctl.select(); }
				else if (k === K.B) { if (onCancel) onCancel(); }
				else if (k === K.START) { if (rows.length) this.showCardPage(rows[ctl.cursor].card); }
				return true;
			},
			rows, wrap, owner: d, deckIndexes: items,
		};
		if (rows.length) ctl.move(ctl.cursor);
		return ctl;
	}

	/* play area list of duelist d (6 rows). selectable(slot) -> bool */
	playAreaView({ d, header, cursorSlot = 0, selectable = () => true, onSelect, onCancel, pkmnPower = null }) {
		const st = this.sideState(d);
		const wrap = el('div', 'play-area');
		wrap.appendChild(el('div', 'pa-header', header || (d === this.perspective ? 'Your Play Area' : `${this.name(d)}'s Play Area`)));
		const rows = [];
		st.slots.forEach((slot, i) => {
			if (!slot) return;
			const row = el('div', 'pa-row');
			row.appendChild(el('span', 'pa-loc', i === 0 ? 'Active' : `Bench ${i}`));
			row.appendChild(art(slot.card.id, 0.5));
			const info = el('div', 'pa-info');
			const n = el('div', 'pa-name');
			n.append(esym(slot.color), document.createTextNode(' ' + cardTitle(slot.card)));
			info.appendChild(n);
			const l2 = el('div', 'pa-line');
			l2.appendChild(sym(0x10));
			l2.appendChild(el('span', 'pa-hp', `${slot.hp}/${slot.card.hp}`));
			l2.appendChild(sym(0x12));
			slot.energies.forEach((cnt, c) => { for (let k = 0; k < cnt; k++) l2.appendChild(esym(c)); });
			if (slot.status & C.CNF_SLP_PRZ) l2.appendChild(sym(STATUS_SYM[slot.status & C.CNF_SLP_PRZ]));
			if (slot.status & C.POISONED) l2.appendChild(sym(POISON_SYM));
			if (slot.pluspower) l2.appendChild(sym(SYM_PLUSPOWER));
			if (slot.defender) l2.appendChild(sym(SYM_DEFENDER));
			info.appendChild(l2);
			if (pkmnPower) {
				const pw = slot.card.attacks.find((a) => a.isPower);
				if (pw) info.appendChild(el('div', 'pa-power', `PKMN POWER: ${pw.name}`));
			}
			row.appendChild(info);
			const ok = selectable(i);
			if (!ok) row.classList.add('disabled');
			row.addEventListener('click', () => { if (ok) { ctl.moveTo(i); ctl.select(); } else this.showCardPage(slot.card, { hp: slot.hp }); });
			wrap.appendChild(row);
			rows.push({ slot: i, row, ok, data: slot });
		});
		const sel = rows.filter((r) => r.ok);
		const ctl = {
			i: Math.max(0, sel.findIndex((r) => r.slot === cursorSlot)),
			moveTo: (slot) => { const k = sel.findIndex((r) => r.slot === slot); if (k >= 0) ctl.move(k); },
			move: (k) => {
				if (!sel.length) return;
				ctl.i = (k + sel.length) % sel.length;
				rows.forEach((r) => r.row.classList.toggle('cur', r === sel[ctl.i]));
			},
			select: () => { if (sel.length && onSelect) onSelect(sel[ctl.i].slot); },
			key: (k) => {
				if (k === K.UP || k === K.LEFT) ctl.move(ctl.i - 1);
				else if (k === K.DOWN || k === K.RIGHT) ctl.move(ctl.i + 1);
				else if (k === K.A) ctl.select();
				else if (k === K.B) { if (onCancel) onCancel(); }
				else if (k === K.START && sel.length) this.showCardPage(sel[ctl.i].data.card, { hp: sel[ctl.i].data.hp });
				return true;
			},
			fieldClick: (side, slot) => {
				if (side !== d) return false;
				const r = sel.find((x) => x.slot === slot);
				if (!r) return false;
				ctl.moveTo(slot);
				ctl.select();
				return true;
			},
			wrap,
		};
		ctl.move(ctl.i);
		return ctl;
	}

	/* a vertical/horizontal menu in the text box. items: [{label|node}] */
	textMenu({ items, cursor = 0, cols = 1, onSelect, onCancel, cls = '', grid = null }) {
		this.clearMenu();
		this.$textbox.classList.add('with-menu');
		const box = el('div', 'tmenu ' + cls);
		box.style.setProperty('--cols', cols);
		const els = items.map((it, i) => {
			const b = el('button', 'tm-item');
			if (it.node) b.appendChild(it.node); else b.appendChild(richText(it.label));
			if (it.disabled) b.classList.add('disabled');
			b.addEventListener('click', (e) => { e.stopPropagation(); ctl.move(i); ctl.select(); });
			if (grid) { b.style.gridRow = grid[i][0] + 1; b.style.gridColumn = grid[i][1] + 1; }
			box.appendChild(b);
			return b;
		});
		this.$menu.appendChild(box);
		const ctl = {
			cursor,
			move: (i) => {
				ctl.cursor = (i + els.length) % els.length;
				els.forEach((b, k) => b.classList.toggle('cur', k === ctl.cursor));
				if (ctl.onMove) ctl.onMove(ctl.cursor);
			},
			select: () => { if (onSelect) onSelect(ctl.cursor); },
			key: (k) => {
				if (grid) {
					const [r, c] = grid[ctl.cursor];
					const find = (rr, cc) => grid.findIndex(([a, b]) => a === rr && b === cc);
					const rows = Math.max(...grid.map((g) => g[0])) + 1, colsN = Math.max(...grid.map((g) => g[1])) + 1;
					if (k === K.UP || k === K.DOWN) { const j = find((r + (k === K.UP ? rows - 1 : 1)) % rows, c); if (j >= 0) ctl.move(j); }
					else if (k === K.LEFT || k === K.RIGHT) { const j = find(r, (c + (k === K.LEFT ? colsN - 1 : 1)) % colsN); if (j >= 0) ctl.move(j); }
				} else if (k === K.UP || k === K.LEFT) ctl.move(ctl.cursor - (k === K.UP && cols > 1 ? cols : 1));
				else if (k === K.DOWN || k === K.RIGHT) ctl.move(ctl.cursor + (k === K.DOWN && cols > 1 ? cols : 1));
				if (k === K.A) ctl.select();
				else if (k === K.B) { if (onCancel) onCancel(); }
				else if (k === K.START && ctl.onStart) ctl.onStart(ctl.cursor);
				return true;
			},
			els,
		};
		ctl.move(Math.min(cursor, els.length - 1));
		return ctl;
	}

	// ---------------------------------------------------------------- events
	pace(ms) {
		if (!this.pacing || this.skipping || ms <= 0 || (this.skipDelay && this.bHeld)) return undefined;
		return delay(ms);
	}

	onEvent(ev) {
		const a = ev.arg;
		switch (ev.type) {
		case EV.TEXT:
			if (a[1] === TEXT_MODE.PRACTICE_CHECKLIST) {
				this.practiceChecklist = ev.str || '';
				return undefined;
			}
			this.lastTextId = ev.text;
			this.setText(ev.str);
			if (a[1] === TEXT_MODE.PRACTICE_SPEECH) this.showPracticeReading(ev);
			else this.hidePracticeReading();
			return undefined;
		case EV.SCREEN:
			return this.onScreen(a);
		case EV.CARD:
			if (a[3] === 1) {
				const page = cardPage(this.duel.card(a[2]));
				page.classList.add('in-overlay');
				this.showOverlay('cardpage', page);
			} else {
				this.showOverlay('largecard', this.largeCard(a[2]));
			}
			this.render();
			return undefined;
		case EV.ANIM:
			return this.onAnim(a);
		case EV.COIN:
			return this.onCoin(a);
		case EV.DAMAGE: {
			this.render();
			const dmg = a[2] | (a[3] << 8);
			this.floatText(a[0], a[1], `-${dmg}`, 'dmg');
			return this.pace(500);
		}
		case EV.WAIT:
			return this.pace(Math.min(a[0] * 12, 700));
		case EV.TURN:
			this.practiceChecklist = '';
			this.hidePracticeReading();
			this.turnHolder = a[0];
			this.turnNumber = a[1];
			if (this.mode === 'hotseat' && this.perspective !== a[0]) { this.perspective = a[0]; clear(this.$fx); }
			this.render();
			return undefined;
		case EV.DUEL_END:
			this.ended = { finished: a[0], turnHolder: a[1] };
			this.render();
			return undefined;
		case EV.THINKING:
			this.setStatus(`${this.name(a[0])} is thinking…`);
			return undefined;
		default:
			return undefined;
		}
	}

	onScreen(a) {
		const screen = a[0];
		this.screen = screen;
		this.screenArg = a;
		if (screen !== SCREEN.EMPTY) this.hidePracticeReading();
		if (screen !== SCREEN.BOX_MESSAGE) this.hideBanner();
		switch (screen) {
		case SCREEN.MAIN: // main scene drawn: back from any other screen (arg[2] = 1: HUDs redrawn only)
			if (a[2] !== 1) this.hideOverlay();
			break;
		case SCREEN.EMPTY:
			if (this.overlayKind !== 'coin') this.hideOverlay();
			break;
		case SCREEN.BOX_MESSAGE:
			this.hideOverlay();
			this.showBanner(a[2]);
			break;
		case SCREEN.PLAY_AREA:
			this.playAreaExclude = a[2];
			this.pkmnPowerScreen = a[3] === 1;
			this.showOverlay('playarea', this.playAreaView({ d: a[1], selectable: () => false, pkmnPower: this.pkmnPowerScreen }).wrap);
			break;
		case SCREEN.CARD_LIST: {
			// show wDuelTempList (e.g. "You received these cards"); a request may replace it
			const list = [];
			const w = this.duel.wram();
			const base = W.wDuelTempList - 0xc000;
			for (let i = 0; i < 60 && w[base + i] !== 0xff; i++) list.push(w[base + i]);
			this.showOverlay('cardlist', this.cardListView({ d: a[1], items: list.slice(0, a[2] || list.length), selectable: false }).wrap);
			break;
		}
		case SCREEN.LARGE_CARD:
			this.showOverlay('largecard', this.largeCard(a[2]));
			break;
		case SCREEN.COIN_TOSS:
			this.coins = [];
			this.coinTotal = a[2] || 1;
			this.showOverlay('coin', this.coinView());
			break;
		case SCREEN.ENERGY_DISCARD:
			break;
		default:
			break;
		}
		this.render();
		return undefined;
	}

	coinView() {
		const box = el('div', 'coin-box');
		box.addEventListener('click', () => {
			const control = this.controls[this.controls.length - 1];
			if (control && control.key) control.key(K.A);
		});
		this.$coinTitle = el('div', 'coin-title', this.coinTotal > 1 ? `Coin 1 of ${this.coinTotal}` : 'Coin toss');
		box.appendChild(this.$coinTitle);
		const stage = el('div', 'coin-stage');
		stage.appendChild(el('div', 'coin-shadow'));
		this.$coin = el('div', 'coin heads');
		this.$coin.setAttribute('role', 'img');
		this.$coin.setAttribute('aria-label', 'Coin ready');
		stage.appendChild(this.$coin);
		box.appendChild(stage);
		this.$coinResult = el('div', 'coin-result');
		box.appendChild(this.$coinResult);
		this.$coinRow = el('div', 'coin-row');
		box.appendChild(this.$coinRow);
		return box;
	}

	async onCoin(a) {
		if (this.overlayKind !== 'coin') { this.coins = []; this.coinTotal = a[2] || 1; this.showOverlay('coin', this.coinView()); }
		const heads = a[0] === C.HEADS;
		this.$coinTitle.textContent = a[2] > 1 ? `Coin ${a[1]} of ${a[2]}` : 'Coin toss';
		this.$coinResult.textContent = '';
		const motion = this.pacing && !this.skipping && !this.animationsDisabled && !(this.skipDelay && this.bHeld);
		if (motion) {
			if (this.onCoinSound) this.onCoinSound();
			let skip = false;
			const control = { key: (k) => { if (k === K.A || k === K.B) skip = true; return true; } };
			this.controls.push(control);
			try {
				const widths = [1, 0.75, 0.125, 0.75, 1, 0.75, 0.125, 0.75];
				const started = performance.now(), duration = 20 * 1000 / 30;
				for (;;) {
					const elapsed = performance.now() - started;
					if (elapsed >= duration) break;
					const frame = Math.min(19, Math.floor(elapsed * 30 / 1000));
					if (skip || this.skipping || (this.skipDelay && this.bHeld) || !this.$coin.isConnected) break;
					const spin = frame < 16, phase = frame % 8;
					const face = spin ? phase < 4 : heads;
					const rise = spin ? frame * (16 - frame) * 20 / 64 : -[3, 1, 0, 0][frame - 16];
					this.$coin.className = 'coin ' + (face ? 'heads' : 'tails');
					this.$coin.style.transform = `translateY(${-rise * this.$coin.clientWidth / 48}px) scaleX(${spin ? widths[phase] : 1})`;
					await delay(Math.min(1000 / 30, duration - elapsed));
				}
			} finally {
				const at = this.controls.indexOf(control);
				if (at >= 0) this.controls.splice(at, 1);
			}
		}
		this.coins.push(heads);
		this.$coin.className = 'coin ' + (heads ? 'heads' : 'tails');
		this.$coin.style.transform = '';
		this.$coin.setAttribute('aria-label', heads ? 'Heads' : 'Tails');
		this.$coinResult.textContent = heads ? 'Heads' : 'Tails';
		const chip = el('span', 'coin-chip ' + (heads ? 'heads' : 'tails'), heads ? 'H' : 'T');
		this.$coinRow.appendChild(chip);
		while(this.$coinRow.children.length>12)this.$coinRow.firstElementChild.remove();
		return undefined;
	}

	onAnim(a) {
		const [id, isDuelAnim, side, loc] = a;
		if (isDuelAnim) {
			if (id === C.DUEL_ANIM_COIN_SPIN && this.$coin) {
				this.$coin.className = 'coin heads';
				this.$coin.style.transform = '';
				this.$coinResult.textContent = '';
				this.$coinTitle.textContent = this.coinTotal > 1 ? `Coin ${this.coins.length + 1} of ${this.coinTotal}` : 'Coin toss';
				this.$coin.setAttribute('aria-label', 'Coin ready');
				return undefined;
			}
			if (id >= C.DUEL_ANIM_PLAYER_SHUFFLE && id <= C.DUEL_ANIM_BOTH_SHUFFLE) {
				this.flash('shuffle');
				return this.pace(300);
			}
			return undefined;
		}
		if (!id) return undefined;
		this.render();
		const target = this.slotElement(side, loc & 0x7f);
		if (target) {
			target.classList.remove('hit');
			void target.offsetWidth;
			target.classList.add('hit');
		}
		return this.pace(350);
	}

	floatText(side, slot, text, cls) {
		const t = this.slotElement(side, slot);
		if (!t) return;
		const r = t.getBoundingClientRect(), base = this.$fieldWrap.getBoundingClientRect();
		const f = el('div', 'float ' + cls, text);
		f.style.left = `${r.left - base.left + r.width / 2}px`;
		f.style.top = `${r.top - base.top + r.height / 3}px`;
		this.$fx.appendChild(f);
		setTimeout(() => f.remove(), 1200);
	}

	flash(cls) {
		this.$fieldWrap.classList.remove(cls);
		void this.$fieldWrap.offsetWidth;
		this.$fieldWrap.classList.add(cls);
	}

	// ---------------------------------------------------------------- requests
	/* answer a request: opts.remote = the answer comes from the network (show nothing to pick) */
	onRequest(req, opts = {}) {
		this.req = req;
		if (req.type !== REQ.CARD_LIST) this.pendingCardSelection = null;
		if (req.type !== REQ.ACK) {
			this.hidePracticeReading();
			this.setPerspective(req.duelist);
			this.hideBanner();
			this.setStatus('');
		}
		if (req.type === REQ.ACK) return this.reqAck(req, opts);
		this.render();
		switch (req.type) {
		case REQ.DUEL_MENU: return this.reqDuelMenu(req);
		case REQ.CARD_LIST: return this.reqCardList(req);
		case REQ.CARD_ITEM_MENU: return this.reqCardItemMenu(req);
		case REQ.PLAY_AREA: return this.reqPlayArea(req);
		case REQ.MENU: return this.reqMenu(req);
		case REQ.YES_NO: return this.reqYesNo(req);
		case REQ.ENERGY_DISCARD: return this.reqEnergyDiscard(req);
		case REQ.PRIZE: return this.reqPrize(req);
		case REQ.COLOR: return this.reqColor(req);
		case REQ.NUMBER: return this.reqNumber(req);
		case REQ.CONFIRM: return this.reqConfirm(req);
		default: return Promise.resolve(TCG_CANCEL);
		}
	}

	/* run a control until it calls done(answer) */
	ask(make) {
		return new Promise((resolve) => {
			let ctl = null;
			const done = (v) => {
				if (!ctl) return;
				this.popControl(ctl);
				ctl = null;
				this.clearMenu();
				resolve(v);
			};
			ctl = this.pushControl(make(done) || {});
		});
	}

	reqAck(req, opts) {
		if (this.skipping) return 0;
		this.$ack.classList.remove('hidden');
		const remote = !!opts.remote;
		if (remote) this.$ack.classList.add('remote');
		const p = this.ask((done) => {
			const advance = () => {
				if (this.practiceReading && this.practicePage + 1 < this.practicePages.length) {
					this.practicePage++;
					this.paintPracticeReading();
				} else done(0);
			};
			const ctl = {
				key: (k) => { if (k === K.A || k === K.B) advance(); return true; },
				textboxClick: advance,
				overlayClick: advance,
			};
			this.$overlay.onclick = (e) => { if (!e.target.closest('button')) advance(); };
			if (remote) setTimeout(() => done(0), this.remoteAckDelay);
			return ctl;
		});
		return p.then((v) => {
			this.$overlay.onclick = null;
			this.$ack.classList.add('hidden');
			this.$ack.classList.remove('remote');
			if (this.overlayKind === 'largecard' || this.overlayKind === 'cardpage') this.hideOverlay();
			return v;
		});
	}

	reqDuelMenu(req) {
		this.hideOverlay();
		this.setText('');
		return this.ask((done) => {
			const items = DUEL_MENU_TEXTS.map((t) => ({ label: this.duel.text(T[t]) }));
			const grid = items.map((_, i) => { for (let r = 0; r < 2; r++) for (let c = 0; c < 3; c++) if (DUEL_MENU_GRID[r][c] === i) return [r, c]; return [0, 0]; });
			this.$textbox.classList.add('menu-only');
			const ctl = this.textMenu({
				items, cursor: req.cursor, grid, cls: 'duel-menu',
				onSelect: (i) => {
					if (i === 2) { this.openCheckMenu(req.duelist); return; } // Check: browse locally
					done(i);
				},
			});
			ctl.onStart = () => {
				const s = this.sideState(req.duelist).slots[0];
				if (s) this.showCardPage(s.card, { hp: s.hp });
			};
			this.$textbox.classList.add('menu-only');
			return ctl;
		});
	}

	/* the Check menu of the original (In Play Area / Your Play Area / Opp. Play Area / discard piles), UI only */
	openCheckMenu(me) {
		const opp = other(me);
		const entries = [
			['Your Play Area', () => this.browsePlayArea(me)],
			['Opp. Play Area', () => this.browsePlayArea(opp)],
			['Your Hand', () => this.browseCards(me, this.handCards(me), `${this.name(me)}'s Hand`)],
			['Your Discard Pile', () => this.browseCards(me, this.cardsAt(me, C.CARD_LOCATION_DISCARD_PILE), 'Your Discard Pile')],
			['Opp. Discard Pile', () => this.browseCards(opp, this.cardsAt(opp, C.CARD_LOCATION_DISCARD_PILE), `${this.name(opp)}'s Discard Pile`)],
		];
		const box = el('div', 'check-menu');
		box.appendChild(el('div', 'cl-header', 'Check'));
		const list = el('div', 'check-items');
		box.appendChild(list);
		const els = entries.map(([label, fn], i) => {
			const b = el('button', 'tm-item', label);
			b.addEventListener('click', (e) => { e.stopPropagation(); move(i); open(); });
			list.appendChild(b);
			return b;
		});
		const prevOverlay = this.overlayKind;
		this.showOverlay('check', box);
		const move = (i) => { ctl.cursor = (i + els.length) % els.length; els.forEach((b, k) => b.classList.toggle('cur', k === ctl.cursor)); };
		const open = async () => {
			ctl.busy = true;
			await entries[ctl.cursor][1]();
			ctl.busy = false;
			this.showOverlay('check', box);
		};
		const close = () => { this.popControl(ctl); if (prevOverlay !== 'check') this.hideOverlay(); };
		const ctl = this.pushControl({
			cursor: 0,
			key: (k) => {
				if (ctl.busy) return true;
				if (k === K.UP) move(ctl.cursor - 1);
				else if (k === K.DOWN) move(ctl.cursor + 1);
				else if (k === K.A) open();
				else if (k === K.B) close();
				return true;
			},
		});
		const back = el('button', 'btn small', 'Back');
		back.addEventListener('click', (e) => { e.stopPropagation(); close(); });
		box.appendChild(back);
		move(0);
	}

	browsePlayArea(d) {
		return new Promise((resolve) => {
			let ctl;
			const close = () => { this.popControl(ctl); resolve(); };
			ctl = this.pushControl(this.playAreaView({
				d, selectable: () => true,
				onSelect: (slot) => { const s = this.sideState(d).slots[slot]; if (s) this.showCardPage(s.card, { hp: s.hp }); },
				onCancel: close,
			}));
			const back = el('button', 'btn small', 'Back');
			back.addEventListener('click', (e) => { e.stopPropagation(); close(); });
			ctl.wrap.appendChild(back);
			this.showOverlay('playarea', ctl.wrap);
		});
	}

	browseCards(d, items, header) {
		return new Promise((resolve) => {
			let ctl;
			const close = () => { this.popControl(ctl); resolve(); };
			ctl = this.pushControl(this.cardListView({
				d, items, header,
				onSelect: (i) => this.showCardPage(this.duel.card(this.duel.cardId(d, items[i]))),
				onCancel: close,
			}));
			const back = el('button', 'btn small', 'Back');
			back.addEventListener('click', (e) => { e.stopPropagation(); close(); });
			ctl.wrap.appendChild(back);
			this.showOverlay('cardlist', ctl.wrap);
		});
	}

	reqCardList(req) {
		const items = Array.from(req.items.slice(0, req.numItems));
		const viewOnly = !!req.arg[0];
		const pending = this.pendingCardSelection;
		this.pendingCardSelection = null;
		const reselect = pending && pending.owner === req.shown && pending.menuType === req.arg[1] && !viewOnly
			? items.indexOf(pending.deckIndex) : -1;
		this.setText(req.text2Str || this.text);
		return this.ask((done) => {
			const ctl = this.cardListView({
				d: req.shown, items, header: req.textStr, cursor: reselect >= 0 ? reselect : req.cursor,
				onSelect: (i) => done(i),
				onCancel: () => done(TCG_CANCEL),
			});
			const bar = el('div', 'cl-buttons');
			const ok = el('button', 'btn', viewOnly ? 'Check' : 'Select');
			ok.addEventListener('click', (e) => { e.stopPropagation(); ctl.select(); });
			const back = el('button', 'btn', 'Back');
			back.addEventListener('click', (e) => { e.stopPropagation(); done(TCG_CANCEL); });
			bar.append(ok, back);
			ctl.wrap.appendChild(bar);
			this.showOverlay('cardlist', ctl.wrap);
			this.cardListCtl = ctl;
			ctl.menuType = req.arg[1];
			// An explicit click on another hand card cancels Play/Check first.
			// Replay only that click once the C loop requests the same list again.
			if (reselect >= 0) queueMicrotask(() => {
				if (this.controls.at(-1) === ctl) ctl.select();
			});
			return ctl;
		});
	}

	reqCardItemMenu(req) {
		const labels = (req.textStr || 'Play\nCheck').split('\n').map((s) => s.trim()).filter(Boolean);
		const list = this.cardListCtl;
		if (list && list.rows) {
			// keep the list visible with the chosen card highlighted
			list.rows.forEach((r) => r.row.classList.remove('picked'));
			const cur = list.rows[list.cursor];
			if (cur) cur.row.classList.add('picked');
			list.wrap.classList.add('has-picked');
		}
		this.setText(cardTitle(this.duel.card(this.duel.cardId(req.shown, req.arg[0]))));
		return this.ask((done) => {
			if (list && list.owner === req.shown && this.$overlay.contains(list.wrap)) list.pointerSelect = i => {
				const deckIndex = list.deckIndexes[i];
				if (deckIndex === req.arg[0]) return;
				this.pendingCardSelection = { owner: req.shown, deckIndex, menuType: list.menuType };
				done(TCG_CANCEL);
			};
			return this.textMenu({
				items: labels.slice(0, 2).map((label) => ({ label })), cursor: 0, cls: 'item-menu',
				onSelect: (i) => done(i), onCancel: () => done(TCG_CANCEL),
			});
		}).finally(() => { if (list) list.pointerSelect = null; });
	}

	reqPlayArea(req) {
		const first = req.arg[0], last = req.arg[0] + req.numItems - 1;
		const viewOnly = !!req.arg[1];
		return this.ask((done) => {
			const ctl = this.playAreaView({
				d: req.shown, cursorSlot: req.cursor,
				selectable: (slot) => slot >= first && slot <= last,
				onSelect: (slot) => done(slot), onCancel: () => done(TCG_CANCEL),
			});
			const bar = el('div', 'cl-buttons');
			const back = el('button', 'btn', viewOnly ? 'Back' : 'Cancel');
			back.addEventListener('click', (e) => { e.stopPropagation(); done(TCG_CANCEL); });
			bar.appendChild(back);
			ctl.wrap.appendChild(bar);
			this.showOverlay('playarea', ctl.wrap);
			return ctl;
		});
	}

	reqMenu(req) {
		const n = req.numItems;
		if (req.menu === MENU.ATTACKS) return this.attackMenu(req);
		if (req.menu === MENU.PEEK_AREA) return this.peekAreaMenu(req);
		if (req.menu === MENU.TWO_ITEMS) return this.twoItemMenu(req);
		if (req.menu === MENU.PEEK_TARGET) return this.peekTargetMenu(req);
		if (n === 6 && COLOR_MENU_TEXTS.some((t) => T[t] === this.lastTextId)) {
			return this.colorMenu(req, COLOR_MENU_ORDER, (i) => i, req.cursor);
		}
		if (req.menu === MENU.PLAY_AREA || (req.menu === MENU.GENERIC && this.screen === SCREEN.PLAY_AREA)) {
			// TCG_MENU_PLAY_AREA: item i is location arg[0] + i (wExcludeArenaPokemon)
			const exclude = req.menu === MENU.PLAY_AREA ? req.arg[0] : (this.playAreaExclude ? 1 : 0);
			const pkmnPower = this.pkmnPowerScreen;
			return this.ask((done) => {
				const ctl = this.playAreaView({
					d: req.shown, cursorSlot: req.cursor + exclude, pkmnPower,
					selectable: (slot) => slot - exclude >= 0 && slot - exclude < n,
					onSelect: (slot) => done(slot - exclude), onCancel: () => done(TCG_CANCEL),
				});
				const bar = el('div', 'cl-buttons');
				const back = el('button', 'btn', 'Cancel');
				back.addEventListener('click', (e) => { e.stopPropagation(); done(TCG_CANCEL); });
				bar.appendChild(back);
				ctl.wrap.appendChild(bar);
				this.showOverlay('playarea', ctl.wrap);
				return ctl;
			});
		}
		if (req.menu === MENU.CARD_LIST || (req.menu === MENU.GENERIC && this.screen === SCREEN.CARD_LIST)) {
			const items = Array.from(req.items.slice(0, n));
			return this.ask((done) => {
				const ctl = this.cardListView({ d: req.shown, items, cursor: req.cursor, onSelect: (i) => done(i), onCancel: () => done(TCG_CANCEL) });
				this.showOverlay('cardlist', ctl.wrap);
				return ctl;
			});
		}
		// unknown context: numbered choices under the last text
		return this.ask((done) => this.textMenu({
			items: Array.from({ length: n }, (_, i) => ({ label: `${i + 1}` })), cursor: req.cursor, cols: Math.min(n, 6),
			onSelect: (i) => done(i), onCancel: () => done(TCG_CANCEL),
		}));
	}

	attackMenu(req) {
		const n = req.numItems;
		const attacks = [];
		for (let i = 0; i < n; i++) {
			const card = this.duel.card(this.duel.cardId(req.shown, req.items[i * 2]));
			const atk = card.attacks.find((a) => a.index === req.items[i * 2 + 1]) || card.attacks[0];
			attacks.push({ card, atk });
		}
		const info = el('div', 'attack-info');
		this.showOverlay('attackinfo', info);
		const showInfo = (i) => {
			const { card, atk } = attacks[i];
			clear(info);
			const head = el('div', 'ai-head');
			head.append(costIcons(atk.cost), el('span', 'ai-name', atk.name));
			if (atk.damage) head.appendChild(el('span', 'ai-dmg', String(atk.damage) + ['', '+', '-', '×', ''][atk.category & 7]));
			info.appendChild(head);
			const d = el('div', 'ai-desc');
			d.appendChild(richText(reflowRules(atk.description) || 'No additional effect.'));
			info.appendChild(d);
			info.appendChild(el('div', 'ai-card', cardTitle(card)));
		};
		this.setText('');
		return this.ask((done) => {
			const ctl = this.textMenu({
				items: attacks.map(({ atk }) => {
					const node = el('span', 'atk-item');
					node.append(costIcons(atk.cost), el('span', 'atk-name', atk.name), el('span', 'atk-dmg', atk.damage ? String(atk.damage) + ['', '+', '-', '×', ''][atk.category & 7] : ''));
					return { node };
				}),
				cursor: req.cursor, cls: 'attack-menu',
				onSelect: (i) => done(i), onCancel: () => done(TCG_CANCEL),
			});
			this.$textbox.classList.add('menu-only');
			ctl.onMove = showInfo;
			showInfo(ctl.cursor);
			ctl.onStart = (i) => this.showCardPage(attacks[i].card);
			return ctl;
		}).then((v) => { if (this.overlayKind === 'attackinfo') this.hideOverlay(); return v; });
	}

	/* colors: list of colors in menu order; answers toAnswer(index); cursorIndex = initial item */
	colorMenu(req, colors, toAnswer, cursorIndex = 0) {
		return this.ask((done) => this.textMenu({
			items: colors.map((c) => { const node = el('span'); node.append(esym(c), document.createTextNode(' ' + COLOR_NAMES[c])); return { node }; }),
			cursor: Math.max(0, Math.min(cursorIndex, colors.length - 1)), cols: 3,
			onSelect: (i) => done(toAnswer(i)), onCancel: () => done(TCG_CANCEL),
		}));
	}

	/* the two items are on the last line of the text, e.g. "Please select the Deck:\n   Yours   Opponent's" */
	splitTwoItems(text) {
		const lines = (text || '').split('\n');
		const parts = lines[lines.length - 1].trim().split(/\s{2,}/);
		if (parts.length === 2) return { labels: parts, prompt: lines.slice(0, -1).join('\n') };
		return null;
	}

	/* TwoItemHorizontalMenu: 0 = left item, 1 = right item */
	twoItemMenu(req) {
		const split = this.splitTwoItems(req.textStr || this.text) || { labels: ['1', '2'], prompt: req.textStr || this.text };
		this.setText(split.prompt);
		return this.ask((done) => this.textMenu({
			items: split.labels.map((label) => ({ label })), cursor: req.cursor ? 1 : 0, cols: 2, cls: 'yes-no two-items',
			onSelect: (i) => done(i),
		}));
	}

	reqYesNo(req) {
		let prompt = req.textStr || this.text;
		let labels = [this.duel.text(T.YesText) || 'Yes', this.duel.text(T.NoText) || 'No'];
		if (req.arg[0]) { // older engines sent TwoItemHorizontalMenu as a yes/no request
			const split = this.splitTwoItems(prompt);
			if (split) ({ labels, prompt } = split);
		}
		this.setText(prompt);
		return this.ask((done) => this.textMenu({
			items: labels.map((label) => ({ label })), cursor: req.cursor === 1 ? 0 : 1, cols: 2, cls: 'yes-no',
			onSelect: (i) => done(i === 0 ? 1 : 0), onCancel: () => done(0),
		}));
	}

	reqEnergyDiscard(req) {
		const items = Array.from(req.items.slice(0, req.numItems));
		const loc = req.arg[2];
		return this.ask((done) => {
			const st = this.sideState(req.shown).slots[loc];
			const box = el('div', 'energy-discard');
			if (st) {
				const head = el('div', 'ed-head');
				head.appendChild(art(st.card.id, 1));
				const info = el('div', 'ed-info');
				info.appendChild(el('div', 'pa-name', cardTitle(st.card)));
				info.appendChild(el('div', '', `HP ${st.hp}/${st.card.hp}`));
				info.appendChild(el('div', 'ed-progress', `Selected ${req.arg[0]} / ${req.arg[1]}`));
				head.appendChild(info);
				box.appendChild(head);
			}
			const ctl = this.cardListView({ d: req.shown, items, header: req.textStr, onSelect: (i) => done(i), onCancel: () => done(TCG_CANCEL) });
			box.appendChild(ctl.wrap);
			const bar = el('div', 'cl-buttons');
			const back = el('button', 'btn', 'Cancel');
			back.addEventListener('click', (e) => { e.stopPropagation(); done(TCG_CANCEL); });
			bar.appendChild(back);
			box.appendChild(bar);
			this.showOverlay('energy', box);
			return ctl;
		});
	}

	reqPrize(req) {
		const mask = req.arg[0];
		this.setText(req.textStr || this.text);
		return this.ask((done) => {
			const box = el('div', 'prizes-box');
			box.appendChild(el('div', 'cl-header event-heading', plainText(req.textStr || 'Choose a Prize')));
			const grid = el('div', 'prize-grid');
			const els = [];
			for (let i = 0; i < 6; i++) {
				const p = el('button', 'prize-card' + (mask & (1 << i) ? '' : ' taken'));
				p.appendChild(sym(SYM_PRIZE));
				p.addEventListener('click', (e) => { e.stopPropagation(); if (mask & (1 << i)) done(i); });
				grid.appendChild(p);
				els.push(p);
			}
			box.appendChild(grid);
			this.showOverlay('prize', box);
			let cur = req.cursor;
			const move = (i) => { cur = (i + 6) % 6; els.forEach((e, k) => e.classList.toggle('cur', k === cur)); };
			move(cur);
			return {
				key: (k) => {
					if (k === K.LEFT) move(cur - 1);
					else if (k === K.RIGHT) move(cur + 1);
					else if (k === K.UP || k === K.DOWN) move((cur + 3) % 6);
					else if (k === K.A && (mask & (1 << cur))) done(cur);
					return true;
				},
			};
		});
	}

	/* TCG_REQ_COLOR: answer a color (FIRE..PSYCHIC). items (if any) = the colors in menu order
	 * (e.g. ShiftListItemToColor for the color change screen), cursor = the default color */
	reqColor(req) {
		let colors = Array.from(req.items.slice(0, req.numItems)).filter((c) => c < 7);
		if (!req.numItems || colors.length !== req.numItems) colors = [0, 1, 2, 3, 4, 5];
		return this.colorMenu(req, colors, (i) => colors[i], colors.indexOf(req.cursor));
	}

	/* WaitForButtonAorB: A = go on (0), B = stop (TCG_CANCEL), e.g. Devolution Spray's "one more stage?" */
	reqConfirm(req) {
		return this.ask((done) => this.textMenu({
			items: [{ label: 'Continue (A)' }, { label: 'Stop (B)' }], cursor: 0, cols: 2, cls: 'confirm',
			onSelect: (i) => done(i === 0 ? 0 : TCG_CANCEL), onCancel: () => done(TCG_CANCEL),
		}));
	}

	/* Peek (HandlePeekSelection): which play area */
	peekAreaMenu(req) {
		return this.ask((done) => this.textMenu({
			items: [{ label: req.shown === this.perspective ? 'Your Play Area' : `${this.name(req.shown)}'s Play Area` },
				{ label: req.shown === this.perspective ? 'Opp. Play Area' : `${this.name(other(req.shown))}'s Play Area` }],
			cursor: req.cursor, cols: 1, onSelect: (i) => done(i), // B does nothing here in the original
		}));
	}

	/* Peek: a prize (0-5), the hand (6, the other duelist only) or the top card of the deck (7) */
	peekTargetMenu(req) {
		const mask = req.arg[0], hand = !!req.arg[1];
		const st = this.sideState(req.shown);
		this.setText(req.textStr || this.text);
		return this.ask((done) => {
			const box = el('div', 'prizes-box');
			box.appendChild(el('div', 'cl-header', `${req.shown === this.perspective ? 'Your' : `${this.name(req.shown)}'s`} cards`));
			const grid = el('div', 'prize-grid');
			const extra = el('div', 'peek-extra');
			const items = [];
			const add = (parent, i, b, ok) => {
				if (!ok) b.classList.add('taken');
				b.addEventListener('click', (e) => { e.stopPropagation(); if (ok) done(i); });
				parent.appendChild(b);
				items.push({ i, b, ok });
			};
			for (let i = 0; i < 6; i++) {
				const p = el('button', 'prize-card');
				p.appendChild(sym(SYM_PRIZE));
				add(grid, i, p, !!(mask & (1 << i)));
			}
			if (hand) add(extra, 6, el('button', 'btn', `Hand (${st.hand})`), st.hand > 0);
			add(extra, 7, el('button', 'btn', `Top of the deck (${st.deck})`), st.deck > 0);
			const back = el('button', 'btn small', 'Back');
			back.addEventListener('click', (e) => { e.stopPropagation(); done(TCG_CANCEL); });
			box.append(grid, extra, back);
			this.showOverlay('prize', box);
			let cur = 0;
			const move = (k) => { if (k < 0) return; cur = (k + items.length) % items.length; items.forEach((x, j) => x.b.classList.toggle('cur', j === cur)); };
			move(Math.max(0, items.findIndex((x) => x.i === req.cursor)));
			return {
				key: (k) => {
					if (k === K.LEFT || k === K.UP) move(cur - 1);
					else if (k === K.RIGHT || k === K.DOWN) move(cur + 1);
					else if (k === K.A && items[cur].ok) done(items[cur].i);
					else if (k === K.B) done(TCG_CANCEL);
					return true;
				},
			};
		});
	}

	reqNumber(req) {
		return this.ask((done) => this.textMenu({
			items: Array.from({ length: req.numItems }, (_, i) => ({ label: String(i) })), cursor: req.cursor, cols: Math.min(req.numItems, 6),
			onSelect: (i) => done(i), onCancel: () => done(TCG_CANCEL),
		}));
	}

	/* online: the other duelist is choosing. Their menus are not shown: back to the board. */
	showWaiting(req, who) {
		if (['cardlist', 'playarea', 'energy', 'attackinfo', 'prize', 'check'].includes(this.overlayKind)) this.hideOverlay();
		const what = {
			[REQ.DUEL_MENU]: 'to choose an action', [REQ.CARD_LIST]: 'to choose a card', [REQ.CARD_ITEM_MENU]: 'to choose a card',
			[REQ.PLAY_AREA]: 'to choose a Pokémon', [REQ.MENU]: 'to choose', [REQ.YES_NO]: 'to answer', [REQ.ENERGY_DISCARD]: 'to choose an Energy card',
			[REQ.PRIZE]: 'to take a Prize', [REQ.COLOR]: 'to choose a color', [REQ.NUMBER]: 'to choose a number',
		}[req.type] || '';
		this.setStatus(`Waiting for ${who} ${what}…`, 'waiting');
	}
}
