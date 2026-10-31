// Resolution-independent versions of the seven original BOXMSG plaques.
export const DUEL_BANNER_LABELS = Object.freeze([
	"Player's turn",
	"Opponent's turn",
	'Between turns',
	'Decision',
	'Bench Pokémon',
	'Arena Pokémon',
	'Coin toss',
]);

export function duelBanner(id) {
	if (!Number.isInteger(id) || id < 0 || id >= DUEL_BANNER_LABELS.length) return null;
	const plaque = document.createElement('div');
	plaque.className = 'boxmsg duel-banner';
	plaque.dataset.message = String(id);
	plaque.setAttribute('role', 'status');
	plaque.setAttribute('aria-live', 'polite');
	plaque.setAttribute('aria-atomic', 'true');
	const label = document.createElement('span');
	label.className = 'duel-banner-label';
	label.textContent = DUEL_BANNER_LABELS[id];
	plaque.appendChild(label);
	return plaque;
}
