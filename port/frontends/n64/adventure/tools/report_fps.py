#!/usr/bin/env python3
"""Build a sortable, standalone report from native diagnostic20 measurements."""
import argparse
import json
from pathlib import Path
import re
import shutil

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results',type=Path,nargs='+')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--duel',type=Path,help='Separately measured optimized duel/UI cases')
    parser.add_argument('--duel-before',type=Path,help='Duel/UI baseline before optimization')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    runs=[]
    for source in args.results:
        run=json.loads(source.read_text());assert run['passed'],source
        target=out/f"{run['native_dimensions'][0]}x{run['native_dimensions'][1]}-{run['ram_mib']}mib"
        target.mkdir(exist_ok=True)
        shutil.copyfile(source,target/'result.json')
        if (source.parent/'ares.log').exists():shutil.copyfile(source.parent/'ares.log',target/'ares.log')
        runs.append(run)
    maps=Path(__file__).resolve().parents[4]/'world3d/generated/world3d_maps.h'
    map_names=re.findall(r'/\* ([A-Z_]+) \*/',maps.read_text())[:34]
    combined={'runs':runs,'map_names':map_names}
    if args.duel:
        combined['duel']=json.loads(args.duel.read_text())
        assert combined['duel']['passed']
        shutil.copyfile(args.duel,out/'duel-final.json')
    if args.duel_before:
        combined['duel_before']=json.loads(args.duel_before.read_text())
        shutil.copyfile(args.duel_before,out/'duel-before.json')
    (out/'results.json').write_text(json.dumps(combined,indent=2)+'\n')
    payload=json.dumps(combined).replace('<',chr(92)+'u003c')
    html='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>N64 frame performance survey</title>
