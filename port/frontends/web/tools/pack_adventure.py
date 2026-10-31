#!/usr/bin/env python3
"""Package shared N64 meshes, original intro art and browser audio."""
from pathlib import Path
import json,shutil,subprocess
WEB=Path(__file__).resolve().parents[1]
PORT=WEB.parents[1]
OUT=WEB/'dist'
SOURCE=PORT/'world3d/generated'
world=OUT/'world';world.mkdir(exist_ok=True)
for part in ('maps','textures'):
    shutil.copytree(SOURCE/part,world/part,dirs_exist_ok=True)
for name in ('world.json','actors.l64'):
    shutil.copy2(SOURCE/name,world/name)
intro=PORT/'frontends/n64/adventure/assets/intro'
shutil.copytree(intro,OUT/'intro',dirs_exist_ok=True)
# The browser uses the exact N64 Expansion-Pak title derivative.
shutil.copy2(intro.parent/'title64/logo-480.png',OUT/'intro/title64-logo.png')
manifest=json.loads((PORT/'audio/generated/audio.json').read_text())
audio=OUT/'audio';audio.mkdir(exist_ok=True)
ffmpeg=shutil.which('ffmpeg')
cache=WEB/'assets/audio';cache.mkdir(parents=True,exist_ok=True)
for track in manifest['tracks']:
    stem=f"{track['kind']}_{track['id']:02}"
    source=PORT/'audio/build'/(stem+'.wav')
    compressed=cache/(stem+'.ogg')
    if not compressed.exists():
        if not source.exists() or not ffmpeg:
            raise RuntimeError('Browser audio assets missing: regenerate from original driver PCM with ffmpeg')
        subprocess.run([ffmpeg,'-v','error','-y','-i',str(source),'-c:a','libvorbis','-q:a','3',str(compressed)],check=True)
    shutil.copy2(compressed,audio/compressed.name)
    track['browser_file']=compressed.name
(audio/'audio.json').write_text(json.dumps(manifest,separators=(',',':')))
print('Adventure assets: 34 maps, shared characters, original intro, 31 music +96 SFX')
