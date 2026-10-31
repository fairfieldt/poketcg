#!/usr/bin/env python3
"""Capture CRT/disabled-shader variants through the native END marker."""
from pathlib import Path
import argparse
import json
import shlex
import subprocess
import trailer_capture as base


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'record', 'fetch'))
    parser.add_argument('--name', required=True)
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--shader', choices=('crt', 'none'), default='crt')
    parser.add_argument('--warmup', type=float, default=1)
    parser.add_argument('--seconds', type=int, default=100, help='Safety limit, not a fixed shot duration')
    parser.add_argument('--postroll', type=float, default=5)
    parser.add_argument('--ram-mib', type=int, choices=(4, 8), default=8)
    parser.add_argument('--fixed-seconds', action='store_true')
    parser.add_argument('--trace-shader', action='store_true')
    args = parser.parse_args()
    assert args.name.replace('-', '').isalnum() and len(args.name)<55
    base.REMOTE += '/'+args.name
    if args.action == 'prepare':
        assert args.rom and args.rom.is_file()
        # Reuse v1's private settings/ROM/snapshot transfer; replace its worker
        # with the marker-aware recorder and an explicitly disabled variant.
        base.prepare(args.rom.resolve(), args.warmup, args.seconds, args.trace_shader)
        config = {'shader': args.shader, 'warmup': args.warmup, 'seconds': args.seconds,
                  'postroll': args.postroll, 'until_end': not args.fixed_seconds,
                  'trace_shader': args.trace_shader,
                  'ram_mib': args.ram_mib,
                  'sink': 'poketcg_trailer_'+args.name.replace('-', '_'),
                  'identity': 'PoketcgTrailer_'+args.name}
        files = {'capture-config.json': json.dumps(config, indent=2),
                 'capture_worker.py': Path(__file__).with_name('trailer_capture_runner.py').read_text()}
        script = ('from pathlib import Path\nimport json\nw=Path('+repr(base.REMOTE)+')\n'
                  'files=json.loads('+repr(json.dumps(files))+')\n'
                  'for n,v in files.items():(w/n).write_text(v)\n')
        if args.shader == 'none':
            script += ('p=w/"settings.bml"\np.write_text(p.read_text().replace('
                       '"Shader: crt/crt-guest-advanced.slangp","Shader: None"))\n')
        if args.ram_mib == 4:
            script += 'p=w/"settings.bml"\np.write_text(p.read_text().replace("ExpansionPak: true","ExpansionPak: false"))\n'
        base.ssh('python3 -', script.encode())
        print(json.dumps(config, indent=2))
    elif args.action == 'record':
        print(base.ssh('python3 '+shlex.quote(base.REMOTE+'/capture_worker.py')+' --outer').decode())
    else:
        out = base.ADV/'build/trailer-capture'/args.name
        out.mkdir(parents=True, exist_ok=True)
        optional = {'shader-opens.log'}
        for name in ('proof.png', 'x-tree.txt', 'pulse-inputs.txt', 'audio-route.json',
                     'shader-opens.log', 'ares-proof.log', 'gamescope-proof.log', 'ffmpeg-proof.log',
                     'capture-metadata.json', 'marker-times.json', 'capture-config.json'):
            try: (out/name).write_bytes(base.ssh('cat '+shlex.quote(base.REMOTE+'/'+name)))
            except subprocess.CalledProcessError:
                if name not in optional: raise
        # Earlier workers could observe a partially written UART line. Keep
        # its observation timestamp and recover the exact line in log order.
        full = [s for s in (out/'ares-proof.log').read_text().splitlines() if s.startswith('TRAILER ')]
        events = json.loads((out/'marker-times.json').read_text())
        cursor = 0
        for event in events:
            for index in range(cursor, len(full)):
                if full[index].startswith(event['line']):
                    event['partial_line_repaired'] = full[index] != event['line']
                    event['line'] = full[index]
                    cursor = index+1
                    break
        (out/'marker-times-complete.json').write_text(json.dumps(events, indent=2)+'\n')
        print(out)


if __name__ == '__main__':
    main()
