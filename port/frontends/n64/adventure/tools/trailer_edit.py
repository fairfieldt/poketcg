#!/usr/bin/env python3
"""Reproduce the private trailer edit from captured shader output and original audio."""
from pathlib import Path
import argparse
import json
import shlex
import subprocess

HOST = 'deck@steamdeck.tdf.sh'
REMOTE = '/home/deck/.cache/poketcg-trailer-20261031'
ROOT = Path(__file__).resolve().parents[5]
OUT = ROOT / 'port/reviews/trailer'
PLAN = {
    'fps': 60,
    'size': [1440, 1080],
    'shots': [
        {'name': 'opening-and-sam', 'source': 'intro', 'start': 9.65, 'end': 78.30},
        {'name': 'ronald-knockout', 'source': 'ronald', 'start': 8.05, 'end': 26.75},
        {'name': 'lightning-walk', 'source': 'clubs', 'start': 11.50, 'end': 16.30},
        {'name': 'water-walk', 'source': 'clubs', 'start': 22.30, 'end': 28.50},
        {'name': 'fire-walk', 'source': 'clubs', 'start': 31.60, 'end': 33.95},
        {'name': 'water-medal', 'source': 'ending', 'start': 15.916666667, 'end': 21.383333333},
        {'name': 'hall-of-honor', 'source': 'ending', 'start': 25.10, 'end': 29.066666667},
        {'name': 'title64', 'source': 'ending', 'start': 29.066666667, 'end': 38.483333334},
    ],
    'dissolves': [.45, .35, .10, .10, .35, .25, .65],
    'audio': {
        'opening_gain_db': -.2,
        'ronald_gain_db': 2.5,
        'club_source': 'music_12.wav',
        'club_gain_db': -3.7,
        'medal_source': 'music_29.wav',
        'medal_gain_db': -5.2,
        'hall_source': 'music_17.wav',
        'hall_gain_db': 2.6,
        'title_source': 'music_01.wav',
        'title_start': 24.636719,
        'title_gain_db': -5.5,
    },
}


def remote(command, data=None):
    return subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST, command], input=data,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True).stdout


