#!/usr/bin/env python3
"""Remote worker for an isolated Ares window, audio stream and capture."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time

WORK = Path(__file__).resolve().parent
CONFIG = json.loads((WORK/'capture-config.json').read_text())


def json_command(args):
    return json.loads(subprocess.check_output(args, text=True))


def outer():
    sink = CONFIG['sink']
    module = subprocess.check_output(['pactl', 'load-module', 'module-null-sink',
        'sink_name='+sink, 'sink_properties=device.description=PoketcgTrailer',
        'channels=2', 'rate=48000'], text=True).strip()
    try:
        env = os.environ.copy()
        for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'MANGOHUD_CONFIGFILE',
                     'LD_PRELOAD', 'VK_INSTANCE_LAYERS'):
            env.pop(name, None)
        env.update(MANGOHUD='0', PULSE_SINK=sink)
        with (WORK/'gamescope-proof.log').open('w') as log:
            subprocess.run(['gamescope', '--backend', 'headless', '-W', '1440', '-H', '1080',
                '-w', '1440', '-h', '1080', '-r', '60', '--force-windows-fullscreen', '--',
                sys.executable, __file__], stdout=log, stderr=subprocess.STDOUT, env=env,
                timeout=CONFIG['seconds']+CONFIG['warmup']+45, check=True)
    finally:
        if module.isdigit():
            subprocess.run(['pactl', 'unload-module', module], check=True)


def capture():
    sink_name, identity = CONFIG['sink'], CONFIG['identity']
    log = (WORK/'ares-proof.log').open('w')
    fd = os.open(WORK/'flatpak-instance.txt', os.O_WRONLY|os.O_CREAT|os.O_TRUNC, 0o600)
    command = ['flatpak', 'run', '--instance-id-fd='+str(fd), '--filesystem='+str(WORK),
        '--nosocket=wayland', '--socket=x11', '--env=GDK_BACKEND=x11',
        '--env=XDG_DATA_HOME='+str(WORK/'data'), '--env=SDL_AUDIODRIVER=pulseaudio',
        '--env=SDL_AUDIO_DRIVER=pulseaudio', '--env=SDL_APP_NAME='+identity,
        '--env=SDL_AUDIO_DEVICE_APP_NAME='+identity, '--env=SDL_AUDIO_DEVICE_STREAM_NAME='+identity,
        '--env=PULSE_SINK='+sink_name, '--env=MANGOHUD=0', 'dev.ares.ares', '--settings-file',
        str(WORK/'settings.bml'), '--pseudofullscreen', '--kiosk', '--no-file-prompt',
        '--system', 'Nintendo 64']
    if CONFIG['shader'] == 'crt':
        command += ['--shader', 'crt/crt-guest-advanced']
    command += [str(WORK/'capture.z64')]
    if CONFIG['trace_shader']:
        command = ['strace', '-f', '-e', 'trace=openat', '-o', str(WORK/'shader-opens.log')]+command
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                               pass_fds=[fd], start_new_session=True)
    os.close(fd)
    stop = threading.Event()
    routes, markers = [], []
    end_at = None
    fatal_error = None
    recorder = None
    start = time.monotonic()
    metadata = {'configuration': CONFIG, 'launch_epoch': time.time(),
                'launch_monotonic': start, 'display': os.environ['DISPLAY']}
    try:
        sink = next(s for s in json_command(['pactl', '-f', 'json', 'list', 'sinks'])
                    if s['name'] == sink_name)

        def route_audio():
            while not stop.is_set() and process.poll() is None:
                instance = (WORK/'flatpak-instance.txt').read_text().strip()
                inputs = json_command(['pactl', '-f', 'json', 'list', 'sink-inputs'])
                for stream in inputs:
                    props = stream.get('properties', {})
                    if not (props.get('application.name') == identity and
                            props.get('window.x11.display') == os.environ['DISPLAY'] and
                            props.get('pipewire.access.portal.instance_id') == instance):
                        continue
                    if stream['sink'] != sink['index']:
                        result = subprocess.run(['pactl', 'move-sink-input', str(stream['index']),
                                                 sink_name], capture_output=True)
                        if result.returncode == 0:
                            routes.append({'instance': instance, 'display': os.environ['DISPLAY'],
                                'stream_index': stream['index'], 'sink_index': sink['index'],
                                'seconds': time.monotonic()-start, 'properties': props})
                            (WORK/'audio-route.json').write_text(json.dumps(routes, indent=2))
                stop.wait(.05)

        def watch_markers():
            nonlocal end_at, fatal_error
            pending = ''
            with (WORK/'ares-proof.log').open() as reading:
                while not stop.is_set():
                    line = reading.readline()
                    if not line:
                        stop.wait(.005)
                        continue
                    if not line.endswith('\n'):
                        pending += line
                        stop.wait(.005)
                        continue
                    line = pending+line
                    pending = ''
                    if any(message in line for message in ('ASSERTION FAILED', 'EXCEPTION:', 'ADVENTURE ERROR:')):
                        fatal_error = line.strip()
                    if line.startswith('TRAILER '):
                        event = {'line': line.strip(), 'epoch': time.time(),
                                 'monotonic': time.monotonic()}
                        markers.append(event)
                        if line.startswith('TRAILER END ') and end_at is None:
                            end_at = event['monotonic']
                        (WORK/'marker-times.json').write_text(json.dumps(markers, indent=2))

        router = threading.Thread(target=route_audio, daemon=True)
        watcher = threading.Thread(target=watch_markers, daemon=True)
        router.start(); watcher.start()
        # Start from a source marker rather than a variable emulator-launch delay.
        deadline = time.monotonic()+25
        while not any(m['line'].startswith('TRAILER READY ') for m in markers):
            assert process.poll() is None, 'Private Ares exited before READY'
            assert time.monotonic()<deadline, 'Private ROM never reached READY'
            time.sleep(.02)
        time.sleep(CONFIG['warmup'])
        assert routes, 'No uniquely verified private Ares stream routed'
        (WORK/'pulse-inputs.txt').write_text(subprocess.check_output(['pactl', 'list', 'sink-inputs'], text=True))
        tree = subprocess.check_output(['xwininfo', '-root', '-tree'], text=True)
        (WORK/'x-tree.txt').write_text(tree)
        windows = re.findall(r'(0x[0-9a-f]+).*1440x1080', tree)
        assert windows, 'No private Ares window'
        (WORK/'capture-window.txt').write_text(windows[-1])
        record_command = ['ffmpeg', '-hide_banner', '-y', '-thread_queue_size', '512',
            '-f', 'x11grab', '-framerate', '60', '-video_size', '1440x1080', '-draw_mouse', '0',
            '-window_id', windows[-1], '-i', os.environ['DISPLAY']+'+0,0',
            '-thread_queue_size', '512', '-f', 'pulse', '-i', sink_name+'.monitor',
            '-t', str(CONFIG['seconds']), '-c:v', 'libx264rgb', '-threads', '2',
            '-preset', 'ultrafast', '-crf', '0', '-pix_fmt', 'bgr0', '-c:a', 'pcm_s16le',
            str(WORK/'proof.mkv')]
        metadata.update(capture_epoch=time.time(), capture_monotonic=time.monotonic(),
                        recorder_command=record_command, ares_command=command)
        with (WORK/'ffmpeg-proof.log').open('w') as output:
            recorder = subprocess.Popen(record_command, stdin=subprocess.PIPE, stdout=output,
                stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(10))
            while recorder.poll() is None:
                assert process.poll() is None, 'Private Ares exited during capture'
                assert fatal_error is None, fatal_error
                if CONFIG['until_end'] and end_at is not None and time.monotonic()>=end_at+CONFIG['postroll']:
                    recorder.communicate(b'q\n', timeout=10)
                    break
                time.sleep(.02)
            assert recorder.returncode == 0, 'FFmpeg capture failed'
        if CONFIG['until_end']:
            assert end_at is not None, 'Capture limit reached before native END'
        metadata.update(end_marker_seen=end_at is not None, capture_stop_epoch=time.time(),
                        capture_stop_monotonic=time.monotonic())
        (WORK/'capture-metadata.json').write_text(json.dumps(metadata, indent=2))
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', '8', '-i', str(WORK/'proof.mkv'),
            '-frames:v', '1', str(WORK/'proof.png')], check=True)
    finally:
        if recorder is not None and recorder.poll() is None:
            recorder.communicate(b'q\n', timeout=10)
        stop.set()
        if 'router' in locals(): router.join(timeout=2)
        if 'watcher' in locals(): watcher.join(timeout=2)
        instance = (WORK/'flatpak-instance.txt').read_text().strip()
        if instance.isdigit():
            subprocess.run(['flatpak', 'kill', instance], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try: os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError: pass
        try: process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL); process.wait()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--outer', action='store_true')
    args = parser.parse_args()
    if args.outer: outer()
    else: capture()
