#!/usr/bin/env python3
"""Record one private native Ares window on macOS; never record the desktop/audio."""
import argparse,hashlib,json,signal,subprocess,threading,time
from pathlib import Path
ADV=Path(__file__).resolve().parents[1]
SWIFT='''import CoreGraphics
import Foundation
let windows=CGWindowListCopyWindowInfo([.optionAll,.excludeDesktopElements],kCGNullWindowID) as? [[String:Any]] ?? []
let pid=Int(CommandLine.arguments[1])!
let chosen=windows.filter{($0[kCGWindowOwnerPID as String] as? Int)==pid}
print(String(data:try JSONSerialization.data(withJSONObject:chosen,options:.sortedKeys),encoding:.utf8)!)
'''
CURSOR_SWIFT = r'''import CoreGraphics
import Foundation
let original = CGEvent(source:nil)?.location ?? .zero
let display = CGMainDisplayID()
let screen = CGDisplayBounds(display)
let a = CommandLine.arguments
let window = CGRect(x:Double(a[1])!, y:Double(a[2])!, width:Double(a[3])!, height:Double(a[4])!)
let corners = [CGPoint(x:screen.minX+2,y:screen.minY+2),CGPoint(x:screen.maxX-2,y:screen.minY+2),CGPoint(x:screen.minX+2,y:screen.maxY-2),CGPoint(x:screen.maxX-2,y:screen.maxY-2)]
let parked = corners.first(where:{!window.contains($0)})!
let hidden = CGDisplayHideCursor(display)
let warped = CGWarpMouseCursorPosition(parked)
defer {
    _ = CGWarpMouseCursorPosition(original)
    if hidden == .success { _ = CGDisplayShowCursor(display) }
}
let status:[String:Any] = ["hidden":hidden == .success,"warped":warped == .success,"parked_outside_window":!window.contains(parked),"original":[original.x,original.y],"parked":[parked.x,parked.y]]
print(String(data:try JSONSerialization.data(withJSONObject:status,options:.sortedKeys),encoding:.utf8)!)
fflush(stdout)
// EOF also restores the cursor if the owning Python process exits unexpectedly.
while let line = readLine() { if line == "stop" { break } }
'''

