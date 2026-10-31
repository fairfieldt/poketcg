// Real WASM frame cadence, continuous tile motion, stops, collisions and warps.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';
const web=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const isolated=path.join(web,'build/movement-engine-test');
await fs.mkdir(path.join(isolated,'js/gen'),{recursive:true});
await fs.writeFile(path.join(isolated,'package.json'),'{"type":"module"}\n');
for(const name of ['engine.js','adventure_engine.js'])await fs.copyFile(path.join(web,'src/js',name),path.join(isolated,'js',name));
await fs.copyFile(path.join(web,'dist/js/gen/consts.js'),path.join(isolated,'js/gen/consts.js'));
const {AdventureEngine,Cartridge}=await import(pathToFileURL(path.join(isolated,'js/adventure_engine.js')));
const {C}=await import(pathToFileURL(path.join(isolated,'js/engine.js')));
const module=await WebAssembly.compile(await fs.readFile(path.join(web,'dist/tcg.wasm')));
const fixture=JSON.parse(await fs.readFile(path.join(web,'src/saves/diary2-after-tutorial.json'),'utf8'));
const completed=new Uint8Array(Buffer.from(fixture.save,'base64'));
const world=JSON.parse(await fs.readFile(path.resolve(web,'../../world3d/generated/world.json'),'utf8'));
const close=(actual,expected,message)=>assert(Math.abs(actual-expected)<.00021,`${message}: ${actual} != ${expected}`);
const pose=(view)=>view.presentation.player;
const originalClock=Date.now;
Date.now=()=>1900000000000;
async function loadDiary(){
 const frames=[],cartridge=new Cartridge(null);
 const engine=await AdventureEngine.load(module,{
  frame(view){frames.push(view);return Promise.resolve();},
  message(title,text){assert.fail(`Completed tutorial unexpectedly displayed ${title}: ${text}`);},
  duel(){assert.fail('Completed tutorial unexpectedly started a duel');},
 },cartridge);
 assert(engine.installDiary(2,completed));assert.equal(await engine.start(2),0);
 assert.equal(engine.view.map,C.MASON_LABORATORY);assert.equal(engine.view.x,14);assert.equal(engine.view.y,8);
 frames.length=0;
 return {engine,frames,cartridge};
}
async function checkpoint(session){await session.engine.action(6);return session.cartridge.read('adventure2');}
function routeTo(view,x,y){
 const map=world.maps[view.map],blocked=new Set(view.npcs.filter(npc=>npc.visible).map(npc=>`${npc.x},${npc.y}`));
 const queue=[[view.x,view.y,[]]],seen=new Set([`${view.x},${view.y}`]);
 for(let at=0;at<queue.length;at++){
  const [px,py,steps]=queue[at];if(px===x&&py===y)return steps;
  for(const [direction,dx,dy] of [[0,0,-2],[1,2,0],[2,0,2],[3,-2,0]]){
   const nx=px+dx,ny=py+dy,key=`${nx},${ny}`;
   if(nx<0||ny<0||nx>=map.width||ny>=map.height||seen.has(key)||blocked.has(key)||(map.permissions[ny>>1][nx>>1]&128))continue;
   seen.add(key);queue.push([nx,ny,[...steps,direction]]);
  }
 }
 assert.fail(`No source collision-grid route to ${x},${y}`);
}
try{
 for(const running of [false,true]){
  const live=await loadDiary(),reference=await loadDiary();
  const origin=live.engine.view,perTile=running?4:8,speed=running?.5:.25;
  let previous=pose(origin);
  for(let tile=0;tile<4;tile++){
   const start=live.frames.length;
   assert.equal(await live.engine.action(2,+running),0);
   const samples=live.frames.slice(start);
   assert.equal(samples.length,perTile,`${running?'run':'walk'} tile${tile+1} must have no redundant idle AV_FRAME`);
   for(const view of samples){
    const current=pose(view);
    assert.equal(view.map,C.MASON_LABORATORY);close(current.x,14,'straight corridor X');
    close(current.z-previous.z,speed,'constant velocity, including tile boundary');
    close(Math.sin(current.gait),Math.sin(previous.gait+Math.PI*speed),'distance-bound gait continuity');
    close(Math.cos(current.gait),Math.cos(previous.gait+Math.PI*speed),'gait wrap continuity');
    if(running)assert.equal(current.lean,6,'run lean must not reset between held tiles');
    previous=current;
   }
   await reference.engine.action(2,+running);
  }
  assert.equal(live.frames.length,4*perTile);assert.equal(live.engine.view.y,16);
  const canonicalBefore=live.engine.view,beforeStopFrames=live.frames.length;
  assert.equal(await live.engine.stopMovement(),0);
  assert.equal(live.frames.length,beforeStopFrames+1,'release emits one idle refresh');
  const stopped=live.engine.view;
  assert.equal(pose(stopped).gait,0);assert.equal(pose(stopped).lean,0);
  assert(Math.abs(pose(stopped).bob)<=.0081);
  for(const key of ['x','y','map','totalCards','selectedDeck'])assert.equal(stopped[key],canonicalBefore[key],`stop preserves ${key}`);
  assert.deepEqual(await checkpoint(live),await checkpoint(reference),'stop preserves complete canonical diary bytes, including RNG/events/cards');

  // Follow the original collision grid through the laboratory exit. The stop
  // operation must not change the warp destination or any source outcomes.
  const directions=routeTo(live.engine.view,14,28);const warpFramesStart=live.frames.length;
  for(const direction of directions){await live.engine.action(direction);await reference.engine.action(direction);}
  assert.equal(live.engine.view.map,0);assert.equal(live.engine.view.overworldSelection,C.OWMAP_MASON_LABORATORY);
  const warpFrames=live.frames.slice(warpFramesStart),lastLab=warpFrames.findLast(view=>view.map===C.MASON_LABORATORY);
  assert(lastLab&&warpFrames.some(view=>view.map===0),'actual exit includes old and destination map frames');
  close(pose(lastLab).x,14,'warp reaches original source X');close(pose(lastLab).z,28,'warp reaches original source Y');
  assert.deepEqual(await checkpoint(live),await checkpoint(reference),'extra stop does not change canonical warp/save results');
 }
 // A blocked step still emits its necessary refresh and updates facing, while
 // leaving the player outside Dr. Mason's occupied source tile.
 const blocked=await loadDiary();
 await blocked.engine.action(1);await blocked.engine.action(3);
 const blockedBefore=blocked.engine.view,blockedFrame=blocked.frames.length;
 assert(blockedBefore.npcs.some(npc=>npc.visible&&npc.x===14&&npc.y===6));
 await blocked.engine.action(0);
 assert.equal(blocked.frames.length,blockedFrame+1,'failed move retains fallback frame');
 assert.equal(blocked.engine.view.direction,0);assert.equal(blocked.engine.view.x,14);assert.equal(blocked.engine.view.y,8);
 assert.equal(blocked.engine.view.totalCards,blockedBefore.totalCards);
 await blocked.engine.stopMovement();
 assert.equal(pose(blocked.engine.view).gait,0);assert.equal(pose(blocked.engine.view).lean,0);

 // Keep the requested 70% tutorial speed: only moving samples are counted, so
 // source text/wait frames cannot hide a movement-duration regression.
 const tutorialFrames=[];let tutorialDuels=0,tutorialAtDuel=0;
 const tutorial=await AdventureEngine.load(module,{
  frame(view){tutorialFrames.push(view);return Promise.resolve();},message(){return 0;},card(){return 0;},
  menu(title,items){
   if(items[0]==='Yes'&&items[1]==='No')return 0;
   if(items.includes('Charmander & Friends'))return 0;
   if(items[0]==='Normal duel'&&items[1]==='Practice')return 1;
   if(items.length===8&&items.at(-1)==='Exit')return 7;
   assert.fail(`Unexpected tutorial menu: ${title}`);
  },duel(){tutorialAtDuel=tutorialFrames.length;tutorialDuels++;return 0;},
 },new Cartridge(null));
 assert.equal(await tutorial.start(1,{fresh:true,name:'MARK',seed:123}),0);
 let island=0,run=0,approach=0;
 for(let i=1;i<tutorialAtDuel;i++){
  const previous=tutorialFrames[i-1],current=tutorialFrames[i];
  if(previous.map!==current.map)continue;
  if(current.map===0){
   const a=previous.presentation.world,b=current.presentation.world;
   if(Math.hypot(b.x-a.x,b.z-a.z)>.0001)island++;
  }else if(current.map===C.MASON_LABORATORY){
   const a=pose(previous),b=pose(current);
   if(Math.hypot(b.x-a.x,b.z-a.z)>.0001){if(b.lean)run++;else approach++;}
  }
 }
 assert.equal(island,60);assert.equal(run,51);assert.equal(approach,57);
 assert.equal(tutorialDuels,1);assert.equal(tutorial.view.x,14);assert.equal(tutorial.view.y,8);
 assert.equal(tutorial.view.totalCards,90);
 console.log('MOVEMENT WASM PASS: four-tile walk32/run16 frames, constant boundary velocity/gait, one-frame stop and identical canonical saves, blocked turn, source exit warp, tutorial island60/run51/approach57 frames');
}finally{Date.now=originalClock;}
