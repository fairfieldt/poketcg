// Source UI with real compiled text/card records. Tutorial redraws stay out of speech.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(import.meta.url);
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const output = path.join(web, 'build/practice-reading');fs.mkdirSync(output,{recursive:true});
const fixture = `<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,"><link rel="stylesheet" href="/css/style.css">
<style>body{margin:0;background:#173d36}#root{margin:0}</style><div id="root"></div><script type="module">
import {Duel,compileModule,C,T,EV,TEXT_MODE,REQ,SCREEN,TCG_PLAYER,TCG_OPPONENT} from '/js/engine.js';
import {DuelUI} from '/js/duel_ui.js';
const duel=new Duel(await compileModule('/tcg.wasm'));
const card=duel.cards().find(c=>c.isPokemon);
const ui=new DuelUI(document.querySelector('#root'),duel,{pacing:false});
ui.names=['Mark','Sam'];ui.turnHolder=TCG_PLAYER;
ui.sideState=d=>({d,count:1,hand:7,deck:50,discard:0,prizes:2,prizeMask:3,
slots:[{slot:0,deckIndex:0,card,hp:card.hp,color:3,energies:[0,0,0,1,0,0,0],status:0,pluspower:0,defender:0},null,null,null,null,null]});
ui.render();window.acks=0;window.fixture={ui,duel,T,EV,TEXT_MODE,REQ,TCG_PLAYER,SCREEN};
window.summary='Mark\\'s turn 1\\n\\n'+[T.Turn1Instr1PracticeDuelText,T.Turn1Instr2PracticeDuelText,T.Turn1Instr3PracticeDuelText].map(id=>duel.text(id)).join('\\n\\n');
window.lesson=(id,custom)=>{ui.onEvent({type:EV.TEXT,arg:[1,TEXT_MODE.PRACTICE_SPEECH,0,0],text:id,str:custom??duel.text(id)});ui.onRequest({type:REQ.ACK,duelist:TCG_PLAYER,shown:TCG_PLAYER,arg:[0,0,0,0]}).then(()=>window.acks++);};
window.checklist=str=>ui.onEvent({type:EV.TEXT,arg:[0,TEXT_MODE.PRACTICE_CHECKLIST,3,0],text:T.PlayersTurnPracticeDuelText,str});
window.ready=true;
</script>`;
const mime={'.js':'application/javascript','.css':'text/css','.wasm':'application/wasm','.png':'image/png','.json':'application/json'};
const server=http.createServer((req,res)=>{
 const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
 if(name==='/'){res.setHeader('content-type','text/html');res.end(fixture);return;}
 const source=path.join(web,'src',name),file=fs.existsSync(source)?source:path.join(web,'dist',name);
 fs.readFile(file,(error,data)=>{if(error){res.writeHead(404).end();return;}res.setHeader('content-type',mime[path.extname(file)]||'application/octet-stream');res.end(data);});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:'chrome',headless:true});const errors=[];
try {
 for(const width of [320,960]) {
  const page=await browser.newPage({viewport:{width,height:width===320?650:1200}});
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>window.ready);
  await page.evaluate(u=>document.documentElement.style.setProperty('--u',u),width===320?1.3:3.5);
  await page.evaluate(()=>lesson(fixture.T.Turn1DrMason1PracticeDuelText));
  const longBounds=await page.locator('.practice-reading').boundingBox();
  assert(await page.locator('.practice-reading').isVisible());
  assert(await page.locator('.tb-text').isHidden(),'Mason speech is not duplicated in the bottom box');
  assert.match(await page.locator('.practice-message').innerText(),/Water Energy card/);
  assert.equal(await page.locator('.practice-continue').innerText(),'A/B: continue');
  const speech=await page.locator('.practice-message').innerText();
  await page.evaluate(()=>checklist(summary));
  assert.equal(await page.locator('.practice-message').innerText(),speech,'Numbered redraw cannot replace an acknowledged lesson');
  await page.keyboard.down('z');await page.waitForFunction(()=>acks===1);
  await page.evaluate(()=>lesson(fixture.T.Turn1DrMason3PracticeDuelText));
  const shortBounds=await page.locator('.practice-reading').boundingBox();
  assert.deepEqual(shortBounds,longBounds,'Short Finally explanation uses the same full reading panel');
  assert.match(await page.locator('.practice-message').innerText(),/Finally, attack your opponent/);
  await page.keyboard.down('z');await page.waitForTimeout(50);
  assert.equal(await page.evaluate(()=>acks),1,'A held through the next explanation does not skip it');
  await page.keyboard.up('z');await page.screenshot({path:path.join(output,`${width}-finally.png`)});
  await page.keyboard.press('z');await page.waitForFunction(()=>acks===2);
  await page.evaluate(()=>lesson(fixture.T.LetsPlayTheGamePracticeDuelText));
  assert.match(await page.locator('.practice-message').innerText(),/3\. Choose Attack/);
  const layout=await page.locator('.practice-reading').evaluate(node=>({height:node.clientHeight,scrollHeight:node.scrollHeight,width:node.clientWidth,scrollWidth:node.scrollWidth}));
  assert(layout.scrollWidth<=layout.width+1,'Recap has no horizontal overflow');
  assert(layout.scrollHeight<=layout.height+1,'All three recap steps fit in the large panel');
  await page.screenshot({path:path.join(output,`${width}-recap.png`)});
  await page.keyboard.press('z');await page.waitForFunction(()=>acks===3);
  await page.evaluate(()=>lesson(fixture.T.Turn1DrMason1PracticeDuelText,Array.from({length:24},(_,i)=>'Lesson line '+(i+1)).join('\n')));
  await page.keyboard.down('z');assert.match(await page.locator('.practice-continue').innerText(),/2 \/ 3/);
  await page.keyboard.down('z');assert.match(await page.locator('.practice-continue').innerText(),/2 \/ 3/);
  assert.equal(await page.evaluate(()=>acks),3,'A local page turn does not finish the engine ACK');
  await page.keyboard.up('z');await page.keyboard.press('x');assert.match(await page.locator('.practice-continue').innerText(),/3 \/ 3/);
  await page.keyboard.press('z');await page.waitForFunction(()=>acks===4);
  await page.evaluate(()=>fixture.ui.onEvent({type:fixture.EV.TURN,arg:[fixture.TCG_PLAYER,2,0,0]}));
  assert(await page.locator('.practice-reading').isHidden());
  assert.equal(await page.evaluate(()=>fixture.ui.practiceChecklist),'','A new turn clears the old action list');
  await page.close();
 }
 assert.deepEqual(errors,[]);
 console.log('PRACTICE READING PASS: actual Mason text at320/960px; uniform long/Finally panels; hint-only bottom; persistent recap; fresh press per page; unchanged single ACK lifetime; new-turn clearing');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
