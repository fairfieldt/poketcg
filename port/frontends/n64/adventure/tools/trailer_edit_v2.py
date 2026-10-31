#!/usr/bin/env python3
"""Matched native-clock CRT/None edits with one shared original-game audio mix."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shlex
import subprocess
from trailer_v2_clock import load_clock

ADV = Path(__file__).resolve().parents[1]
ROOT = ADV.parents[3]
LOCAL = ROOT/'port/reviews/trailer/v2'
CAPTURES = ADV/'build/trailer-capture'
REMOTE = '/home/deck/.cache/poketcg-trailer-20261031'
WORK = REMOTE+'/edit-v2'
HOST = 'deck@steamdeck.tdf.sh'
SOURCES = ('intro', 'ronald', 'clubs', 'amy')
FADES = (.45, .30, .08, .08, .35, .10, .12, .35, .25, .65)
PROFILES = {v: {s: f'v2-{v}-{s}' for s in SOURCES} for v in ('crt', 'none')}
if (LOCAL/'capture-sources.json').exists():
    PROFILES = json.loads((LOCAL/'capture-sources.json').read_text())


def ssh(command, data=None):
    return subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST, command], input=data,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True).stdout


def event(clock, contains):
    return next(e for e in clock.events if contains in e['line'])


def at(clock, contains):
    return event(clock, contains)['native_seconds']


def grid(value):
    return round(value*60)/60


def plan():
    clocks = {variant: {s: load_clock(CAPTURES/PROFILES[variant][s], presentation_latency=.04)
                       for s in SOURCES} for variant in ('crt', 'none')}
    c = clocks['crt']
    cuts = [
        ('opening-first-table', 'intro', at(c['intro'], 'TRAILER INTRO_RUN ')+.15,
         at(c['intro'], 'TRAILER END intro')-.10),
        ('ronald-knockout', 'ronald', at(c['ronald'], 'TRAILER FIRST_FRAME ')+.1,
         at(c['ronald'], 'TRAILER END ronald')-.1),
        ('lightning-walk', 'clubs', at(c['clubs'], 'TRAILER WALK map=15 ')-.25,
         at(c['clubs'], 'TRAILER WALK_COMPLETE map=15 ')+.8),
        ('fire-deep-walk', 'clubs', at(c['clubs'], 'TRAILER WALK map=27 ')-.6,
         at(c['clubs'], 'TRAILER WALK_COMPLETE map=27 ')+1.2),
        ('water-walk', 'clubs', at(c['clubs'], 'TRAILER WALK map=12 ')-.3,
         at(c['clubs'], 'TRAILER WALK_COMPLETE map=12 ')+.9),
        ('amy-agility', 'amy', at(c['amy'], 'TRAILER REQUEST index=3 '),
         at(c['amy'], 'TRAILER REQUEST index=3 ')+1.183333333),
        ('amy-final-pokemon-knockout', 'amy', at(c['amy'], 'TRAILER SHOT amy-knockout ')+.15,
         at(c['amy'], 'TRAILER DUEL_TEXT id=66 ')-.05),
        ('amy-real-win', 'amy', at(c['amy'], 'TRAILER REQUEST index=12 '),
         at(c['amy'], 'TRAILER SHOT amy-won ')-.1),
        ('water-medal', 'amy', at(c['amy'], 'TRAILER SHOT water-medal '),
         at(c['amy'], 'TRAILER SHOT water-medal-complete ')),
        ('hall-of-honor', 'amy', at(c['amy'], 'TRAILER SHOT HALL_OF_HONOR '),
         at(c['amy'], 'TRAILER SHOT title64 ')),
        ('title64', 'amy', at(c['amy'], 'TRAILER SHOT title64 ')+.1,
         at(c['amy'], 'TRAILER SHOT title64 ')+.1+9.416666667),
    ]
    shots = []
    cursor = 0
    for index, (name, source, start, end) in enumerate(cuts):
        start, end = grid(start), grid(end)
        if index: cursor -= FADES[index-1]
        shots.append({'name': name, 'source': source, 'native_start': start,
                      'native_end': end, 'duration': grid(end-start), 'output_start': grid(cursor)})
        cursor += end-start
    return clocks, {'fps': 60, 'size': [1440, 1080], 'duration': grid(cursor), 'shots': shots,
        'dissolves': FADES, 'clock': 'Native VI/60, source marker aligned; recorded frames held/dropped, no motion interpolation.',
        'presentation_latency_seconds': .04, 'phase_uncertainty': 'Approximately one 60 Hz frame; semantic rather than pixel-phase synchronization.',
        'audio': {'opening': 'One continuous original music09 stem; disrupted captured opening bed is entirely replaced.',
                  'clubs': 'One continuous original Club1 stem across Lightning, Fire, Water.',
                  'medal': 'Complete original5.396-second fanfare, unfaded; Hall music enters afterward.',
                  'title': 'Original closing phrase24.636719–34.05 seconds.',
                  'comparison': 'Common AAC encoded once, then copied without re-encoding into both MP4 files.'}}


def upload(files):
    code = ('from pathlib import Path\nimport json\nw=Path('+repr(WORK)+')\nw.mkdir(exist_ok=True)\n'
            'files=json.loads('+repr(json.dumps(files))+')\n'
            'for n,v in files.items():(w/n).write_text(v)\n')
    ssh('python3 -', code.encode())


def prepare():
    clocks, timeline = plan()
    LOCAL.mkdir(parents=True, exist_ok=True)
    (LOCAL/'timeline.json').write_text(json.dumps(timeline, indent=2)+'\n')
    files = {'timeline.json': json.dumps(timeline, indent=2)}
    metadata = {}
    for variant in ('crt', 'none'):
        for source in SOURCES:
            metadata[f'{variant}-{source}'] = clocks[variant][source].metadata
        args = ['ffmpeg', '-hide_banner', '-y', '-filter_complex_threads', '2']
        for source in SOURCES:
            args += ['-i', f'{REMOTE}/{PROFILES[variant][source]}/proof.mkv']
        filters = []
        for i, source in enumerate(SOURCES):
            filters.append(f'[{i}:v]settb=AVTB,setpts={clocks[variant][source].setpts_expression()}[clock{i}]')
        filters += ['[clock2]split=3[club0][club1][club2]',
                    '[clock3]split=6[amy0][amy1][amy2][amy3][amy4][amy5]']
        inputs = ['clock0', 'clock1', 'club0', 'club1', 'club2', 'amy0', 'amy1', 'amy2', 'amy3', 'amy4', 'amy5']
        for i, (shot, src) in enumerate(zip(timeline['shots'], inputs)):
            filters.append(f'[{src}]trim=start={shot["native_start"]}:end={shot["native_end"]},'
                f'setpts=PTS-STARTPTS,fps=60,tpad=stop_mode=clone:stop_duration=0.1,'
                f'trim=duration={shot["duration"]},settb=1/60,format=yuv420p[v{i}]')
        last = 'v0'
        for i, fade in enumerate(FADES, 1):
            offset = timeline['shots'][i]['output_start']
            filters.append(f'[{last}][v{i}]xfade=transition=fade:duration={fade}:offset={offset}[x{i}]')
            last = f'x{i}'
        # Fractional dissolve boundaries can round the final stream short by
        # one or two samples. Hold the recorded title tail before fading so
        # both exports contain the exact canonical number of video frames.
        filters.append(f'[{last}]tpad=stop_mode=clone:stop_duration=0.2,'
            f'trim=duration={timeline["duration"]},fade=t=out:st={timeline["duration"]-.8}:d=0.8[vout]')
        files[f'video-{variant}.txt'] = ';\n'.join(filters)
        args += ['-filter_complex_script', f'{WORK}/video-{variant}.txt', '-map', '[vout]', '-an',
                 '-c:v', 'libx264', '-threads', '4', '-preset', 'medium', '-crf', '18',
                 '-pix_fmt', 'yuv420p', '-t', str(timeline['duration']), f'{WORK}/video-{variant}.mp4']
        files[f'render-{variant}.json'] = json.dumps(args)
    (LOCAL/'clock-metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
    # The captured duel effects/music are used once, from CRT recordings.
    # Their raw source positions follow the same marker clock as the picture;
    # their waveform itself is not stretched or independently encoded twice.
    files['audio-cuts.json'] = json.dumps({timeline['shots'][i]['name']: {
        'raw_start': clocks['crt'][timeline['shots'][i]['source']].raw_at(timeline['shots'][i]['native_start']),
        'duration': timeline['shots'][i]['duration']} for i in (1, 5, 6, 7)}, indent=2)
    upload(files)
    stems = ADV/'build/trailer-audio'
    for name in ('music_09.wav', 'music_12.wav', 'music_29.wav', 'music_17.wav', 'music_01.wav'):
        with (stems/name).open('rb') as f:
            subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST, 'cat > '+shlex.quote(WORK+'/'+name)], stdin=f, check=True)
    print(json.dumps({'duration': timeline['duration'], 'shots': timeline['shots']}, indent=2))


def run_video(variant):
    worker = ('from pathlib import Path\nimport os,json,subprocess\nos.nice(10)\nw=Path('+repr(WORK)+')\n'
              'args=json.loads((w/'+repr('render-'+variant+'.json')+').read_text())\n'
              'with (w/'+repr('render-'+variant+'.log')+').open("w") as log:subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)\n')
    print(ssh('python3 -', worker.encode()).decode())


def audio():
    timeline = json.loads((LOCAL/'timeline.json').read_text())
    cuts = json.loads(ssh('cat '+shlex.quote(WORK+'/audio-cuts.json')))
    # Measure each selected captured excerpt, then use fixed gain only.
    gains = {'intro': -4.2, 'ronald': 2.5, 'clubs': -3.7, 'amy': 1.0,
             'medal': -5.2, 'hall': 2.6, 'title': -5.5}
    shots = timeline['shots']
    club_duration = shots[4]['output_start']+shots[4]['duration']-shots[2]['output_start']
    hall_start = max(shots[9]['output_start']+.25, shots[8]['output_start']+5.396)
    cases = [
        ('intro', 'music_09.wav', 1.5, shots[0]['duration'], 0, .005, FADES[0]),
        ('ronald', f'../{PROFILES["crt"]["ronald"]}/proof.mkv', cuts[shots[1]['name']]['raw_start'], shots[1]['duration'], shots[1]['output_start'], FADES[0], FADES[1]),
        ('clubs', 'music_12.wav', .016, club_duration, shots[2]['output_start'], FADES[1], FADES[4]),
        ('amy', f'../{PROFILES["crt"]["amy"]}/proof.mkv', cuts[shots[5]['name']]['raw_start'], shots[5]['duration'], shots[5]['output_start'], FADES[4], FADES[5]),
        ('amy', f'../{PROFILES["crt"]["amy"]}/proof.mkv', cuts[shots[6]['name']]['raw_start'], shots[6]['duration'], shots[6]['output_start'], FADES[5], FADES[6]),
        ('amy', f'../{PROFILES["crt"]["amy"]}/proof.mkv', cuts[shots[7]['name']]['raw_start'], shots[7]['duration'], shots[7]['output_start'], FADES[6], FADES[7]),
        ('medal', 'music_29.wav', 0, shots[8]['duration'], shots[8]['output_start'], 0, 0),
        ('hall', 'music_17.wav', 0, shots[9]['output_start']+shots[9]['duration']-hall_start, hall_start, 0, FADES[9]),
        ('title', 'music_01.wav', 24.636719, shots[10]['duration'], shots[10]['output_start'], FADES[9], .12),
    ]
    args = ['ffmpeg', '-hide_banner', '-y']
    filters = []
    for i, (name, path, start, duration, offset, incoming, outgoing) in enumerate(cases):
        args += ['-i', f'{WORK}/{path}']
        chain = (f'[{i}:a]atrim=start={start}:duration={duration},asetpts=PTS-STARTPTS,'
                 f'aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo,'
                 f'volume={gains[name]}dB,apad,atrim=duration={duration}')
        if incoming: chain += f',afade=t=in:d={incoming}'
        if outgoing: chain += f',afade=t=out:st={duration-outgoing}:d={outgoing}'
        filters.append(chain+f',adelay={offset*1000}:all=1[a{i}]')
    filters.append(''.join(f'[a{i}]' for i in range(len(cases)))+
                   f'amix=inputs={len(cases)}:duration=longest:normalize=0,atrim=duration={timeline["duration"]}[out]')
    args += ['-filter_complex_script', f'{WORK}/audio.txt', '-map', '[out]', '-c:a', 'aac', '-b:a', '192k',
             '-ar', '48000', '-t', str(timeline['duration']), f'{WORK}/common-audio.m4a']
    recipe = {'gains_db': gains, 'cases': cases, 'processing': 'Linear gains/fades only; no compressor, limiter or captured-opening music.'}
    (LOCAL/'audio-recipe.json').write_text(json.dumps(recipe, indent=2)+'\n')
    upload({'audio.txt': ';\n'.join(filters), 'audio-render.json': json.dumps(args)})
    worker = 'import json,subprocess\nfrom pathlib import Path\nw=Path('+repr(WORK)+')\nwith (w/"audio-render.log").open("w") as log:subprocess.run(json.loads((w/"audio-render.json").read_text()),stdout=log,stderr=subprocess.STDOUT,check=True)\n'
    print(ssh('python3 -', worker.encode()).decode())


def mux():
    for variant, name in (('crt', 'poketcg64-trailer.mp4'), ('none', 'poketcg64-trailer-no-crt.mp4')):
        args = ['ffmpeg', '-v', 'error', '-y', '-i', f'{WORK}/video-{variant}.mp4', '-i', f'{WORK}/common-audio.m4a',
                '-map', '0:v', '-map', '1:a', '-c', 'copy', '-movflags', '+faststart', '-shortest', f'{WORK}/{name}']
        ssh(shlex.join(args))
        with (LOCAL/name).open('wb') as f:
            subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST, 'cat '+shlex.quote(WORK+'/'+name)], stdout=f, check=True)
        print(name, hashlib.sha256((LOCAL/name).read_bytes()).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'video-crt', 'video-none', 'audio', 'mux'))
    args = parser.parse_args()
    if args.action == 'prepare': prepare()
    elif args.action.startswith('video-'): run_video(args.action[6:])
    elif args.action == 'audio': audio()
    else: mux()
