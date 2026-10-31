#!/usr/bin/env python3
"""Build concise review pages for the native presentation fixes."""
from pathlib import Path
import html

ROOT=Path(__file__).resolve().parent
STYLE='''body{max-width:1040px;margin:32px auto;padding:0 20px;background:#111b29;color:#edf1f2;font:17px/1.5 system-ui}a{color:#abd4ff}h1{font-size:30px}h2{font-size:23px;margin-top:32px}figure{margin:24px 0}img{display:block;max-width:100%;height:auto;image-rendering:pixelated}figcaption{margin-top:8px;font-size:15px}p{max-width:85ch}nav{display:flex;gap:16px;flex-wrap:wrap}'''
def page(folder,title,paragraphs,figures):
    body=''.join(f'<p>{p}</p>' for p in paragraphs)
    body+=''.join(f'<figure><a href="{path}"><img src="{path}" alt="{html.escape(caption)}"></a><figcaption>{caption}</figcaption></figure>' for path,caption in figures if (ROOT/folder/path).exists())
    (ROOT/folder/'index.html').write_text(f'<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(title)}</title><style>{STYLE}</style><main><h1>{title}</h1>{body}<nav><a href="README.md">Technical details</a><a href="validation.json">Validation</a><a href="../performance-applied/">Renderer update</a></nav></main></html>')

page('coin-animation','Coin toss',[
    'The original Pikachu coin now flips through an arc, lands with a bounce and reveals the engine’s actual result. Single and multiple tosses keep their original order and sounds.',
    'Animation settings and controller ownership are preserved. A skip finishes the animation without answering the following prompt.',
    'The GIF previews actual completed N64 framebuffers before VI filtering, with palette reduction for the animation. Full-color source frames and browser captures are linked below.'
], [('coin-toss-320.gif','320 × 240 native animation preview'),('native-sequence-480.png','480 × 360 native sequence'),('web/tails-history.png','Browser result and ordered coin history')])

page('intro-orbs','Intro Energy symbols',[
    'The source circles were intact. Tight texture boundaries and opaque filtered edges made the N64 symbols look cut off.',
    'A transparent sampling border and orb-only alpha blending restore the edges. The artwork, animation positions and timing are unchanged.',
    'These comparisons are actual native RDP pixels before VI filtering. Enlargements use nearest-neighbor scaling.'
], [('native-comparison-320.png','320 × 240 before and after'),('native-comparison-480.png','480 × 360 before and after'),('orb-zoom-480.png','Energy edges enlarged 4×')])

practice_figures=[]
for name,caption in [('native/mason-finally-480.png','480 × 360 native short lesson'),('native/mason-recap-480.png','480 × 360 native recap'),('960-recap.png','Browser recap')]:
    practice_figures.append((name,caption))
page('practice-reading','Tutorial lessons',[
    'Mason’s explanations now consistently use one large reading panel. The bottom contains only the continue control and a page position when needed.',
    'Numbered steps remain a separate persistent checklist. Redrawing them no longer flashes a new message or replaces Mason’s explanation. The final lesson includes the completed action recap.',
    'The original lesson order, duel state, RNG and engine acknowledgement counts are unchanged. Each reading page requires a fresh press.'
], practice_figures)

page('audio-padding','Intro audio repair',[
    'The installed stereo audio converter inserted a brief dropout halfway through clips that needed codec padding. The title music had one around 17 seconds.',
    'The asset build now pads the transport data correctly. Original music, effects, PCM hashes and playback durations are preserved. Fifty-four encoded files were corrected.',
    'A complete intro/title run in ares recorded zero N64 output-queue underruns. The repaired title sample was checked through the actual compressed-audio decoder.'
], [])
print('UPDATE REVIEW PAGES PASS')
