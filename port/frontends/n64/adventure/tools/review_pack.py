#!/usr/bin/env python3
"""Build a standalone labelled N64 screenshot gallery from native captures.

Examples (run from any working directory; paths below are repository relative):
  python3 port/frontends/n64/adventure/tools/review_pack.py \
    --output port/reviews/n64-current \
    --native port/frontends/n64/adventure/build/validation \
    --log port/frontends/n64/adventure/build/review-captures/ares.log \
    --rom port/frontends/n64/adventure/poketcg-review.z64

--log is repeatable and accepts both DIAGNOSTIC=1 map/intro/cinematic dumps and
DIAGNOSTIC=3 real duel request dumps. Explicit logs override matching PNGs from
--native. New galleries never silently import a different capture session's log.
All native pixels are before VI filtering. Labels stay outside image pixels.
Pillow is optional and adds contact-sheet.png; the HTML gallery always works.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

REPO=Path(__file__).resolve().parents[5]
BEFORE=REPO/'port/reviews/n64-polish/before'
DEFAULT_NATIVE=REPO/'port/world3d/generated/native'
sys.path.insert(0,str(REPO/'port/frontends/web/tools'))
from png import read_png,write_png

LABELS={
    'title':'Title screen',
    'intro-charizard':'Opening: Charizard',
    'intro-scyther':'Opening: Scyther',
    'intro-aerodactyl':'Opening: Aerodactyl',
    'naming':'Choose your name',
    'map-00':'Overworld',
    'map-01':'Mason laboratory',
    'mason-dialogue':'Mason dialogue',
    'sam-table':'Approach Sam',
    'map-15':'Lightning Club',
    'map-12':'Water Club',
    'map-18':'Grass Club',
    'map-21':'Psychic Club',
    'map-27':'Fire Club',
    'map-09':'Rock Club',
    'map-24':'Science Club',
    'map-06':'Fighting Club',
    'map-14':'Shared lobby: Lightning',
    'map-32':'Pokemon Dome',
    'map-33':'Hall of Honor',
    'duel':'Duel: native match final board',
    'duel-card-list':'Duel: choose a card from the hand',
    'duel-card-detail':'Duel: card detail',
    'duel-coin':'Duel: coin toss',
    'duel-action-menu':'Duel: action menu',
    'duel-player-turn':"Duel: player's turn",
    'duel-opponent-turn':"Duel: opponent's turn",
}
RAW_CAPTURE='Native N64 RDP RGBA16 framebuffer after completion fence, before VI filtering'


def repo_path(path):
    """Resolve CLI relative paths against the repository, independent of cwd."""
    return (path if path.is_absolute() else REPO/path).resolve()


def relative(path):
    try:return str(path.relative_to(REPO))
    except ValueError:return str(path)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_native_metadata(native):
    for name in ('result.json','validation.json','manifest.json'):
        path=native/name
        if path.exists():
            data=json.loads(path.read_text())
            return path,data
    return None,{}


def decode_log(path):
    """Yield complete framebuffer records, validating every source pixel."""
    text=path.read_text(errors='replace')
    assert not any(mark in text for mark in ('ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')),f'Failed diagnostic log: {path}'
    for m in re.finditer(r'^FRAMEBUFFER ([\w-]+) (\d+) (\d+)\n(.*?)^FRAMEBUFFER END\s*$',text,re.M|re.S):
        name,w,h,body=m.groups();w,h=int(w),int(h)
        assert (w,h)==(320,240),(name,w,h)
        rows=[bytearray(w*4) for _ in range(h)];seen=set()
        for line in body.splitlines():
            if not line.startswith('PIXELS '):continue
            _,xs,ys,data=line.split();x,y=int(xs),int(ys)
            assert len(data)%4==0 and 0<=y<h,(name,line[:80])
            for i in range(0,len(data),4):
                px=x+i//4
                assert 0<=px<w and (px,y) not in seen,(name,px,y)
                seen.add((px,y));v=int(data[i:i+4],16);at=px*4
                rows[y][at:at+4]=bytes([((v>>11)&31)*255//31,((v>>6)&31)*255//31,((v>>1)&31)*255//31,255])
        assert len(seen)==w*h,(name,len(seen))
        yield name,w,h,rows


def image_item(file,label,source,**metadata):
    width,height,_,_=read_png(file)
    assert (width,height)==(320,240),(file,width,height)
    return dict(file=file.name,label=label,source=relative(source),
                sha256=sha256(file),capture=RAW_CAPTURE,
                dimensions=[width,height],**metadata)


def gallery(root,items,title):
    escaped=html.escape(title)
    figures=''.join('<figure><a href="'+html.escape(i['file'])+'"><img src="'+html.escape(i['file'])+'"></a><figcaption>'+html.escape(i['label'])+'</figcaption></figure>' for i in items)
    (root/'index.html').write_text('''<!doctype html><meta charset="utf-8"><title>'''+escaped+'''</title>
<style>body{background:#171923;color:#eee;font:16px system-ui;margin:24px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(640px,1fr));gap:24px}figure{margin:0}img{width:640px;height:480px;image-rendering:pixelated;max-width:100%;object-fit:contain}figcaption{padding:8px 0}p{max-width:900px;line-height:1.5}</style>
<h1>'''+escaped+'''</h1><p>Screenshots from the N64 renderer at 320 × 240, shown at twice their original size. Labels sit outside the game images. Captures precede VI antialiasing and dedithering; final output is softer.</p><main>'''+figures+'</main>')
    try:
        from PIL import Image,ImageDraw
    except ImportError:
        return False
    width=1280;tile_h=266
    sheet=Image.new('RGB',(width,((len(items)+3)//4)*tile_h),(20,22,30));draw=ImageDraw.Draw(sheet)
    for n,item in enumerate(items):
        x=(n%4)*320;y=(n//4)*tile_h
        sheet.paste(Image.open(root/item['file']).convert('RGB'),(x,y))
        draw.text((x+5,y+244),item['label'],fill=(238,238,245))
    sheet.save(root/'contact-sheet.png')
    return True


def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output',type=Path,default=BEFORE,help='Gallery directory (default: original before path; existing baseline is protected)')
    p.add_argument('--native',type=Path,default=DEFAULT_NATIVE,help='Fresh DIAG1 PNGs and result.json/validation.json (default: generated/native)')
    p.add_argument('--log',type=Path,action='append',help='DIAG1 or DIAG3 framebuffer log; repeat to combine captures')
    p.add_argument('--rom',type=Path,action='append',help='Exact capture ROM for each --log, repeated in matching order; omit when unknown')
    p.add_argument('--title',default='N64 game presentation',help='Standalone gallery heading (default: N64 game presentation)')
    args=p.parse_args();root=repo_path(args.output);native=repo_path(args.native)
    if root.is_relative_to(BEFORE) and BEFORE.exists() and any(BEFORE.iterdir()):
        p.error('The existing baseline gallery is immutable. Choose a new --output directory.')
    if root==native:
        p.error('--output must differ from --native so source captures remain untouched.')
    logs=[repo_path(path) for path in (args.log or [])]
    if not logs and root==BEFORE:
        old_log=BEFORE/'diagnostic/ares.log'
        if old_log.exists():logs=[old_log]
    for path in logs:
        if not path.is_file():p.error(f'Capture log does not exist: {path}')
    roms=[repo_path(path) for path in (args.rom or [])]
    if roms and len(roms)!=len(logs):p.error('Supply one --rom for each --log, in matching order, or omit --rom.')
    for rom in roms:
        if not rom.is_file():p.error(f'Capture ROM does not exist: {rom}')
    root.mkdir(parents=True,exist_ok=True)
    metadata_path,metadata=load_native_metadata(native)
    source_items={i['file']:i for i in metadata.get('images',[])}
    images={};validation_hash=metadata.get('rom_sha256',metadata.get('baseline_validation_rom_sha256'))
    for name,label in LABELS.items():
        source=native/(name+'.png')
        if not source.is_file():continue
        file=root/source.name;shutil.copy2(source,file)
        provenance={}
        if metadata_path:provenance['source_manifest']=relative(metadata_path)
        original=source_items.get(source.name)
        if original:
            assert original['sha256']==sha256(source),f'Source manifest hash mismatch: {source}'
            provenance['source_capture']=original
        elif validation_hash:provenance['rom_sha256']=validation_hash
        images[name]=image_item(file,label,source,**provenance)
    log_records=[]
    for log_index,log in enumerate(logs):
        rom=roms[log_index] if roms else None
        captured=[]
        for name,width,height,rows in decode_log(log):
            if name not in LABELS:continue
            file=root/(name+'.png');write_png(file,width,height,rows)
            provenance={'diagnostic_mode':3 if name.startswith('duel-') else 1}
            if rom:provenance['rom_sha256']=sha256(rom)
            images[name]=image_item(file,LABELS[name],log,**provenance);captured.append(name)
        record=dict(path=relative(log),sha256=sha256(log),screens=captured)
        if rom:record['rom_sha256']=sha256(rom)
        log_records.append(record)
    if not images:p.error('No selected screenshots found in --native or --log.')
    items=[images[name] for name in LABELS if name in images]
    try:commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip()
    except (OSError,subprocess.CalledProcessError):commit=None
    manifest=dict(purpose='N64 game presentation screenshots for independent review',
                  capture_notes='Original native 320x240 pixels before final VI antialiasing/dedithering. Labels are outside screenshots. No final-VI scanout is claimed.',
                  source_commit=commit,native_source=relative(native),
                  native_validation_source=relative(metadata_path) if metadata_path else None,
                  native_validation_rom_sha256=validation_hash,
                  logs=log_records,images=items)
    if len(roms)==1:manifest['log_rom_sha256']=sha256(roms[0])
    manifest['contact_sheet']=gallery(root,items,args.title)
    (root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    missing=[name for name in LABELS if name not in images]
    print(f'{len(items)} labelled native screenshots: {root}')
    if missing:print('Not supplied: '+', '.join(missing))


if __name__=='__main__':main()
