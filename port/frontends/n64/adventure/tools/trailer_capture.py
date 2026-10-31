#!/usr/bin/env python3
"""Private Steam Machine shader-output capture; never launches the live wrapper."""
from pathlib import Path
import argparse
import hashlib
import json
import shlex
import subprocess

ADV = Path(__file__).resolve().parents[1]
HOST = 'deck@steamdeck.tdf.sh'
REMOTE = '/home/deck/.cache/poketcg-trailer-20261031'


def ssh(command, data=None):
    return subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST, command], input=data,
                          check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout


def prepare(rom, warmup=20, seconds=6, trace_shader=False):
    config = '''Video
  Driver: OpenGL 3.2
  Format: ARGB24
  Exclusive: false
  Blocking: false
  PresentSRGB: false
  ThreadedRenderer: true
  Shader: crt/crt-guest-advanced.slangp
  WindowWidth: 1440
  WindowHeight: 1080
  Output: Scale
  FixedScale: 2
  AspectCorrectionMode: Standard
  AdaptiveSizing: false
  AutoCentering: false
  Luminance: 1.0
  Saturation: 1.0
  Gamma: 1.0
  ColorBleed: false
  ColorEmulation: true
  DeepBlackBoost: false
  InterframeBlending: true
  Overscan: true
  PixelAccuracy: false
  Quality: SD
  Supersampling: false
  DisableVideoInterfaceProcessing: false
  WeaveDeinterlacing: true
Audio
  Driver: SDL
  Device: Default
  Frequency: 48000
  Latency: 20
  Blocking: true
  Dynamic: false
  Mute: false
  Volume: 1.0
  Balance: 0.0
Input
  Driver: SDL
  Defocus: Allow
General
  ShowStatusBar: false
  Rewind: false
  RunAhead: false
  AutoSaveMemory: false
  HomebrewMode: true
  NoFilePrompt: true
DebugServer
  Enabled: false
Nintendo64
  ExpansionPak: true
Paths
  Saves: REMOTE/saves/
  Screenshots: REMOTE/screenshots/
'''.replace('REMOTE', REMOTE)
    runner = r'''from pathlib import Path
import json,os,re,signal,subprocess,threading,time
work=Path(__file__).resolve().parent
log=(work/'ares-proof.log').open('w')
fd=os.open(work/'flatpak-instance.txt',os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
command=['flatpak','run','--instance-id-fd='+str(fd),'--filesystem='+str(work),
 '--nosocket=wayland','--socket=x11','--env=GDK_BACKEND=x11',
 '--env=XDG_DATA_HOME='+str(work/'data'),
 '--env=SDL_AUDIODRIVER=pulseaudio','--env=SDL_AUDIO_DRIVER=pulseaudio',
 '--env=SDL_APP_NAME=PoketcgTrailer','--env=SDL_AUDIO_DEVICE_APP_NAME=PoketcgTrailer',
 '--env=SDL_AUDIO_DEVICE_STREAM_NAME=PoketcgTrailer','--env=PULSE_SINK=poketcg_trailer',
 '--env=MANGOHUD=0','dev.ares.ares','--settings-file',str(work/'settings.bml'),
 '--pseudofullscreen','--kiosk','--no-file-prompt','--system','Nintendo 64',
 '--shader','crt/crt-guest-advanced',str(work/'capture.z64')]
process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,pass_fds=[fd],start_new_session=True)
os.close(fd)
try:
 # SDL recreates its stream during ROM/audio initialization; follow only ours.
 started=time.monotonic();routes=[];route_stop=threading.Event()
 sinks=json.loads(subprocess.run(['pactl','-f','json','list','sinks'],capture_output=True,text=True,check=True).stdout)
 sink=next(v for v in sinks if v['name']=='poketcg_trailer')
 def route_audio():
  while not route_stop.is_set() and process.poll() is None:
   instance=(work/'flatpak-instance.txt').read_text().strip()
   values=json.loads(subprocess.run(['pactl','-f','json','list','sink-inputs'],capture_output=True,text=True,check=True).stdout)
   matches=[v for v in values if v.get('properties',{}).get('application.name')=='PoketcgTrailer' and
    v['properties'].get('window.x11.display')==os.environ['DISPLAY'] and
    v['properties'].get('pipewire.access.portal.instance_id')==instance]
   for stream in matches:
    if stream['sink']!=sink['index']:
     result=subprocess.run(['pactl','move-sink-input',str(stream['index']),'poketcg_trailer'],capture_output=True)
     if result.returncode==0:
      routes.append({'instance':instance,'display':os.environ['DISPLAY'],'stream_index':stream['index'],
       'private_sink_index':sink['index'],'seconds':time.monotonic()-started,'properties':stream['properties']})
      (work/'audio-route.json').write_text(json.dumps(routes,indent=2))
   route_stop.wait(.05)
 router=threading.Thread(target=route_audio,daemon=True);router.start()
 time.sleep(20)
 assert routes,'No uniquely verified private Ares audio stream routed'
 (work/'pulse-inputs.txt').write_text(subprocess.run(['pactl','list','sink-inputs'],capture_output=True,text=True).stdout)
 (work/'x-tree.txt').write_text(subprocess.run(['xwininfo','-root','-tree'],capture_output=True,text=True).stdout)
 env=os.environ.copy();env['PULSE_SINK']='poketcg_trailer'
 windows=re.findall(r'(0x[0-9a-f]+).*1440x1080', (work/'x-tree.txt').read_text())
 assert windows, 'No private Ares window'
 window=windows[-1];(work/'capture-window.txt').write_text(window)
 subprocess.run(['ffmpeg','-hide_banner','-y','-f','x11grab','-framerate','60',
  '-video_size','1440x1080','-draw_mouse','0','-window_id',window,'-i',os.environ['DISPLAY']+'+0,0',
  '-f','pulse','-i','poketcg_trailer.monitor','-t','6','-c:v','libx264rgb',
  '-preset','ultrafast','-crf','0','-pix_fmt','bgr0','-c:a','pcm_s16le',str(work/'proof.mkv')],
  stdout=(work/'ffmpeg-proof.log').open('w'),stderr=subprocess.STDOUT,check=True,env=env)
 subprocess.run(['ffmpeg','-hide_banner','-y','-ss','2','-i',str(work/'proof.mkv'),'-frames:v','1',str(work/'proof.png')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
finally:
 if 'route_stop' in globals():route_stop.set();router.join(timeout=2)
 instance=(work/'flatpak-instance.txt').read_text().strip()
 if instance.isdigit():subprocess.run(['flatpak','kill',instance],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 try:os.killpg(process.pid,signal.SIGTERM)
 except ProcessLookupError:pass
 try:process.wait(timeout=5)
 except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
'''
    outer = r'''from pathlib import Path
import os,subprocess,sys
work=Path('REMOTE')
module=subprocess.run(['pactl','load-module','module-null-sink','sink_name=poketcg_trailer',
 'sink_properties=device.description=PoketcgTrailer','channels=2','rate=48000'],capture_output=True,text=True,check=True).stdout.strip()
try:
 env=os.environ.copy()
 for key in ['DISPLAY','WAYLAND_DISPLAY','MANGOHUD_CONFIGFILE','LD_PRELOAD','VK_INSTANCE_LAYERS']:env.pop(key,None)
 env['MANGOHUD']='0';env['PULSE_SINK']='poketcg_trailer'
 with (work/'gamescope-proof.log').open('w') as log:
  subprocess.run(['gamescope','--backend','headless','-W','1440','-H','1080',
   '-w','1440','-h','1080','-r','60','--force-windows-fullscreen','--',
   sys.executable,str(work/'proof_runner.py')],stdout=log,stderr=subprocess.STDOUT,env=env,timeout=40,check=True)
finally:
 if module.isdigit():subprocess.run(['pactl','unload-module',module],check=True)
'''.replace('REMOTE', REMOTE)
    runner = runner.replace('time.sleep(20)', f'time.sleep({warmup})')
    runner = runner.replace("'-t','6'", f"'-t','{seconds}'")
    if trace_shader:
        runner = runner.replace('process=subprocess.Popen(command,stdout=log',
            "command=['strace','-f','-e','trace=openat','-o',str(work/'shader-opens.log')]+command\nprocess=subprocess.Popen(command,stdout=log")
    outer = outer.replace('timeout=40', f'timeout={warmup+seconds+25}')
    payload = {name: value for name, value in [('settings.bml', config),
        ('proof_runner.py', runner), ('proof_outer.py', outer)]}
    remote = ('from pathlib import Path\nimport json\nw=Path(' + repr(REMOTE) + ')\n'
              'w.mkdir(parents=True,exist_ok=True)\n'
              'source=Path.home()/".var/app/dev.ares.ares/data/ares/Shaders/crt/crt-guest-advanced.slangp"\n'
              'dest=w/"data/ares/Shaders/crt/crt-guest-advanced.slangp"\n'
              'dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(source.read_bytes())\n'
              '(w/"saves").mkdir(exist_ok=True)\n(w/"screenshots").mkdir(exist_ok=True)\n'
              'files=json.loads(' + repr(json.dumps(payload)) + ')\n'
              'for name,value in files.items():(w/name).write_text(value)\n')
    ssh('python3 -', remote.encode())
    with rom.open('rb') as stream:
        subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST,
                        'cat > ' + shlex.quote(REMOTE + '/capture.z64')], stdin=stream, check=True)
    print(json.dumps({'remote_private_directory': REMOTE, 'rom': str(rom),
        'rom_sha256': hashlib.sha256(rom.read_bytes()).hexdigest()}))


