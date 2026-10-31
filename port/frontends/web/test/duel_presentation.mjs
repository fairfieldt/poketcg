// Source renderer + real compiled card records. Exercises long rules and narrow layouts.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(import.meta.url);
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const out = path.join(web, 'build/duel-presentation');
fs.mkdirSync(out, { recursive: true });
const fixture = `<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,"><link rel="stylesheet" href="/css/style.css">
<style>html,body{margin:0;background:#173d36}#root{margin:0} .status-line:empty{display:none}</style><div id="root"></div>
<script type="module">
import {Duel,compileModule,C,TCG_PLAYER,TCG_OPPONENT,REQ} from '/js/engine.js';
import {DuelUI,K} from '/js/duel_ui.js';
import {reflowRules} from '/js/gfx.js';
const duel = new Duel(await compileModule('/tcg.wasm'));
const cards = duel.cards();
const pokemon = cards.filter(c => c.isPokemon);
const longTrainer = cards.filter(c => c.isTrainer).sort((a,b) => b.description.length-a.description.length)[0];
const longPokemon = pokemon.slice().sort((a,b) => b.attacks.reduce((n,a)=>n+a.description.length,0)-a.attacks.reduce((n,a)=>n+a.description.length,0))[0];
const energy = cards.find(c=>c.isEnergy);
const ids=[longPokemon.id,longTrainer.id,energy.id,...pokemon.slice(0,17).map(c=>c.id)];
duel.cardId=(_d,i)=>ids[i%ids.length];
const ui = new DuelUI(document.querySelector('#root'),duel,{pacing:false});
ui.names=['Mark','Sam']; ui.turnHolder=TCG_PLAYER;
ui.sideState = d => ({d,count:6,hand:20,deck:23,discard:17,prizes:4,prizeMask:15,
slots:Array.from({length:6},(_,i)=>({slot:i,deckIndex:i,card:duel.card(ids[i===0?0:i+2]),
hp:i===0?10:40,color:i%7,energies:i===0?[10,10,10,10,10,10,10]:[1,2,0,0,0,0,2],
status:i===0?C.CONFUSED|C.DOUBLE_POISONED:0,pluspower:i===0?2:0,defender:i===0?1:0}))});
ui.render();
window.fixture={ui,duel,ids,longTrainer,longPokemon,K,REQ,TCG_PLAYER,TCG_OPPONENT,reflowRules};
window.showList=(index=0)=>{ui.hideOverlay();ui.clearMenu();ui.controls=[];const ctl=ui.cardListView({d:TCG_PLAYER,items:ids.map((_,i)=>i),header:'Hand',cursor:index,onSelect:()=>{}});ui.showOverlay('cardlist',ctl.wrap);ui.pushControl(ctl);window.ctl=ctl;};
window.showMenu=()=>{ui.hideOverlay();ui.clearMenu();ui.controls=[];ui.pushControl(ui.textMenu({items:['Hand','Attack','Check','Power','Retreat','Done'].map(label=>({label})),cols:3,onSelect:()=>{}}));};
window.ready=true;
</script>`;
const mime = {'.js':'application/javascript','.css':'text/css','.png':'image/png','.wasm':'application/wasm','.json':'application/json'};
const server=http.createServer((req,res)=>{
 const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
 if(name==='/'){res.setHeader('content-type','text/html');res.end(fixture);return;}
 const candidate=path.join(web,'src',name);
 const file=fs.existsSync(candidate)?candidate:path.join(web,'dist',name);
 fs.readFile(file,(err,data)=>{if(err){res.writeHead(404).end();return;}res.setHeader('content-type',mime[path.extname(file)]||'application/octet-stream');res.end(data);});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const errors=[],results=[];
const browser=await chromium.launch({channel:'chrome',headless:true});
try {
 for(const size of [{name:'native-width',width:320,height:480,u:4/3},{name:'large',width:960,height:1200,u:4}]) {
  const page=await browser.newPage({viewport:{width:size.width,height:size.height},reducedMotion:'reduce'});
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(()=>window.ready);
  await page.evaluate(u=>document.documentElement.style.setProperty('--u',u),size.u);
  const reflow=await page.evaluate(()=>{
   const {duel,reflowRules}=window.fixture;
   const descriptions=duel.cards().flatMap(card=>[card.description,...(card.attacks||[]).map(attack=>attack.description)]).filter(Boolean);
   return {count:descriptions.length,changed:descriptions.some(text=>reflowRules(text).replace(/\s/g,'')!==text.replace(/\s/g,'')),paragraph:reflowRules('First line\nwrap.\n\nNext paragraph.')};
  });
  assert(reflow.count>200);assert.equal(reflow.changed,false,'Reflow retains every rules word, punctuation mark and icon token');
  assert.equal(reflow.paragraph,'First line wrap.\n\nNext paragraph.');
  const board=await page.locator('.field').evaluate(node=>({width:node.clientWidth,scroll:node.scrollWidth,height:node.clientHeight}));
  assert(board.scroll<=board.width+1,'Crowded board overflows horizontally');
  assert.equal(await page.locator('.bench-slot').count(),10);
  assert.equal(await page.locator('.energy-group').count(),14);
  assert.match(await page.locator('.hud-status').first().textContent(),/Confused.*Double Poison.*PlusPower 2.*Defender 1/);
  assert.equal(await page.locator('[role=meter]').first().getAttribute('aria-valuenow'),'10');
  await page.screenshot({path:path.join(out,`${size.name}-board.png`),fullPage:true});
  await page.evaluate(()=>window.showList());
  assert.match(await page.locator('.clp-name').textContent(),/\w/);
  assert(await page.locator('.clp .cost').count()>0);
  assert(await page.locator('.clp-effect').count()>0);
  const list=await page.locator('.cl-body').evaluate(node=>({width:node.clientWidth,scroll:node.scrollWidth}));
  assert(list.scroll<=list.width+1,'Hand browser overflows horizontally');
  const font=await page.locator('.cl-item').first().evaluate(node=>({size:parseFloat(getComputedStyle(node).fontSize),family:getComputedStyle(node).fontFamily}));
  assert(font.family.includes('Trebuchet'));assert(Math.abs(font.size-8.25*size.u)<0.01);
  await page.screenshot({path:path.join(out,`${size.name}-pokemon-hand.png`),fullPage:true});
  await page.keyboard.press('ArrowDown');
  assert.equal(await page.evaluate(()=>window.ctl.cursor),1);
  assert.equal(await page.locator('.clp-effect').innerText(),await page.evaluate(()=>window.fixture.reflowRules(window.fixture.longTrainer.description)));
  await page.screenshot({path:path.join(out,`${size.name}-trainer-hand.png`),fullPage:true});
  await page.locator('.clp .btn').click();
  assert.equal(await page.locator('.modal .cp-section-title').first().textContent(),'Rules');
  assert.equal(await page.locator('.modal .cp-desc').innerText(),await page.evaluate(()=>window.fixture.reflowRules(window.fixture.longTrainer.description)));
  const detail=await page.locator('.modal .card-page').evaluate(node=>({width:node.clientWidth,scroll:node.scrollWidth,height:node.clientHeight,scrollHeight:node.scrollHeight}));
  assert(detail.scroll<=detail.width+1,'Long exact rules overflow horizontally');
  const headerBounds=await page.locator('.modal .cp-head').evaluate(node=>{const box=node.getBoundingClientRect(),modal=node.closest('.modal').getBoundingClientRect();return {top:box.top,bottom:box.bottom,modalTop:modal.top,modalBottom:modal.bottom};});
  assert(headerBounds.top>=headerBounds.modalTop && headerBounds.bottom<=headerBounds.modalBottom,'Initial card name/header is inside the modal');
  await page.screenshot({path:path.join(out,`${size.name}-trainer-detail.png`),fullPage:true});
  if(detail.scrollHeight>detail.height) {
   await page.keyboard.press('ArrowDown');
   assert(await page.locator('.modal .card-page').evaluate(node=>node.scrollTop)>0,'Controller/keyboard can scroll exact rules');
  }
  await page.screenshot({path:path.join(out,`${size.name}-trainer-detail-scrolled.png`),fullPage:true});
  await page.locator('.modal .card-page').evaluate(node=>node.scrollTop=node.scrollHeight);
  assert(await page.locator('.modal .close-btn').isVisible(),'Close remains reachable at the end of all rules');
  await page.keyboard.press('x');
  assert.equal(await page.evaluate(()=>window.ctl.cursor),1,'Inspection keeps hand position');
  await page.keyboard.press('ArrowDown');
  assert.equal(await page.locator('.clp-sub').textContent(),await page.evaluate(()=>`${['Fire','Grass','Lightning','Water','Fighting','Psychic','Colorless'][window.fixture.duel.card(window.fixture.ids[2]).type&7]} Energy`));
  await page.screenshot({path:path.join(out,`${size.name}-energy-hand.png`),fullPage:true});
  await page.evaluate(()=>window.showMenu());
  await page.screenshot({path:path.join(out,`${size.name}-actions.png`),fullPage:true});
  results.push({size,board,list,font,detail});
  await page.close();
 }
 assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({passed:true,results,errors},null,2)+'\n');
 console.log('DUEL PRESENTATION PASS: source UI + compiled cards, 320px/960px widths; full bench/70 Energy/statuses/low HP, three card types, exact long rules/controller scroll/cursor retained, no horizontal clipping or browser errors');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
