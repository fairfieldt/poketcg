// Actual browser UI: hide the result until landing, preserve event order,
// and honor animation/skip settings without changing the engine outcome.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const web=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(import.meta.url);
const {chromium}=require(path.join(web,'build/world-test/node_modules/playwright'));
const out=path.join(web,'build/coin-animation');fs.mkdirSync(out,{recursive:true});
const fixture=`<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,"><link rel="stylesheet" href="/css/style.css"><div id="root"></div>
<script type="module">
import {Duel,compileModule,C,SCREEN,TCG_PLAYER,TCG_OPPONENT} from '/js/engine.js';
import {DuelUI} from '/js/duel_ui.js';
const duel=new Duel(await compileModule('/tcg.wasm'));
const ui=new DuelUI(document.querySelector('#root'),duel,{pacing:true,onCoinSound:()=>window.sounds++});
const cards=duel.cards().filter(c=>c.isPokemon);window.sounds=0;
ui.names=['Mark','Sam'];ui.turnHolder=TCG_PLAYER;
ui.sideState=d=>({d,count:1,hand:7,deck:46,discard:0,prizes:6,prizeMask:63,slots:[{slot:0,deckIndex:0,card:cards[d===TCG_PLAYER?0:1],hp:40,color:0,energies:[1,0,0,0,0,0,0],status:0,pluspower:0,defender:0}]});
ui.render();window.fixture={ui,C,SCREEN,TCG_PLAYER,TCG_OPPONENT};window.ready=true;
</script>`;
const mime={'.js':'text/javascript','.css':'text/css','.png':'image/png','.wasm':'application/wasm','.json':'application/json'};
const server=http.createServer((req,res)=>{
 const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
 if(name==='/'){res.setHeader('content-type','text/html');res.end(fixture);return;}
 const source=path.join(web,'src',name),file=fs.existsSync(source)?source:path.join(web,'dist',name);
 fs.readFile(file,(err,data)=>{if(err){res.writeHead(404).end();return;}res.setHeader('content-type',mime[path.extname(file)]||'application/octet-stream');res.end(data);});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:'chrome',headless:true}),errors=[];
try {
 const page=await browser.newPage({viewport:{width:960,height:1000}});
 page.on('pageerror',e=>errors.push(e.message));
 await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>window.ready);
 await page.evaluate(()=>{const {ui,SCREEN,TCG_PLAYER}=window.fixture;ui.onScreen([SCREEN.COIN_TOSS,TCG_PLAYER,3,0]);const start=performance.now();window.toss=ui.onCoin([1,1,3,TCG_PLAYER]).then(()=>window.tossElapsed=performance.now()-start);});
 await page.waitForTimeout(220);
 assert.equal(await page.locator('.coin-result').textContent(),'');
 assert.equal(await page.locator('.coin-chip').count(),0);
 assert.notEqual(await page.locator('.coin').evaluate(n=>n.style.transform),'');
 await page.screenshot({path:path.join(out,'flight.png'),fullPage:true});
 await page.evaluate(()=>window.toss);
 assert(await page.evaluate(()=>window.tossElapsed)<950,'Intentional toss lasts about two-thirds of a second');
 assert.equal(await page.locator('.coin-result').textContent(),'Heads');
 assert.equal(await page.locator('.coin').getAttribute('aria-label'),'Heads');
 assert.deepEqual(await page.locator('.coin-chip').allTextContents(),['H']);
 await page.screenshot({path:path.join(out,'heads.png'),fullPage:true});
 const disabled=await page.evaluate(async()=>{const {ui,TCG_PLAYER}=window.fixture;ui.animationsDisabled=true;const t=performance.now();await ui.onCoin([0,2,3,TCG_PLAYER]);return performance.now()-t;});
 assert(disabled<200,'Disabled animation adds no pacing wait');
 assert.deepEqual(await page.locator('.coin-chip').allTextContents(),['H','T']);
 assert.equal(await page.locator('.coin-result').textContent(),'Tails');
 assert.equal(await page.evaluate(()=>window.sounds),1);
 await page.screenshot({path:path.join(out,'tails-history.png'),fullPage:true});
 const skipped=await page.evaluate(async()=>{const {ui,TCG_PLAYER}=window.fixture;ui.animationsDisabled=false;ui.skipDelay=true;ui.setBHeld(true);const t=performance.now();await ui.onCoin([1,3,3,TCG_PLAYER]);return performance.now()-t;});
 assert(skipped<200,'Allowed held-B skip adds no animation wait');
 assert.deepEqual(await page.locator('.coin-chip').allTextContents(),['H','T','H']);
 await page.evaluate(()=>{const {ui,SCREEN,TCG_OPPONENT}=window.fixture;ui.setBHeld(false);ui.skipDelay=false;ui.onScreen([SCREEN.COIN_TOSS,TCG_OPPONENT,1,0]);window.toss=ui.onCoin([0,1,1,TCG_OPPONENT]);});
 await page.waitForTimeout(100);await page.keyboard.press('z');await page.evaluate(()=>window.toss);
 assert.equal(await page.locator('.coin-result').textContent(),'Tails');
 assert.deepEqual(await page.locator('.coin-chip').allTextContents(),['T']);
 assert.equal(await page.evaluate(()=>window.fixture.ui.controls.length),0,'Skip control is removed before next ACK');
 await page.evaluate(()=>{const {ui,SCREEN,TCG_PLAYER}=window.fixture;ui.onScreen([SCREEN.COIN_TOSS,TCG_PLAYER,1,0]);window.toss=ui.onCoin([1,1,1,TCG_PLAYER]);});
 await page.waitForTimeout(100);await page.locator('.coin').click();await page.evaluate(()=>window.toss);
 assert.equal(await page.locator('.coin-result').textContent(),'Heads','Click skips through the same active control');
 await page.setViewportSize({width:320,height:700});
 await page.evaluate(()=>document.documentElement.style.setProperty('--u',4/3));
 await page.evaluate(async()=>{const {ui,SCREEN,TCG_PLAYER}=window.fixture;ui.animationsDisabled=true;ui.onScreen([SCREEN.COIN_TOSS,TCG_PLAYER,20,0]);for(let i=1;i<=20;i++)await ui.onCoin([i&1,i,20,TCG_PLAYER]);});
 assert.equal(await page.locator('.coin-chip').count(),12,'Long histories retain the most recent twelve results');
 assert.deepEqual(await page.locator('.coin-chip').allTextContents(),Array.from({length:12},(_,i)=>i%2?'T':'H'));
 const bounds=await page.locator('.coin-box').evaluate(n=>{
  const b=n.getBoundingClientRect(),f=n.closest('.overlay').getBoundingClientRect();
  return {left:b.left,right:b.right,top:b.top,bottom:b.bottom,fieldTop:f.top,fieldBottom:f.bottom};
 });
 assert(bounds.left>=0&&bounds.right<=320,'Coin panel fits a320px viewport');
 assert(bounds.top>=bounds.fieldTop-1&&bounds.bottom<=bounds.fieldBottom+1,'Coin panel fits the duel field');
 await page.screenshot({path:path.join(out,'narrow-tails.png'),fullPage:true});
 assert.deepEqual(errors,[]);
 console.log('WEB COIN PASS: flip/reveal, heads/tails art, multi-coin order, disabled/held/pressed skip, clean control ownership');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
