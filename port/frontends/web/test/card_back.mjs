// Asset-only visual fixture using the real browser portrait/back CSS and sprites.
// No game rebuild or production UI changes are required.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const repo = path.resolve(web, '../../..');
const require = createRequire(import.meta.url);
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const out = path.join(web, 'build/card-back-review');
fs.mkdirSync(out, {recursive:true});
const fixture = variant => `<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,">
<link rel="stylesheet" href="/${variant}/css/style.css">
<style>body{padding:24px;background:#173d36}.duel{padding:20px;background:var(--screen);width:fit-content;max-width:none}.portrait-row{display:flex;gap:calc(8px*var(--u));margin-bottom:calc(10px*var(--u))}.status-line:empty{display:none}</style>
<div class="duel"><div class="portrait-row"></div><div class="prizes-box"><div class="prize-grid"></div></div></div>
<script type="module">
import {art} from '/js/gfx.js';
for(const id of [10,84,103]) document.querySelector('.portrait-row').appendChild(art(id));
for(let i=0;i<6;i++){const p=document.createElement('button');p.className='prize-card';document.querySelector('.prize-grid').appendChild(p);}
const back=new Image();back.src='/${variant}/img/card-back.png';await back.decode();
window.fixture={back};window.ready=true;
</script>`;
const mime={'.js':'application/javascript','.css':'text/css','.png':'image/png','.wasm':'application/wasm'};
const server=http.createServer((req,res)=>{
 let name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
 if(name==='/before/'||name==='/after/'){res.setHeader('content-type','text/html');res.end(fixture(name.split('/')[1]));return;}
 let file;
 if(name==='/before/img/card-back.png') file=path.join(repo,'port/reviews/card-back-texels/before/web.png');
 else {
  name=name.replace(/^\/(before|after)\//,'/');
  const source=path.join(web,'src',name);file=fs.existsSync(source)?source:path.join(web,'dist',name);
 }
 fs.readFile(file,(err,data)=>{if(err){res.writeHead(404).end();return;}res.setHeader('content-type',mime[path.extname(file)]||'application/octet-stream');res.end(data);});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:'chrome',headless:true});
const errors=[],results=[];
try {
 for(const size of [{name:'native-high',u:1.5,width:480,height:420},{name:'desktop',u:3,width:840,height:700}]) {
  for(const variant of ['before','after']) {
   const page=await browser.newPage({viewport:{width:size.width,height:size.height}});
   page.on('pageerror',error=>errors.push(error.message));
   await page.goto(`http://127.0.0.1:${server.address().port}/${variant}/`);
   await page.waitForFunction(()=>window.ready);
   await page.evaluate(u=>document.documentElement.style.setProperty('--u',u),size.u);
   await page.waitForFunction(()=>[...document.images].every(image=>image.complete));
   const dimensions=await page.evaluate(()=>({back:[window.fixture.back.naturalWidth,window.fixture.back.naturalHeight],
     portrait:document.querySelector('.art').getBoundingClientRect().toJSON(),
     prize:document.querySelector('.prize-card').getBoundingClientRect().toJSON(),
     scroll:document.documentElement.scrollWidth,width:innerWidth}));
   assert.deepEqual(dimensions.back,[256,357]);
   assert(Math.abs(dimensions.prize.width-28*size.u)<.01);
   assert(dimensions.scroll<=dimensions.width,'Fixture overflows the viewport');
   assert.equal(await page.locator('.prize-card').count(),6);
   await page.locator('.duel').screenshot({path:path.join(out,`${variant}-${size.name}.png`)});
   results.push({variant,size,dimensions});await page.close();
  }
 }
 assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'validation.json'),JSON.stringify({passed:true,method:'Actual source portrait/card-back CSS and card sprite sheet; static asset fixture',results,errors},null,2)+'\n');
 console.log('CARD BACK WEB PASS: source art/CSS, high-resolution native scale and desktop scale; six backs, correct image dimensions, no overflow/errors');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
