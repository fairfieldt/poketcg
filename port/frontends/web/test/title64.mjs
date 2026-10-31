// The actual web title uses shared N64 art without changing its input sequence.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const web=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const {chromium}=createRequire(import.meta.url)(path.join(web,'build/world-test/node_modules/playwright'));
const out=path.join(web,'build/title64');fs.mkdirSync(out,{recursive:true});
const shared=path.resolve(web,'../n64/adventure/assets/title64/logo-480.png');
assert.equal(crypto.createHash('sha256').update(fs.readFileSync(shared)).digest('hex'),crypto.createHash('sha256').update(fs.readFileSync(path.join(web,'dist/intro/title64-logo.png'))).digest('hex'));
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.wasm':'application/wasm','.png':'image/png','.json':'application/json','.ogg':'audio/ogg'};
const server=http.createServer((req,res)=>{let route=decodeURIComponent(new URL(req.url,'http://localhost').pathname);if(route==='/art'){res.setHeader('content-type','text/html');res.end('<!doctype html><div id="root"></div>');return;}if(route.endsWith('/'))route+='index.html';const src=path.join(web,'src',route),file=fs.existsSync(src)?src:path.join(web,'dist',route);fs.readFile(file,(err,data)=>{if(err){res.writeHead(404).end();return;}res.setHeader('content-type',mime[path.extname(file)]||'application/octet-stream');res.end(data);});});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const url=`http://127.0.0.1:${server.address().port}/`;
const browser=await chromium.launch({channel:'chrome',headless:true,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});const errors=[],results=[];
try {
 const art=await browser.newPage();await art.goto(url+'art');
 const pixels=await art.evaluate(async()=>{
  const {drawIntroBackdrop,beginIntroSprites}=await import('/js/intro_art.js');
  const load=src=>new Promise((resolve,reject)=>{const i=new Image();i.onload=()=>resolve(i);i.onerror=reject;i.src=src;});
  const names=['charizard','scyther','aerodactyl','title'];const pictures={};for(const n of names)pictures[n]=await load('/intro/'+n+'.png');pictures.title64=await load('/intro/title64-logo.png');
  const output=[];
  for(const scale of [1,1.5]) {
   const make=()=>{const c=document.createElement('canvas');c.width=320*scale;c.height=240*scale;const x=c.getContext('2d');x.scale(scale,scale);return[c,x];};
   for(let scene=0;scene<4;scene++) {
    const[a,x]=make(),[b,y]=make();drawIntroBackdrop(x,pictures,scene);
    y.fillStyle='#e7e7c6';y.fillRect(0,0,320,240);
    if(scene<3)y.drawImage(pictures[names[scene]],40,12,240,216);
    else {y.drawImage(pictures.title64,20,12,280,140);y.imageSmoothingEnabled=false;y.drawImage(pictures.title,0,110,160,34,40,177,240,51);}
    const aa=x.getImageData(0,0,a.width,a.height).data,bb=y.getImageData(0,0,b.width,b.height).data;
    if(aa.some((v,i)=>v!==bb[i]))throw Error('Art/copyright placement differs: '+scale+'/'+scene);
    output.push({width:a.width,height:a.height,scene,exact:true});
   }
   const[c,x]=make();x.fillStyle='#e7e7c6';x.fillRect(0,0,320,240);beginIntroSprites(x);x.fillStyle='#f00';x.fillRect(-100,-100,400,400);x.restore();
   const at=(xx,yy)=>Array.from(x.getImageData(Math.floor(xx*scale),Math.floor(yy*scale),1,1).data);
   if(at(40,12)[0]!==255||at(39,12)[0]!==231||at(280,12)[0]!==231||at(40,228)[0]!==231)throw Error('Original sprite clip changed');
  }
  return output;
 });assert.equal(pixels.length,8);results.push(...pixels);await art.close();
 for(const viewport of [{width:1280,height:720},{width:320,height:480}]) {
  const page=await browser.newPage({viewport});page.on('pageerror',e=>errors.push(e.message));
  await page.goto(url+'?fast=1');await page.waitForFunction(()=>window.tcgAdventure?.engine&&document.querySelector('.adventure-title-start:not(:disabled)'),{timeout:60000});
  await page.evaluate(()=>{const audio=tcgAdventure.audio,original=audio.event.bind(audio);window.titleAudio=[];audio.event=(...a)=>{titleAudio.push(a);return original(...a);};});
  const button=page.getByRole('button',{name:'Start',exact:true});const before=await button.boundingBox();
  await page.mouse.move(before.x+before.width/2,before.y+before.height/2);await page.mouse.down();const held=await button.boundingBox();
  assert(Math.abs(before.x+before.width/2-held.x-held.width/2)<0.1,'Pressed Start moves horizontally');
  await page.mouse.up();await page.waitForTimeout(150);
  assert.equal(await page.locator('.adventure-title').count(),1,'First press skips opening and keeps title');
  const canvas=await page.locator('.adventure-title canvas').evaluate(c=>({width:c.width,height:c.height}));assert.equal(canvas.width/canvas.height,4/3);
  const after=await button.boundingBox();assert(Math.abs(before.x-after.x)<0.1,'Skipping intro shifts Start');
  assert(await page.evaluate(()=>titleAudio.some(a=>a[0]===0&&a[1]===1)),'Title music uses original ID1');
  await page.waitForTimeout(1050);await page.screenshot({path:path.join(out,`title-${viewport.width}.png`)});
  await page.keyboard.press('Enter');await page.waitForSelector('.adventure-title',{state:'detached'});await page.waitForSelector('.adventure-menu');
  results.push({viewport,openingSkip:true,secondPressOpensDiary:true,centerStable:true,titleMusicId:1});await page.close();
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({passed:true,sharedLogoExact:true,results,errors},null,2)+'\n');
 console.log('TITLE64 WEB PASS: shared PNG hash; exact320/480 logo/footer/opening placement; original sprite clip; stableStart; opening skip→title→diary; original title music');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
