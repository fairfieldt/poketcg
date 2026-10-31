#!/usr/bin/env python3
"""Isolated 32x32 face paint options; no production assets are modified."""
from collections import Counter,defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import struct
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
SOURCE=ROOT/'port/characters/assets/textures'
WORLD=ROOT/'port/world3d/generated'
INK=(33,24,24,255)
CREAM=(247,239,214,255)
GREY=(132,123,115,255)
CHEEK=(156,107,66,255)
MOUTH=(90,57,33,255)
OPTIONS=[
 dict(id='baseline',label='Current',description='Current production face paint.'),
 dict(id='moderate',label='Larger classic',description='Larger whites, four-texel pupils, thicker brows and mouth.'),
 dict(id='bold',label='Simple dark',description='Solid dark eye shapes with small glints; clear glasses frames.'),
 dict(id='large',label='Big cartoon',description='Oversized white eyes and broad pupils with small glints.'),
]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def texture_key(image):return 't'+hashlib.sha256(struct.pack('>HH',image.width,image.height)+image.tobytes()).hexdigest()[:16]


def paint(style,option,background):
    image=Image.new('RGBA',(32,32),background);d=ImageDraw.Draw(image)
    def rect(x0,y0,x1,y1,color):d.rectangle((x0,y0,x1-1,y1-1),fill=color)
    def rounded(x0,y0,x1,y1,color,radius=1):d.rounded_rectangle((x0,y0,x1-1,y1-1),radius=radius,fill=color)
    aged=style.startswith('old') or style=='mason'
    glasses='glasses' in style or style=='mason'
    stern=style in ('stern','mature','ronald')
    # The mascot retains its distinctive eyes and open grin rather than being
    # given human sclera or spectacles.
    if style=='imakuni':
        if option=='moderate':bounds=(6,8,12,22);mouth=(10,25,23,29);teeth=(12,25,21,26)
        elif option=='bold':bounds=(8,10,11,23);mouth=(9,25,24,30);teeth=(11,25,22,27)
        else:bounds=(9,11,10,23);mouth=(8,25,25,31);teeth=(10,25,23,27)
        width,height,top,bottom=bounds
        for center in (8,24):
            left=center-width//2;rounded(left,top,left+width,bottom,INK,1)
            if option!='moderate':rect(left+1,top+1,left+3,top+4,CREAM)
        rect(*mouth,INK);rect(*teeth,CREAM)
        return image
    if option=='moderate':
        for left in (4,19):
            rounded(left,10,left+9,23,CREAM,1)
            rect(left+3,11,left+7,22,INK)
        if glasses:
            for left in (2,17):
                # Wide cream margins survive downsampling better than a thick
                # four-sided black box. Mason keeps a stronger upper rim.
                top=9;bottom=25
                rect(left,top,left+13,top+(3 if style=='mason' else 2),INK)
                rect(left,bottom-1,left+13,bottom,INK)
                rect(left,top,left+1,bottom,INK);rect(left+12,top,left+13,bottom,INK)
            rect(14,14,18,16,INK)
            if style=='mason':rect(0,14,3,17,INK);rect(29,14,32,17,INK)
        brow_y=6
    elif option=='bold':
        if glasses:
            for left in (2,17):
                rounded(left,9,left+13,25,INK,1)
                inner_top=12 if style=='mason' else 11
                rect(left+2,inner_top,left+11,24,CREAM)
                rect(left+5,13,left+9,23,INK)
                rect(left+5,13,left+7,16,CREAM)
            rect(14,14,18,17,INK)
            if style=='mason':rect(0,14,3,17,INK);rect(29,14,32,17,INK)
        else:
            for left in (5,20):
                rounded(left,11,left+8,23,INK,2)
                rect(left+1,12,left+3,15,CREAM)
        brow_y=6
    else:
        for left in (2,18):
            rounded(left,9,left+12,24,CREAM,2)
            rect(left+4,11,left+9,23,INK)
            rect(left+4,11,left+6,14,CREAM)
        if glasses:
            for left in (1,17):
                # One-texel side rims retain a large cream lens interior.
                rect(left,8,left+14,10 if style!='mason' else 11,INK)
                rect(left,24,left+14,25,INK)
                rect(left,9,left+1,25,INK);rect(left+13,9,left+14,25,INK)
            rect(14,14,18,16,INK)
            if style=='mason':rect(0,14,2,17,INK);rect(30,14,32,17,INK)
        else:
            for left in (3,19):rect(left,8,left+10,9,INK)
        brow_y=4
    # Expression remains independent of the eye scale and glasses treatment.
    if aged:
        rect(4,brow_y,13,brow_y+2,GREY);rect(19,brow_y,28,brow_y+2,GREY)
        if style.startswith('old'):
            rect(1,26,5,27 if option=='moderate' else 28,CHEEK)
            rect(27,26,31,27 if option=='moderate' else 28,CHEEK)
    elif stern:
        left=[(4,brow_y),(8,brow_y+1),(13,brow_y+3)]
        right=[(19,brow_y+3),(24,brow_y+1),(28,brow_y)]
        if style=='ronald':left[0]=(4,brow_y-1);right[-1]=(28,brow_y+1)
        d.line(left,fill=INK,width=2);d.line(right,fill=INK,width=2)
    else:
        rect(4,brow_y+1,13,brow_y+3,INK);rect(19,brow_y+1,28,brow_y+3,INK)
    if style=='gentle':
        # Flat, relaxed upper lids preserve the gentle expression.
        for left in (4,19) if option=='moderate' else (5,20) if option=='bold' else (3,19):
            rect(left,10 if option!='large' else 9,left+(9 if option=='moderate' else 8 if option=='bold' else 10),11 if option!='large' else 10,INK)
    mouth_color=MOUTH if style in ('mason','ronald') else INK
    rect(12,27,20,29,mouth_color)
    if style=='smile':rect(13,29,19,30,mouth_color)
    return image


