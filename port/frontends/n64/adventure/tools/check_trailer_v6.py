#!/usr/bin/env python3
"""Validate the complete v6 edit before replacing the current teaser files."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
from check_trailer_pair import boxes

ADV = Path(__file__).resolve().parents[1]
ROOT = ADV.parents[3]
BASE = ROOT/'port/reviews/trailer'
OUT = BASE/'v6'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    video = OUT/'poketcg64-trailer.mp4'
    timeline = json.loads((OUT/'timeline.json').read_text())
    probe = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(video)]))
    (OUT/'ffprobe.json').write_text(json.dumps(probe, indent=2)+'\n')
    v = next(s for s in probe['streams'] if s['codec_type'] == 'video')
    a = next(s for s in probe['streams'] if s['codec_type'] == 'audio')
    assert (v['width'], v['height'], v['r_frame_rate'], int(v['nb_frames'])) == (
        1440, 1080, '60/1', timeline['frames']), v
    assert abs(float(v['duration'])-timeline['duration']) < 1/60
    assert abs(float(v['duration'])-float(a['duration'])) < 1/60
    names = [s['name'] for s in timeline['shots']]
    assert len(names) == 11 and names.index('ronald-source-dialogue')+1 == names.index('ronald-attack-knockout')
    hall = next(s for s in timeline['shots'] if s['name'] == 'hall-legendary-reveal')
    assert hall['native_start'] < timeline['legendary_glow_native_start'] < hall['native_end']-3.5
    recipe = json.loads((OUT/'audio-recipe.json').read_text())
    cue = next(c for c in recipe['cases'] if c['name'] == 'original-legendary-cards-accent')
    assert abs(cue['offset']-(hall['output_start']+timeline['legendary_glow_native_start']-hall['native_start'])) < 1/60
    for source in recipe['verified_original_driver_PCM']:
        assert sha(ROOT/source['path']) == source['wav_sha256']
    decoded = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(video), '-f', 'null', '-'], capture_output=True, text=True)
    assert decoded.returncode == 0 and not decoded.stderr, decoded.stderr
    audio = subprocess.run(['ffmpeg', '-hide_banner', '-i', str(video), '-vn', '-af',
                            'astats=metadata=0:reset=0,ebur128=peak=true', '-f', 'null', '-'], capture_output=True, text=True)
    assert audio.returncode == 0, audio.stderr
    (OUT/'audio-levels.log').write_text('\n'.join(line.rstrip() for line in audio.stderr.splitlines())+'\n')
    peaks = [float(x) for x in re.findall(r'Peak level dB:\s*(-?[0-9.]+)', audio.stderr)]
    true_peaks = [float(x) for x in re.findall(r'Peak:\s*(-?[0-9.]+) dBFS', audio.stderr)]
    assert peaks and true_peaks and max(peaks) < 0 and max(true_peaks) <= 0, (peaks, true_peaks)
    atoms = boxes(video)
    assert next(x['offset'] for x in atoms if x['type'] == 'moov') < next(x['offset'] for x in atoms if x['type'] == 'mdat')
    provenance = OUT/'provenance'
    provenance.mkdir(exist_ok=True)
    for segment in ('intro', 'ronald', 'clubs', 'amy'):
        capture = ADV/f'build/trailer-capture/v6-local-{segment}'
        metadata = json.loads((capture/'capture.json').read_text())
        log = (capture/'runtime.log').read_text()
        build = ADV/f'build/trailer-cast-{segment}/build.json'
        build_data = json.loads(build.read_text())
        assert 'VIDEO MODE width=480 height=360 ram=8388608 highres=1' in log
        assert 'TRAILER END '+segment in log and metadata['native_end_seen']
        assert metadata['coverage']['passed'] and metadata['probe_seconds'] is None
        # This edit uses continuously presented Ares windows. Reject the long
        # recorded gaps seen in v5; this is not a general native-FPS estimator.
        assert metadata['window_delivery_cadence']['gap_max_seconds'] < .1
        assert not any(s in log for s in ('ASSERTION FAILED', 'EXCEPTION:', 'ADVENTURE ERROR:'))
        assert metadata['rom_sha256'] == build_data['rom_sha256'] and build_data['embedded_character_files_verified']
        assert metadata['video_sha256'] == sha(capture/'proof.mov')
        recorder = metadata['recorder']
        assert not recorder['shows_cursor'] and not recorder['captures_audio'] and not recorder['capture_microphone']
        for source, name in ((build, 'build.json'), (capture/'runtime.log', 'runtime.log'),
                             (capture/'capture.json', 'capture.json'), (capture/'proof.cadence.json', 'cadence.json')):
            shutil.copyfile(source, provenance/f'{segment}-{name}')
    result = dict(passed=True, width=1440, height=1080, fps=60, native_framebuffer=[480,360],
                  native_target_fps=30, frames=timeline['frames'], duration=float(v['duration']),
                  audio_duration=float(a['duration']), sample_peak_db=max(peaks), true_peak_db=max(true_peaks),
                  original_audio_sources_verified=True, legendary_cue_synced=True, decode_errors=0, faststart=True,
                  video_sha256=sha(video), bytes=video.stat().st_size,
                  capture='Fresh local native post-VI window capture; no CRT, cursor, or motion interpolation.')
    (OUT/'validation.json').write_text(json.dumps(result, indent=2)+'\n')
    dialogue = next(s for s in timeline['shots'] if s['name'] == 'ronald-source-dialogue')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', str(dialogue['output_start']+1),
                    '-i', str(video), '-frames:v', '1', str(OUT/'poster.png')], check=True)
    for source, names in ((video, ('poketcg64-trailer.mp4', 'poketcg64-trailer-no-crt.mp4')),
                           (OUT/'poster.png', ('poster.png', 'poster-no-crt.png')),
                           (OUT/'timeline.json', ('timeline.json',)),
                           (OUT/'validation.json', ('validation.json',))):
        for name in names:
            temporary = BASE/(name+'.next')
            shutil.copyfile(source, temporary)
            temporary.replace(BASE/name)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
