// Production resize hooks + compiled cards. Synthetic crowded counts stress wrapping.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const web=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const {chromium}=createRequire(import.meta.url)(path.join(web,'build/world-test/node_modules/playwright'));
const out=path.join(web,'build/duel-presentation');fs.mkdirSync(out,{recursive:true});
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.wasm':'application/wasm','.png':'image/png','.json':'application/json'};
const server=http.createServer((req,res)=>{
 let route=decodeURIComponent(new URL(req.url,'http://localhost').pathname);if(route.endsWith('/'))route+='index.html';
 const base=path.join(web,'src');let file=path.resolve(base,'.'+route);
 if(!file.startsWith(base+path.sep)){res.writeHead(403).end();return;}
 if(!fs.existsSync(file))file=path.join(web,'dist',route);
 fs.readFile(file,(error,data)=>{if(error){res.writeHead(404).end();return;}res.setHeader('content-type',mime[path.extname(file)]||'application/octet-stream');res.end(data);});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const base=`http://127.0.0.1:${server.address().port}/`;
const browser=await chromium.launch({channel:'chrome',headless:true,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
const errors=[],results=[];
try {
 for(const mode of ['standalone','adventure'])for(const [width,height] of [[1280,720],[800,600],[320,480]]) {
  const page=await browser.newPage({viewport:{width,height}});page.on('pageerror',error=>errors.push(error.message));
  await page.goto(base+(mode==='standalone'?'?fast=1&duel=1':'?fast=1'));
  if(mode==='standalone') {
   await page.getByRole('button',{name:'2 Players (same screen)',exact:true}).click();
   await page.locator('.deck-tile:not(:disabled)').first().click();await page.locator('.deck-tile:not(:disabled)').first().click();
   await page.waitForSelector('.duel .field');
  } else await page.waitForFunction(()=>window.tcgAdventure?.engine,{timeout:60000});
  await page.evaluate(async mode=>{
   const {DuelUI}=await import('/js/duel_ui.js');const {C,TCG_PLAYER}=await import('/js/engine.js');
   const app=mode==='standalone'?window.tcgApp:window.tcgAdventure;let ui=app.ui;
   if(mode==='adventure') {
    app.mode='duel';app.overlay.classList.add('hidden');app.duelLayer.classList.remove('hidden');
    app.duelLayer.innerHTML='<div class="screen duel-screen"><div class="duel-bar"><span class="duel-title">Mark vs. Sam</span><button class="btn small">Save and quit</button></div><div id="viewport-duel"></div></div>';
    ui=new DuelUI(document.querySelector('#viewport-duel'),app.duel,{pacing:false});app.ui=ui;
   }
   const card=app.duel.cards().find(card=>card.isPokemon&&card.hp>=100);
   ui.sideState=d=>({d,count:6,hand:20,deck:23,discard:17,prizes:4,prizeMask:15,
    slots:Array.from({length:6},(_,i)=>({slot:i,deckIndex:i,card,hp:i?40:10,color:i%7,
     energies:i?[1,2,0,0,0,0,2]:[10,10,10,10,10,10,10],
     status:i?0:C.CONFUSED|C.DOUBLE_POISONED,pluspower:i?0:2,defender:i?0:1}))});
   ui.names=['Mark','Sam'];ui.turnHolder=TCG_PLAYER;ui.hideOverlay();ui.clearMenu();ui.setText('');ui.render();
   ui.textMenu({items:['Hand','Attack','Check','Power','Retreat','Done'].map(label=>({label})),cols:3,onSelect:()=>{}});
   ui.setStatus('Choose an action.');
   dispatchEvent(new Event('resize'));
  },mode);
  const layout=await page.locator('.duel').evaluate(node=>({
   unit:Number(getComputedStyle(document.documentElement).getPropertyValue('--u')),
   width:node.getBoundingClientRect().width,scroll:node.scrollWidth,
   textboxBottom:node.querySelector('.textbox').getBoundingClientRect().bottom,
   lastActionBottom:node.querySelector('.tm-item:last-child').getBoundingClientRect().bottom,
   viewportHeight:innerHeight,actions:node.querySelectorAll('.tm-item').length
  }));
  assert.equal(layout.actions,6);assert(layout.scroll<=layout.width+1,`${mode} horizontal overflow`);
  assert(layout.textboxBottom<=height-2,`${mode} ${width}x${height}: textbox below viewport (${layout.textboxBottom})`);
  assert(layout.lastActionBottom<=height-2,`${mode}: last action below viewport`);
  await page.screenshot({path:path.join(out,`viewport-${mode}-${width}x${height}.png`)});
  results.push({mode,viewport:{width,height},...layout});await page.close();
 }
 assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'viewport-result.json'),JSON.stringify({passed:true,results,errors},null,2)+'\n');
 console.log('DUEL VIEWPORT PASS: production standalone/adventure resize hooks; full benches, grouped Energy, low HP and multiple conditions; all actions fit 1280x720, 800x600 and 320x480');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