def cursor_guard(window):
 helper=ADV/'build/trailer-capture/cursor-guard';src=helper.with_suffix('.swift')
 if not helper.exists() or not src.exists() or src.read_text()!=CURSOR_SWIFT:
  src.write_text(CURSOR_SWIFT);subprocess.run(['swiftc',str(src),'-o',str(helper)],check=True)
 bounds=window['kCGWindowBounds']
 process=subprocess.Popen([str(helper),*[str(bounds[k]) for k in ('X','Y','Width','Height')]],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
 try:
  status=json.loads(process.stdout.readline())
  assert status['warped'] and status['parked_outside_window'],status
 except BaseException:
  process.stdin.close();process.wait(timeout=5);raise
 return process,status

def capture(segment, version=4):
 out=ADV/'build/trailer-capture'/f'v{version}-local-{segment}';out.mkdir(parents=True,exist_ok=True);(out/'saves').mkdir(exist_ok=True)
 helper=ADV/'build/trailer-capture/window-info';src=helper.with_suffix('.swift')
 if not helper.exists():
  src.write_text(SWIFT);subprocess.run(['swiftc',str(src),'-o',str(helper)],check=True)
 rom=ADV/f'build/trailer-cast-{segment}/trailer-{segment}.z64';app=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares'
 settings=f'''Video
  Driver: Metal
  Quality: SD
  Shader: None
  WindowWidth: 720
  WindowHeight: 540
  AspectCorrectionMode: Standard
  DisableVideoInterfaceProcessing: false
Audio
  Driver: SDL
  Blocking: true
  Mute: true
Input
  Driver: SDL
  Defocus: Allow
General
  HomebrewMode: true
  NoFilePrompt: true
  ShowStatusBar: false
Nintendo64
  ExpansionPak: true
Paths
  Saves: {out}/saves/
'''
 (out/'settings.bml').write_text(settings);events=[];end=threading.Event();log=(out/'runtime.log').open('w');start=time.time()
 appproc=subprocess.Popen([str(app),'--settings-file',str(out/'settings.bml'),'--no-file-prompt',str(rom)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
 def reader():
  for line in appproc.stdout:
   log.write(line);log.flush()
   if line.startswith('TRAILER '):
    events.append(dict(line=line.strip(),epoch=time.time()))
    if line.startswith('TRAILER END '+segment):end.set()
 thread=threading.Thread(target=reader,daemon=True);thread.start();rec=None;cursor=None;cursor_status=None;recorder_status=None
 try:
  window=None
  for _ in range(60):
   windows=json.loads(subprocess.check_output([str(helper),str(appproc.pid)]))
   window=next((w for w in windows if w.get('kCGWindowName')=='trailer-'+segment),None)
   if window:break
   time.sleep(.1)
  assert window,'Private capture window unavailable'
  cursor,cursor_status=cursor_guard(window)
  limit={'intro':60,'ronald':45,'clubs':45,'amy':85}[segment]
  recorder_source=ADV/'tools/capture_window.swift';recorder=ADV/'build/trailer-capture/capture-window'
  if not recorder.exists() or recorder.stat().st_mtime_ns<recorder_source.stat().st_mtime_ns:
   subprocess.run(['swiftc','-parse-as-library',str(recorder_source),'-o',str(recorder)],check=True)
  bounds=window['kCGWindowBounds']
  record_start=time.time();rec=subprocess.Popen([str(recorder),str(window['kCGWindowNumber']),str(limit),str(out/'proof.mov'),str(round(bounds['Width']*2)),str(round(bounds['Height']*2))],stdout=subprocess.PIPE,stderr=(out/'recorder.log').open('w'),text=True)
  recorder_status=json.loads(rec.stdout.readline())
  assert recorder_status['shows_cursor'] is False and recorder_status['captures_audio'] is False and recorder_status['capture_microphone'] is False
  record_start=recorder_status['ready_epoch']
  while not end.wait(.1):
   if appproc.poll() is not None:raise RuntimeError('Ares exited before native END')
   if rec.poll() is not None:raise RuntimeError('Recorder exited before native END')
   if time.time()-start>180:raise TimeoutError('Missing native END')
  rec.wait(timeout=limit+15)
  assert (out/'proof.mov').stat().st_size>10000
  subprocess.run(['screencapture','-x','-o','-l'+str(window['kCGWindowNumber']),str(out/'end.png')],check=True)
 finally:
  try:
   if rec and rec.poll() is None:rec.send_signal(signal.SIGINT);rec.wait(timeout=15)
  finally:
   try:
    appproc.terminate();appproc.wait(timeout=10);thread.join(timeout=2);log.close()
   finally:
    if cursor:
     cursor.stdin.close();cursor.wait(timeout=5)
 (out/'markers.json').write_text(json.dumps(events,indent=2)+'\n')
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(out/'proof.mov')]))
 (out/'probe.json').write_text(json.dumps(probe,indent=2)+'\n')
 metadata=dict(recorder=recorder_status,cursor_guard=cursor_status,segment=segment,rom=str(rom.relative_to(ADV)),rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),capture_method='ScreenCaptureKit desktopIndependentWindow of private Ares only; showsCursor=false; post-VI Metal output; no CRT shader; no desktop or microphone/system audio',window=window,app_launch_epoch=start,recorder_launch_epoch=record_start,native_end_seen=end.is_set(),recording_limit_seconds=limit,video_sha256=hashlib.sha256((out/'proof.mov').read_bytes()).hexdigest())
 (out/'capture.json').write_text(json.dumps(metadata,indent=2)+'\n');print(out,flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('segment',choices=('intro','ronald','clubs','amy','all'));p.add_argument('--version',type=int,default=4);a=p.parse_args()
 for s in ('intro','ronald','clubs','amy') if a.segment=='all' else (a.segment,):capture(s,a.version)
