// The same original-driver sequences as the N64 build, streamed as Vorbis.
export class AdventureAudio {
	constructor() {
		this.context = null;
		this.music = null;
		this.musicId = 0;
		this.offset = 0;
		this.started = 0;
		this.paused = null;
		this.cache = new Map();
		this.generation = 0;
		this.ready = fetch('audio/audio.json')
			.then((r) => r.json())
			.then((j) => {
				this.tracks = j.tracks;
			});
	}
	async unlock() {
		if (!this.context) this.context = new AudioContext();
		await this.context.resume();
	}
	async buffer(kind, id) {
		await this.ready;
		const key = `${kind}:${id}`,
			track = this.tracks.find((t) => t.kind === kind && t.id === id);
		if (!track) return null;
		if (!this.cache.has(key))
			this.cache.set(
				key,
				fetch(`audio/${track.browser_file}`)
					.then((r) => r.arrayBuffer())
					.then((b) => this.context.decodeAudioData(b))
					.then((buffer) => ({ buffer, track })),
			);
		return this.cache.get(key);
	}
	async event(kind, id) {
		if (!this.context) return;
		if (kind === 0x4a) {
			if (this.music)
				this.paused = { id: this.musicId, offset: this.offset + this.context.currentTime - this.started };
			this.stop();
			return;
		}
		if (kind === 0x4b) {
			const p = this.paused;
			this.paused = null;
			if (p) await this.play(p.id, p.offset);
			return;
		}
		if (kind === 0 || kind === 0x48 || kind === 0x4c) {
			await this.play(id);
			return;
		}
		if (kind === 1 || kind === 0x49) {
			if (!id) {
				this.effect?.stop();
				return;
			}
			const found = await this.buffer('sfx', id);
			if (!found) return;
			this.effect?.stop();
			const s = this.context.createBufferSource(),
				gain = this.context.createGain();
			s.buffer = found.buffer;
			gain.gain.value = 0.5;
			s.connect(gain).connect(this.context.destination);
			this.effect = s;
			s.start();
		}
	}
	stop() {
		this.generation++;
		if (this.music) {
			try {
				this.music.stop();
			} catch {}
			this.music = null;
		}
		this.musicId = 0;
	}
	async play(id, offset = 0) {
		if (id === this.musicId && this.music) return;
		this.stop();
		if (!id) return;
		const generation = this.generation;
		const found = await this.buffer('music', id);
		if (!found || generation !== this.generation) return;
		const s = this.context.createBufferSource(),
			gain = this.context.createGain();
		s.buffer = found.buffer;
		const start = found.track.loop_start_sample / found.track.rate;
		if (found.track.loop_ticks) {
			s.loop = true;
			s.loopStart = start;
			s.loopEnd = found.buffer.duration;
		}
		gain.gain.value = 0.32;
		s.connect(gain).connect(this.context.destination);
		this.music = s;
		this.musicId = id;
		this.offset = offset % found.buffer.duration;
		this.started = this.context.currentTime;
		s.start(0, this.offset);
		s.onended = () => {
			if (this.music === s) {
				this.music = null;
				this.musicId = 0;
			}
		};
	}
	wait() {
		if (!this.music || this.music.loop) return Promise.resolve();
		return new Promise((resolve) => {
			const s = this.music,
				previous = s.onended;
			s.onended = () => {
				previous?.();
				resolve();
			};
		});
	}
}