<style>body{font:16px/1.5 system-ui,sans-serif;max-width:1500px;margin:36px auto;padding:0 22px;color:#202a31;background:#f8f7f1}h1{font-size:30px;margin-bottom:10px}p{max-width:1050px}a{color:#17618c}button,select,input{font:inherit;padding:7px;border:1px solid #bac2c4;background:white;border-radius:4px;margin-right:10px}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums;margin-top:20px}th,td{padding:9px 11px;text-align:right;border-bottom:1px solid #dce0df}th:first-child,td:first-child{text-align:left}th{cursor:pointer;background:#e7ecea;position:sticky;top:0}tr:hover{background:#e9efee}.bad{color:#a32b26;font-weight:650}.ok{color:#20633e}.muted{color:#607078;font-size:14px}.scroll{overflow:auto}.bar{height:8px;width:120px;display:flex;background:#d8dddd}.bar span{display:block;height:8px}.legend span{display:inline-block;padding:1px 7px;margin:0 10px 0 0;color:#fff}code{background:#e5e8e5;padding:1px 4px}#summary{font-weight:600}footer{margin:26px 0}</style>
<h1>N64 frame performance survey</h1>
<p id="summary"></p><p>These are completed game frames measured with the N64 CP0 clock inside ares. They include emulated RSP work and host-backed RDP waits. They are useful for comparing these views; they are not physical-console FPS measurements.</p>
<p>Each case holds assets in memory, warms 32 frames, then samples 96 production draw, present and audio calls. Cadence includes the 30 FPS display limit. Work subtracts display-buffer acquisition and pacing wait. Draw combines CPU, RSP and RDP time; it is not a CPU-only measurement. Input polling and story VM work outside the rendered callback are excluded.</p>
<p>Map fixtures use the complete authored NPC population at the entrance, with extra center views for six interiors. Mason and Sam use states captured from the original opening script. Ronald uses a controlled lobby fixture. Dialogue and naming reuse their normal persistent canvases. Intro and title reuse loaded production assets.</p>
<p id="evidence"></p><p><label>Resolution <select id="resolution"></select></label><label>View <select id="category"><option value="all">All</option><option value="map">Maps</option><option value="source">Mason and Sam</option><option value="ronald">Ronald</option><option value="intro">Intro and title</option><option value="naming">Naming</option></select></label><input id="filter" placeholder="Filter cases" aria-label="Filter cases"></p>
<p class="legend"><span style="background:#427b9a">Draw</span><span style="background:#a47b35">Present</span><span style="background:#7970a4">Audio</span><span style="background:#aebcba;color:#27302f">Display wait</span>Bars show mean time within a 66.7 ms span.</p>
<div class="scroll"><table><thead><tr><th data-key="case">Case</th><th data-key="fps">Mean FPS</th><th data-key="frame">Mean frame ms</th><th data-key="p95">p95 frame ms</th><th data-key="work">Mean work ms</th><th data-key="work95">p95 work ms</th><th data-key="draw">Draw ms</th><th data-key="audio">Audio ms</th><th data-key="wait">Wait ms</th><th>Mean stages</th></tr></thead><tbody id="rows"></tbody></table></div>
<footer class="muted">The baseline ROM hash, per-case sample counts, full stage distributions, NPC counts, positions and free heap are in the JSON and native logs. No framebuffer capture or per-frame diagnostic output runs inside the sampled windows.</footer>
<script type="application/json" id="data">PAYLOAD</script><script>
const data=JSON.parse(document.querySelector('#data').textContent),$=x=>document.querySelector(x),runs=data.runs;
let sort='frame',descending=true;
function title(c){const match=c.match(/^map_(\\d+)_(.*)$/);return match?(data.map_names[Number(match[1])]||c).toLowerCase().replaceAll('_',' ')+' / '+match[2]:c.replaceAll('_',' ').replaceAll('-',' ')}
for(let i=0;i<runs.length;i++){const r=runs[i],o=document.createElement('option');o.value=i;o.textContent=r.native_dimensions.join(' × ')+' / '+r.ram_mib+' MiB';$('#resolution').append(o)}
$('#summary').textContent=runs.map(r=>r.cases.length+' cases at '+r.native_dimensions.join('×')).join('; ')+'. Target: 30 game FPS.';
$('#evidence').innerHTML='<a href="results.json">All measurements</a> · '+runs.map(r=>{const p=r.native_dimensions.join('x')+'-'+r.ram_mib+'mib/';return '<a href="'+p+'result.json">'+r.native_dimensions[0]+' JSON</a> · <a href="'+p+'ares.log">native log</a>'}).join(' · ');
function value(r,key){return({case:title(r.case),fps:r.mean_game_fps,frame:r.cadence.mean_us,p95:r.cadence.p95_us,work:r.work.mean_us,work95:r.work.p95_us,draw:r.draw.mean_us,audio:r.audio.mean_us,wait:r.acquire.mean_us})[key]}
function render(){let rows=runs[Number($('#resolution').value)].cases.slice(),q=$('#filter').value.toLowerCase(),category=$('#category').value;rows=rows.filter(r=>(!q||title(r.case).includes(q))&&(category==='all'||(category==='intro'?(r.case.startsWith('intro')||r.case.startsWith('title')):r.case.startsWith(category))));rows.sort((a,b)=>{let x=value(a,sort),y=value(b,sort);return(typeof x==='string'?x.localeCompare(y):x-y)*(descending?-1:1)});$('#rows').replaceChildren();for(const r of rows){const tr=document.createElement('tr'),cells=[title(r.case),r.mean_game_fps.toFixed(1),...['frame','p95','work','work95','draw','audio','wait'].map(k=>(value(r,k)/1000).toFixed(2))];cells.forEach((v,i)=>{const td=document.createElement('td');td.textContent=v;if(i===1)td.className=r.mean_game_fps<28?'bad':'ok';tr.append(td)});const td=document.createElement('td'),bar=document.createElement('div');bar.className='bar';for(const[k,color]of[['draw','#427b9a'],['present','#a47b35'],['audio','#7970a4'],['acquire','#aebcba']]){const span=document.createElement('span');span.style.width=Math.min(120,r[k].mean_us/66667*120)+'px';span.style.background=color;span.title=k+': '+(r[k].mean_us/1000).toFixed(2)+'ms';bar.append(span)}td.append(bar);tr.append(td);$('#rows').append(tr)}}
for(const control of['#resolution','#category','#filter'])$(control).addEventListener('input',render);for(const th of document.querySelectorAll('th[data-key]'))th.addEventListener('click',()=>{if(sort===th.dataset.key)descending=!descending;else{sort=th.dataset.key;descending=sort!=='case'}render()});render();
</script></html>'''.replace('PAYLOAD',payload)
    if args.duel:
        duel_section='''<h2>Duel and common 2D screens</h2><p>These separate optimized-renderer measurements use 8 MiB at both resolutions, 8 warmup frames and 32 samples, with audio polling stubbed. They are not the same harness or audio workload as the world cases above. The JSON preserves their draw, board, overlay and presentation stage definitions. <a href="duel-final.json">Full measurements</a> · <a href="duel-before.json">Earlier baseline</a></p><div class="scroll"><table><thead><tr><th>Case</th><th>320 FPS</th><th>480 FPS</th><th>320 work ms</th><th>480 work ms</th><th>480 p95 work ms</th><th>480 earlier FPS</th></tr></thead><tbody id="duel-rows"></tbody></table></div>'''
        html=html.replace('<footer ',duel_section+'<footer ',1)
        extra='''
if(data.duel){const old=data.duel_before?data.duel_before.cases:[],names=[...new Set(data.duel.cases.map(r=>r.case))];names.sort((a,b)=>(data.duel.cases.find(r=>r.case===b&&r.width===480)?.work.mean_us||0)-(data.duel.cases.find(r=>r.case===a&&r.width===480)?.work.mean_us||0));for(const name of names){const a=data.duel.cases.find(r=>r.case===name&&r.width===320),b=data.duel.cases.find(r=>r.case===name&&r.width===480),before=old.find(r=>r.case===name&&r.width===480),tr=document.createElement('tr');const ms=(r,k='mean_us')=>r?(r.work[k]/1000).toFixed(2):'—',fps=r=>r?(1e6/r.cadence.mean_us).toFixed(1):'—';for(const value of[name.replaceAll('-',' '),fps(a),fps(b),ms(a),ms(b),ms(b,'p95_us'),fps(before)]){const td=document.createElement('td');td.textContent=value;tr.append(td)}document.querySelector('#duel-rows').append(tr)}}
'''
        html=html.replace('</script></html>',extra+'</script></html>')
    (out/'index.html').write_text(html)
    print('Report:',out/'index.html')

if __name__=='__main__':main()
