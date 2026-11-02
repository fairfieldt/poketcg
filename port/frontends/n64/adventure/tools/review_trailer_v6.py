#!/usr/bin/env python3
"""Extract shot and walking-sequence frames from the finished v6 movie."""
from pathlib import Path
import json
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[5]
OUT = ROOT/'port/reviews/trailer/v6'
WORK = ROOT/'port/frontends/n64/adventure/build/trailer-v6-review'


def frame(time, name, width=480):
    path = WORK/(name+'.png')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', str(time), '-i',
                    str(OUT/'poketcg64-trailer.mp4'), '-vf', f'scale={width}:{width*3//4}',
                    '-frames:v', '1', str(path)], check=True)
    return Image.open(path).convert('RGB')


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    timeline = json.loads((OUT/'timeline.json').read_text())
    sheet = Image.new('RGB', (1440, 4*388), '#111319')
    draw = ImageDraw.Draw(sheet)
    labels = ['Opening', 'First table', "Ronald's challenge", 'Ronald battle',
              'Lightning Club', 'Fire Club', 'Water Club', 'Amy battle',
              'Water Medal', 'Legendary card', 'Title']
    samples = []
    for i, shot in enumerate(timeline['shots']):
        time = shot['output_start']+shot['duration']/2
        if shot['name'] == 'hall-legendary-reveal':
            time = shot['output_start']+timeline['legendary_glow_native_start']-shot['native_start']+1
        x, y = (i % 3)*480, (i//3)*388
        sheet.paste(frame(time, shot['name']), (x, y+28))
        draw.text((x+8, y+8), f'{labels[i]}  {time:.2f}s', fill='white')
        samples.append(dict(shot=shot['name'], output_seconds=time))
    sheet.save(OUT/'contact-sheet.jpg', quality=92)
    for name in ('lightning-moving', 'fire-moving-deep', 'water-moving'):
        shot = next(s for s in timeline['shots'] if s['name'] == name)
        strip = Image.new('RGB', (1920, 268), '#111319')
        draw = ImageDraw.Draw(strip)
        for i in range(6):
            time = shot['output_start']+.4+i*.2
            strip.paste(frame(time, name+f'-{i}', 320), (i*320, 28))
            draw.text((i*320+8, 8), f'{time:.2f}s', fill='white')
        strip.save(OUT/(name+'-sequence.jpg'), quality=92)
    (OUT/'review-samples.json').write_text(json.dumps(samples, indent=2)+'\n')


if __name__ == '__main__':
    main()
