// Semantic effect markers/order numbers survive nested inspection and clear at redraw.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const web=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(import.meta.url);
const {chromium}=require(path.join(web,'build/world-test/node_modules/playwright'));
const output=path.join(web,'build/effect-markers');fs.mkdirSync(output,{recursive:true});
const fixture=`<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,"><link rel="stylesheet" href="/css/style.css"><div id="root"></div><script type="module">
import {Duel,compileModule,EV,SCREEN,PLAY_AREA_MODE as MODE,PLAY_AREA_MARK as MARK,TCG_PLAYER as P1,TCG_OPPONENT as P2} from '/js/engine.js';
import {DuelUI} from '/js/duel_ui.js';
const duel=new Duel(await compileModule('/tcg.wasm'));const card=duel.cards().find(c=>c.isPokemon);
const ui=new DuelUI(document.querySelector('#root'),duel,{pacing:false});ui.names=['Mark','Sam'];ui.turnHolder=P1;
ui.sideState=d=>({d,count:3,hand:4,deck:50,discard:0,prizes:3,prizeMask:7,slots:[0,1,2].map(i=>({slot:i,deckIndex:i,card,hp:card.hp,color:1,energies:[0,1,0,0,0,0,0],status:0,pluspower:0,defender:0})).concat([null,null,null])});
ui.render();window.fixture={ui,duel,card,EV,SCREEN,MODE,MARK,P1,P2};window.ready=true;
</script>`;
const mime={'.js':'application/javascript','.css':'text/css','.wasm':'application/wasm','.png':'image/png','.json':'application/json'};
const server=http.createServer((req,res)=>{const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);if(name==='/'){res.setHeader('content-type','text/html');res.end(fixture);return;}const source=path.join(web,'src',name),file=fs.existsSync(source)?source:path.join(web,'dist',name);fs.readFile(file,(err,data)=>{if(err){res.writeHead(404).end();return;}res.setHeader('content-type',mime[path.extname(file)]||'application/octet-stream');res.end(data);});});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const browser=await chromium.launch({channel:'chrome',headless:true});const errors=[];
try {
 const page=await browser.newPage({viewport:{width:960,height:900}});page.on('pageerror',e=>errors.push(e.message));
 await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>window.ready);
 await page.evaluate(()=>{const {ui,SCREEN,MODE,P1}=fixture;ui.setText('Select a Pokemon to retreat.');ui.onScreen([SCREEN.PLAY_AREA,P1,0,MODE.PEEK]);});
 assert.match(await page.locator('.tb-text').innerText(),/Play Area to inspect/);assert.doesNotMatch(await page.locator('.tb-text').innerText(),/Grass|retreat/i);
 await page.evaluate(()=>{const {ui,SCREEN,MODE,EV,MARK,P1}=fixture;ui.onScreen([SCREEN.PLAY_AREA,P1,0,MODE.ENERGY_FROM]);ui.onEvent({type:EV.PLAY_AREA_MARK,arg:[P1,1,MARK.ENERGY,0]});ui.onScreen([SCREEN.PLAY_AREA,P1,0,MODE.ENERGY_TO]);});
 assert.equal(await page.locator('.pa-row .effect-mark').count(),1);assert.match(await page.locator('.tb-text').innerText(),/receive Grass Energy/);
 await page.evaluate(()=>{fixture.ui.showCardPage(fixture.card);});await page.keyboard.press('Escape');
 assert.equal(await page.locator('.pa-row .effect-mark').count(),1,'Nested card view preserves held Energy');
 await page.evaluate(()=>{const {ui,EV,MARK,P1}=fixture;ui.onEvent({type:EV.PLAY_AREA_MARK,arg:[P1,1,MARK.NONE,0]});});
 assert.equal(await page.locator('.effect-mark').count(),0);
 await page.evaluate(()=>{const {ui,SCREEN,MODE,EV,MARK,P2}=fixture;ui.onScreen([SCREEN.PLAY_AREA,P2,0,MODE.CURSE_TO]);ui.onEvent({type:EV.PLAY_AREA_MARK,arg:[P2,1,MARK.DAMAGE,0]});});
 assert.equal(await page.locator('.pa-row .effect-mark').innerText(),'+');assert.match(await page.locator('.tb-text').innerText(),/damage counter/);
 await page.evaluate(()=>{const {ui,SCREEN,MODE,EV,MARK,P2}=fixture;ui.onScreen([SCREEN.PLAY_AREA,P2,0,MODE.BENCH_MULTI]);ui.onEvent({type:EV.PLAY_AREA_MARK,arg:[P2,1,MARK.TARGET,0]});ui.onEvent({type:EV.PLAY_AREA_MARK,arg:[P2,2,MARK.TARGET,0]});ui.onEvent({type:EV.PLAY_AREA_MARK,arg:[P2,1,MARK.NONE,0]});ui.onEvent({type:EV.PLAY_AREA_MARK,arg:[P2,5,MARK.TARGET,0]});});
 assert.equal(await page.locator('.pa-row .effect-mark').count(),1,'Undo removes only the selected mark');assert.equal(await page.locator('.bench-slot.empty .effect-mark').count(),0,'Empty slots never gain marks');
 await page.screenshot({path:path.join(output,'targets.png'),fullPage:true});
 await page.evaluate(()=>{const {ui,EV,P1}=fixture;ui.onEvent({type:EV.CARD_ORDER,arg:[P1,255,0,0]});ui.onEvent({type:EV.CARD_ORDER,arg:[P1,0,2,0]});ui.onEvent({type:EV.CARD_ORDER,arg:[P1,1,1,0]});ui.showOverlay('cardlist',ui.cardListView({d:P1,items:[0,1],selectable:false}).wrap);});
 assert.deepEqual(await page.locator('.cl-order').allTextContents(),['2','1']);
 await page.evaluate(()=>{fixture.ui.showCardPage(fixture.card);});await page.keyboard.press('Escape');assert.deepEqual(await page.locator('.cl-order').allTextContents(),['2','1']);
 await page.screenshot({path:path.join(output,'order.png'),fullPage:true});
 await page.evaluate(()=>{const {ui,SCREEN,P1}=fixture;ui.onScreen([SCREEN.MAIN,P1,1,0]);});assert(await page.locator('.effect-mark').count()>0,'HUD-only redraw preserves marks');
 await page.evaluate(()=>{const {ui,SCREEN,P1}=fixture;ui.onScreen([SCREEN.MAIN,P1,0,0]);});assert.equal(await page.locator('.effect-mark,.cl-order').count(),0,'Full redraw removes badges immediately');
 await page.evaluate(()=>{const {ui,EV,MARK,SCREEN,P1}=fixture;ui.onEvent({type:EV.PLAY_AREA_MARK,arg:[P1,1,MARK.DAMAGE,0]});ui.onScreen([SCREEN.EMPTY,P1,0,0]);});assert.equal(await page.locator('.effect-mark,.cl-order').count(),0,'EMPTY removes badges immediately');
 assert.deepEqual(errors,[]);console.log('EFFECT MARKERS PASS: Peek, phases, owned markers, undo, empty slots, order snapshots, nested inspection, HUD/full redraw and EMPTY');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
