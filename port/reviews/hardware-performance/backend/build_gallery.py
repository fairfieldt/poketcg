"""Build the fixed-geometry renderer comparison from recorded capture results."""
from pathlib import Path
import html
import json

ROOT=Path(__file__).resolve().parent
data=json.loads((ROOT/'results.json').read_text())
labels={'mark-portrait':'Mark','ronald-portrait':'Ronald','mason-portrait':'Dr. Mason',
        'imakuni-portrait':'Imakuni','leads-room':'Lab: eight actors','leads-conversation':'Conversation: four actors'}
parts=['''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>N64 renderer comparison</title><style>body{max-width:1320px;margin:40px auto;padding:0 20px;background:#f6f5ef;color:#172333;font:17px/1.5 system-ui,sans-serif}h1{font-size:30px}h2{font-size:22px;margin-top:34px}a{color:#164e8c}.pair{display:grid;grid-template-columns:1fr 1fr;gap:18px}figure{margin:0}img{display:block;width:100%;height:auto;background:#111}figcaption{padding:8px 0}.note{font-size:15px;color:#465261}table{border-collapse:collapse;width:100%;max-width:850px}th,td{text-align:left;padding:9px;border-bottom:1px solid #cdcfc8}@media(max-width:760px){.pair{grid-template-columns:1fr}}</style><main>
<h1>N64 renderer comparison</h1><p>The packed backend processes the same high-detail geometry about <strong>3× faster</strong> in the populated lab and conversation fixtures. Source meshes, textures, cameras and poses are unchanged.</p>
<p class="note">480×360, 8 MiB RAM, 24 samples per stage. Timings are ares-emulated CP0 intervals, not measured console frame rates. Both capture suites passed. Small edge and texture differences come from the rendering pipelines.</p>
<p><a href="README.md">Method and adoption checks</a> · <a href="results.json">Measurements</a> · <a href="../">Hardware analysis</a></p>
<table><thead><tr><th>Workload</th><th>Cached GL</th><th>Packed</th><th>Speedup</th></tr></thead><tbody>''']
for stage,row in data['stages'].items():
    parts.append(f'<tr><td>{html.escape(labels[stage])}</td><td>{row["gl"]["median_us"]/1000:.2f} ms</td><td>{row["t3d"]["median_us"]/1000:.2f} ms</td><td>{row["median_speedup"]:.2f}×</td></tr>')
parts.append('</tbody></table>')
for stage in ('leads-room','leads-conversation','mark-portrait','ronald-portrait','mason-portrait','imakuni-portrait'):
    parts.append(f'<h2>{html.escape(labels[stage])}</h2><div class="pair">')
    for backend,label in (('gl','Cached GL'),('t3d','Packed')):
        path=f'cases/{backend}-480-v1/post-vi/{stage}.png'
        assert (ROOT/path).is_file(),path
        parts.append(f'<figure><a href="{path}"><img src="{path}" alt="{html.escape(labels[stage])}, {label}" width="640" height="480"></a><figcaption>{label}</figcaption></figure>')
    parts.append('</div>')
parts.append('</main></html>')
(ROOT/'index.html').write_text('\n'.join(parts)+'\n')
print('Renderer comparison gallery written.')
