// Shared N64 title composition; original GBC animation coordinates stay intact.
export const INTRO_VIEW = { width: 320, height: 240, left: 40, top: 12, scale: 1.5 };
export const TITLE_LOGO = { left: 20, top: 12, width: 280, height: 140 };

export function drawIntroBackdrop(ctx, pictures, scene) {
	ctx.fillStyle = '#e7e7c6';
	ctx.fillRect(0, 0, INTRO_VIEW.width, INTRO_VIEW.height);
	ctx.imageSmoothingEnabled = true;
	if (scene < 3) {
		ctx.drawImage(pictures[['charizard', 'scyther', 'aerodactyl'][scene]],
			INTRO_VIEW.left, INTRO_VIEW.top, 160 * INTRO_VIEW.scale, 144 * INTRO_VIEW.scale);
	} else {
		ctx.drawImage(pictures.title64, TITLE_LOGO.left, TITLE_LOGO.top, TITLE_LOGO.width, TITLE_LOGO.height);
		// The original footer is still point-filtered from exactly the same crop.
		ctx.imageSmoothingEnabled = false;
		ctx.drawImage(pictures.title, 0, 110, 160, 34, INTRO_VIEW.left,
			INTRO_VIEW.top + 110 * INTRO_VIEW.scale, 160 * INTRO_VIEW.scale, 34 * INTRO_VIEW.scale);
		ctx.imageSmoothingEnabled = true;
	}
}

export function beginIntroSprites(ctx) {
	ctx.save();
	ctx.beginPath();
	ctx.rect(INTRO_VIEW.left, INTRO_VIEW.top, 160 * INTRO_VIEW.scale, 144 * INTRO_VIEW.scale);
	ctx.clip();
	ctx.translate(INTRO_VIEW.left, INTRO_VIEW.top);
	ctx.scale(INTRO_VIEW.scale, INTRO_VIEW.scale);
}