def render(draft=False, remix=False):
    """No invented graphics: only trim/dissolve/fade the actual shader frames."""
    work = REMOTE + '/edit'
    source_names = ['intro', 'ronald', 'clubs', 'ending']
    args = ['ffmpeg', '-hide_banner', '-y', '-filter_complex_threads', '2']
    for name in source_names:
        args += ['-i', f'{REMOTE}/{name}/proof.mkv']
    for name in ['music_12.wav', 'music_17.wav', 'music_01.wav', 'music_29.wav']:
        args += ['-i', f'{work}/{name}']
    if remix:
        args += ['-i', f'{work}/trailer.mp4']
    filters = ['[2:v]split=3[club0][club1][club2]',
               '[3:v]split=3[end0][end1][end2]']
    video_sources = ['0:v', '1:v', 'club0', 'club1', 'club2', 'end0', 'end1', 'end2']
    durations = []
    for i, (shot, src) in enumerate(zip(PLAN['shots'], video_sources)):
        # All cuts/dissolves land on the sixty-Hz recording grid.
        duration = round((shot['end'] - shot['start']) * 60) / 60
        durations.append(duration)
        filters.append(f'[{src}]trim=start={shot["start"]}:duration={duration},'
                       f'setpts=PTS-STARTPTS,fps=60,settb=1/60,format=yuv420p[v{i}]')
    cursor = durations[0]
    last = 'v0'
    shot_starts = [0.0]
    for i, fade in enumerate(PLAN['dissolves'], 1):
        offset = cursor - fade
        filters.append(f'[{last}][v{i}]xfade=transition=fade:duration={fade}:offset={offset:.9f}[x{i}]')
        shot_starts.append(offset)
        cursor += durations[i] - fade
        last = f'x{i}'
    filters.append(f'[{last}]fade=t=out:st={cursor-.8:.9f}:d=0.8[vout]')
    video_filter_count = len(filters)
    audio = PLAN['audio']
    club_duration = sum(durations[2:5]) - sum(PLAN['dissolves'][2:4])
    audio_cases = [
        ('0:a', PLAN['shots'][0]['start'], durations[0], audio['opening_gain_db']),
        ('1:a', PLAN['shots'][1]['start'], durations[1], audio['ronald_gain_db']),
        ('4:a', .016, club_duration, audio['club_gain_db']),
        ('7:a', 0, durations[5], audio['medal_gain_db']),
        ('5:a', 0, durations[6], audio['hall_gain_db']),
        ('6:a', audio['title_start'], durations[7], audio['title_gain_db']),
    ]
    audio_starts = [shot_starts[n] for n in (0, 1, 2, 5, 6, 7)]
    # Finish the whole captured fanfare before the Hall music enters, even
    # though its last quarter-second accompanies the visual dissolve.
    audio_starts[4] += .25
    audio_cases[4] = ('5:a', 0, durations[6]-.25, audio['hall_gain_db'])
    for i, (src, start, duration, gain) in enumerate(audio_cases):
        envelopes = []
        if i in (1, 2, 5):
            envelopes.append(f'afade=t=in:d={dict([(1,.45),(2,.35),(5,.65)])[i]}')
        if i in (0, 1, 2, 4):
            fade = dict([(0,.45),(1,.35),(2,.35),(4,.65)])[i]
            envelopes.append(f'afade=t=out:st={duration-fade}:d={fade}')
        envelope = (','.join(envelopes)+',') if envelopes else ''
        filters.append(f'[{src}]atrim=start={start}:duration={duration},asetpts=PTS-STARTPTS,'
                       f'aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo,'
                       f'volume={gain}dB,apad,atrim=duration={duration},'
                       f'{envelope}adelay={audio_starts[i]*1000:.6f}:all=1[a{i}]')
    filters.append(''.join(f'[a{i}]' for i in range(6))+
                   'amix=inputs=6:duration=longest:normalize=0[amix]')
    # Preserve original chiptune dynamics. Only a short end fade protects the
    # final PCM boundary; there is no limiter, compressor or loudness filter.
    filters.append(f'[amix]afade=t=in:d=0.005,afade=t=out:st={cursor-.12:.9f}:d=0.12[aout]')
    output = f'{work}/trailer{"-remix" if remix else "-draft" if draft else ""}.mp4'
    args += ['-filter_complex_script', f'{work}/filters.txt']
    if remix:
        filters = filters[video_filter_count:]
        args += ['-map', '8:v', '-c:v', 'copy']
    else:
        args += ['-map', '[vout]', '-c:v', 'libx264', '-threads', '4',
                 '-preset', 'fast' if draft else 'slow', '-crf', '21' if draft else '18',
                 '-pix_fmt', 'yuv420p']
    args += ['-map', '[aout]', '-c:a', 'aac', '-b:a', '192k', '-ar', '48000',
             '-movflags', '+faststart', '-t', f'{cursor:.9f}', output]
    timeline = {**PLAN, 'duration': cursor, 'shot_output_starts': shot_starts,
                'audio_processing': 'Linear gains, original stem substitutions and short crossfades only; no dynamics processing.',
                'video_processing': 'Trim/dissolve/end fade only; no speed change, generated frames, shader imitation or retouching.'}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'timeline.json').write_text(json.dumps(timeline, indent=2)+'\n')
    payload = {'filters.txt': ';\n'.join(filters), 'timeline.json': json.dumps(timeline, indent=2),
               'render.py': 'import os,subprocess\nfrom pathlib import Path\nos.nice(10)\n'
               f'with Path({work+"/render.log"!r}).open("w") as log:\n'
               f' subprocess.run({args!r},stdout=log,stderr=subprocess.STDOUT,check=True)\n'}
    setup = ('from pathlib import Path\nimport json\nw=Path('+repr(work)+')\nw.mkdir(exist_ok=True)\n'
             'files=json.loads('+repr(json.dumps(payload))+')\n'
             'for n,v in files.items():(w/n).write_text(v)\n')
    remote('python3 -', setup.encode())
    stems = ROOT/'port/frontends/n64/adventure/build/trailer-audio'
    for name in ['music_12.wav', 'music_17.wav', 'music_01.wav', 'music_29.wav']:
        with (stems/name).open('rb') as stream:
            subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST, 'cat > '+shlex.quote(work+'/'+name)],
                           stdin=stream, check=True)
    print(json.dumps({'output': output, 'duration_seconds': cursor, 'remote_render': work+'/render.py'}))
    print(remote('python3 '+shlex.quote(work+'/render.py')).decode())


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--draft', action='store_true')
    p.add_argument('--remix', action='store_true', help='Reuse the existing final video without re-encoding it')
    a = p.parse_args()
    render(a.draft, a.remix)