def main():
    global REMOTE
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'proof', 'fetch-proof'))
    parser.add_argument('--rom',type=Path,default=ADV/'poketcg-expansion.z64')
    parser.add_argument('--name',default='shader-proof')
    parser.add_argument('--warmup',type=int,default=20)
    parser.add_argument('--seconds',type=int,default=6)
    parser.add_argument('--trace-shader',action='store_true')
    args=parser.parse_args()
    assert args.name.replace('-','').isalnum()
    REMOTE += '/' + args.name
    if args.action=='prepare':prepare(args.rom.resolve(),args.warmup,args.seconds,args.trace_shader)
    elif args.action=='proof':print(ssh('python3 '+shlex.quote(REMOTE+'/proof_outer.py')).decode())
    else:
        out=ADV/'build/trailer-capture'/args.name;out.mkdir(parents=True,exist_ok=True)
        for name in ['proof.png','x-tree.txt','pulse-inputs.txt','audio-route.json','shader-opens.log','ares-proof.log','gamescope-proof.log','ffmpeg-proof.log']:
            try:(out/name).write_bytes(ssh('cat '+shlex.quote(REMOTE+'/'+name)))
            except subprocess.CalledProcessError:
                if name!='shader-opens.log':raise
        print(out)


if __name__=='__main__':main()