def main():
    world=json.loads((WORLD/'world.json').read_text());source_files=sorted(SOURCE.glob('fc_*.png'))
    source_images={p.stem:Image.open(p).convert('RGBA') for p in source_files}
    mapping=[];groups=defaultdict(list)
    for file in source_files:
        image=source_images[file.stem];assert image.size==(32,32)
        background=Counter(image.get_flattened_data()).most_common(1)[0][0]
        assert background==(231,173,99,255)
        native=texture_key(image);groups[native].append(file.stem)
        assert (WORLD/'textures'/(native+'.png')).exists(),native
        assert Image.open(WORLD/'textures'/(native+'.png')).convert('RGBA').tobytes()==image.tobytes()
        models=[dict(model=r['model'],slug=r['slug'],family=r['family']) for r in world['individual_characters'] if native in r['textures']]
        mapping.append(dict(source_face=file.stem,style=file.stem.split('_',2)[2],source_file=str(file.relative_to(ROOT)),source_sha256=digest(file),native_texture=native,native_file='world/textures/'+native+'.sprite',skin_rgb8=list(background[:3]),models=models))
    options=[]
    for definition in OPTIONS:
        option=dict(definition);option['textures']=[];replacements={};seen={}
        folder=HERE/'options'/option['id']/'textures';folder.mkdir(parents=True,exist_ok=True)
        for source in mapping:
            original=SOURCE/(source['source_face']+'.png');output=folder/original.name
            background=Counter(source_images[source['source_face']].get_flattened_data()).most_common(1)[0][0]
            if option['id']=='baseline':shutil.copy2(original,output)
            else:paint(source['style'],option['id'],background).save(output)
            proposed=Image.open(output).convert('RGBA');assert proposed.size==(32,32)
            assert all(alpha==255 for _,_,_,alpha in proposed.get_flattened_data())
            assert proposed.getpixel((0,0))==background
            # Aliases which share an existing native texture must remain the
            # same proposed image, otherwise one replacement would be ambiguous.
            proposed_pixels=proposed.tobytes();native=source['native_texture']
            if native in seen:assert seen[native]==proposed_pixels,(option['id'],source['style'],'alias diverged')
            seen[native]=proposed_pixels
            relative=str(output.relative_to(HERE));replacements.setdefault(native,relative)
            option['textures'].append(dict(**source,file=relative,sha256=digest(output),proposed_content_hash=texture_key(proposed)))
        option['native_replacements']=replacements;assert len(replacements)==9
        options.append(option)
    manifest=dict(purpose='Review-only N64 face paint options; production assets unchanged',size=[32,32],format='Opaque RGBA PNG for RGBA16 conversion',skin_rgb8=[231,173,99],native_texture_groups=dict(groups),options=options)
    (HERE/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # The large sheet preserves individual texels; the tiny sheet is explicitly
    # just a texture sampling guide, not an N64 or VI render.
    sheet=Image.new('RGB',(4*192,38+len(mapping)*158),(24,26,34));d=ImageDraw.Draw(sheet)
    for col,option in enumerate(options):d.text((col*192+8,12),option['label'],fill='white')
    for row,source in enumerate(mapping):
        for col,option in enumerate(options):
            image=Image.open(HERE/'options'/option['id']/'textures'/(source['source_face']+'.png')).convert('RGB')
            x=col*192;y=38+row*158;d.text((x+8,y+2),source['style'],fill='white');sheet.paste(image.resize((128,128),Image.Resampling.NEAREST),(x+32,y+22))
    sheet.save(HERE/'texture-sheet.png')
    guide=Image.new('RGB',(4*256,48+len(mapping)*90),(24,26,34));d=ImageDraw.Draw(guide)
    for col,option in enumerate(options):d.text((col*256+8,10),option['label'],fill='white')
    d.text((8,29),'Texture-only bilinear sampling guide: 8 / 12 / 16 pixels, enlarged by nearest-neighbor. Not a game capture.',fill=(190,195,202))
    for row,source in enumerate(mapping):
        for col,option in enumerate(options):
            image=Image.open(HERE/'options'/option['id']/'textures'/(source['source_face']+'.png')).convert('RGB');x=col*256;y=48+row*90
            d.text((x+8,y+3),source['style'],fill='white')
            for i,size in enumerate((8,12,16)):
                sampled=image.resize((size,size),Image.Resampling.BILINEAR)
                guide.paste(sampled.resize((56,56),Image.Resampling.NEAREST),(x+16+i*78,y+23))
    guide.save(HERE/'sampling-guide.png')
    print(f'FACE OPTIONS: {len(options)} options x {len(mapping)} face styles;9native texture groups per option;production untouched')

if __name__=='__main__':main()
