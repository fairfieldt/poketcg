// Unlimited browser diaries use the real canonical C save codec and duel CRC.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';
const web=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const isolated=path.join(web,'build/diaries-test');
await fs.mkdir(path.join(isolated,'js/gen'),{recursive:true});
await fs.writeFile(path.join(isolated,'package.json'),'{"type":"module"}\n');
for(const file of ['engine.js','adventure_engine.js'])await fs.copyFile(path.join(web,'src/js',file),path.join(isolated,'js',file));
await fs.copyFile(path.join(web,'dist/js/gen/consts.js'),path.join(isolated,'js/gen/consts.js'));
const {AdventureEngine,Cartridge}=await import(pathToFileURL(path.join(isolated,'js/adventure_engine.js')));
const {Engine,REQ,C}=await import(pathToFileURL(path.join(isolated,'js/engine.js')));
const module=await WebAssembly.compile(await fs.readFile(path.join(web,'dist/tcg.wasm')));
const seed=JSON.parse(await fs.readFile(path.join(web,'src/saves/diary2-after-tutorial.json'),'utf8'));
const completed=new Uint8Array(Buffer.from(seed.save,'base64'));
class MemoryStorage{
 constructor(){this.value=null;this.failNext=false;this.writes=0;}
 getItem(){return this.value;}
 setItem(key,value){if(this.failNext){this.failNext=false;throw Error('storage failed');}this.value=value;this.writes++;}
}
const storage=new MemoryStorage(),cartridge=new Cartridge(storage);
let engine=await AdventureEngine.load(module,{},cartridge);
for(let id=1;id<=48;id++)assert(engine.installDiary(id,completed));
assert(engine.installDiary(9007199254740991,completed));
assert.equal(engine.diaries().length,49);assert.equal(engine.nextDiaryId(),49);
assert(engine.profile(48));assert(engine.profile(9007199254740991));
assert(!engine.profile(0));assert(!engine.profile(1.5));assert(!engine.profile(Number.MAX_SAFE_INTEGER+1));
assert(!engine.installDiary(48,completed));
const all=cartridge.export();assert(all.length>262144,'Exports must support more than the former total-size cap');
const restored=new Cartridge(null),validator=await AdventureEngine.load(module,{},restored);
assert(validator.importCartridge(all));assert.equal(validator.diaries().length,49);
assert.deepEqual(restored.read('adventure48'),completed);
const corrupt=new Cartridge(null);assert(corrupt.import(all));
const bad=corrupt.read('adventure48');bad[bad.length-1]^=1;assert(corrupt.commit({adventure48:bad}));
assert(!validator.importCartridge(corrupt.export()));assert.deepEqual(restored.export(),all);
const beforeInstall=cartridge.export();storage.failNext=true;
assert(!engine.installDiary(100,completed));assert.deepEqual(cartridge.export(),beforeInstall);
storage.failNext=true;assert(!engine.importCartridge(all));assert.deepEqual(cartridge.export(),beforeInstall);

// Stop an actual duel at its saved human menu, keeping original engine bytes.
class Interrupted extends Error{}
const duel=await Engine.load(module);let checkpoint,stopHash,requests=0,randomState=37;
const rnd=(n)=>{randomState=(Math.imul(randomState,1664525)+1013904223)>>>0;return n?(randomState>>>8)%n:0;};
const setup={duelists:[{kind:'human',name:'MARK',cards:duel.deckList(C.CHARMANDER_AND_FRIENDS_DECK_ID+2).cards},
 {kind:'ai',name:'SAM',deckId:C.SAMS_NORMAL_DECK_ID}],prizes:2,seed:12345,animationsDisabled:true};
await duel.run(setup,{onSave(bytes){checkpoint=bytes;},onRequest(r){
 assert(++requests<2000);
 if(checkpoint&&r.type===REQ.DUEL_MENU){stopHash=duel.stateHash();throw new Interrupted();}
 if(r.type===REQ.ACK)return 0;
 if(r.type===REQ.YES_NO)return rnd(4)?1:0;
 if(r.type===REQ.CARD_LIST)return r.arg[0]||!r.numItems||!rnd(8)?-1:rnd(r.numItems);
 if(r.type===REQ.PLAY_AREA)return r.arg[1]||!rnd(10)?-1:r.arg[0]+rnd(r.numItems||1);
 return !r.numItems||!rnd(10)?-1:rnd(r.numItems);
}}).catch(error=>{if(!(error instanceof Interrupted))throw error;});
assert(checkpoint&&duel.savedDuelInfo(checkpoint));
const pendingStorage=new MemoryStorage(),pendingCartridge=new Cartridge(pendingStorage);
let pending;
const story={frame(){return Promise.resolve();},message(){return 0;},card(){return 0;},menu(title,items){
 if(items.length===8)return 7;
 if(items[0]==='Yes')return 0;
 if(items.includes('Charmander & Friends'))return 0;
 return 0;
},duel(){assert(pending.saveDuel(checkpoint));throw new Interrupted();}};
for(const id of [7,37]){
 pending=await AdventureEngine.load(module,story,pendingCartridge);
 await pending.start(id,{fresh:true,name:`P${id}`,seed:123}).then(()=>assert.fail('The real pending source duel should interrupt'),error=>assert(error instanceof Interrupted));
 assert.deepEqual(pendingCartridge.read(`duel-${id}`),checkpoint);
 assert.equal(new TextDecoder().decode(pendingCartridge.read(`duelowner-${id}`)),String(id));
 assert(pending.profile(id));
}
assert(pending.installDiary(89,completed));
const pendingPair=[pendingCartridge.read('duel-7'),pendingCartridge.read('duel-37')];
assert.equal(await pending.start(89),0,'An unrelated diary must remain playable');
assert.deepEqual(pendingCartridge.read('duel-7'),pendingPair[0]);assert.deepEqual(pendingCartridge.read('duel-37'),pendingPair[1]);
// A peer's interrupted duel does not block another diary's gift transaction.
let pendingGift=1;
const gifter=await AdventureEngine.load(module,{frame(){return Promise.resolve();},peer:async()=>37,
 message(title,text){assert(!/finish.*duel/i.test(text));return 0;},
 menu(title,items){if(title.startsWith('Cards to send:'))return pendingGift?1:0;if(items[0]==='Add'){pendingGift--;return 0;}return 0;}
},pendingCartridge);
assert.equal(await gifter.loadProfile(89),0);await gifter.action(11,0);
assert.deepEqual(pendingCartridge.read('duel-37'),checkpoint);assert(gifter.profile(37));
const pendingExport=pendingCartridge.export();
const imported=await AdventureEngine.load(module,{},new Cartridge(null));assert(imported.importCartridge(pendingExport));
for(const id of [7,37]){
 let resumed=false;
 const reload=await AdventureEngine.load(module,{...story,duel:async(loaded,saved)=>{
  assert.deepEqual(saved,checkpoint);resumed=true;
  const live=await Engine.load(module);
  await live.continueDuel(setup,saved,{onRequest(r){
   if(r.type===REQ.DUEL_MENU){assert.equal(live.stateHash(),stopHash);throw new Interrupted();}
   return 0;
  }}).catch(error=>{if(!(error instanceof Interrupted))throw error;});
  return 0;
 }},pendingCartridge);
 assert.equal(await reload.start(id),0);assert(resumed);
 assert.equal(pendingCartridge.read(`duel-${id}`),null);
 if(id===7)assert.deepEqual(pendingCartridge.read('duel-37'),checkpoint);
}

// Legacy global saves migrate as one storage replacement, preserving payloads.
const legacy=new Cartridge(null);
assert(legacy.commit({adventure2:pendingCartridge.read('adventure37'),duel:checkpoint,duelowner:new Uint8Array([2])}));
const legacyStorage=new MemoryStorage();legacyStorage.value=new TextDecoder().decode(legacy.export());legacyStorage.failNext=true;
const failedMigration=new Cartridge(legacyStorage);
assert.deepEqual(failedMigration.read('duel'),checkpoint);assert.equal(failedMigration.read('duel-2'),null);
assert(failedMigration.migrateLegacyDuels());
assert.equal(failedMigration.read('duel'),null);assert.equal(failedMigration.read('duelowner'),null);
assert.deepEqual(failedMigration.read('duel-2'),checkpoint);assert.deepEqual(failedMigration.read('duelowner-2'),new Uint8Array([2]));
const migrated=await AdventureEngine.load(module,{},new Cartridge(null));assert(migrated.importCartridge(legacy.export()));
// Owner-only files are legitimate between story checkpoint and first duel menu.
const ownerOnly=new Cartridge(null);assert(ownerOnly.commit({adventure2:completed,duelowner:new Uint8Array([2])}));
const ownerImport=await AdventureEngine.load(module,{},new Cartridge(null));assert(ownerImport.importCartridge(ownerOnly.export()));
assert.equal(ownerImport.cartridge.read('duel-2'),null);assert.deepEqual(ownerImport.cartridge.read('duelowner-2'),new Uint8Array([2]));

const invalidOwner=new Cartridge(null);assert(invalidOwner.import(pendingExport));
assert(invalidOwner.commit({'duelowner-7':new TextEncoder().encode('37')}));
const clean=engine.cartridge.export();assert(!engine.importCartridge(invalidOwner.export()));assert.deepEqual(engine.cartridge.export(),clean);
const invalidDuel=new Cartridge(null);assert(invalidDuel.import(pendingExport));
const damaged=invalidDuel.read('duel-37');damaged[100]^=1;assert(invalidDuel.commit({'duel-37':damaged}));
assert(!engine.importCartridge(invalidDuel.export()));assert.deepEqual(engine.cartridge.export(),clean);

// Async selection targets arbitrary diaries; paired writes roll back together.
let choice=48,remaining=1,selections=0;
const peerHandlers={frame(){return Promise.resolve();},message(){return 0;},card(){return 0;},
 peer:async({activeId,diaries})=>{assert.equal(activeId,3);assert(diaries.some(diary=>diary.id===48));selections++;return choice;},
 menu(title,items){if(title.startsWith('Cards to send:'))return remaining?1:0;if(items[0]==='Add'){remaining--;return 0;}return 0;}};
engine=await AdventureEngine.load(module,peerHandlers,cartridge);assert.equal(await engine.loadProfile(3),0);
const unrelated=cartridge.read('adventure2'),own=cartridge.read('adventure3'),peer=cartridge.read('adventure48');
await engine.action(11,0);assert.equal(selections,1);
assert.notDeepEqual(cartridge.read('adventure3'),own);assert.notDeepEqual(cartridge.read('adventure48'),peer);
assert.deepEqual(cartridge.read('adventure2'),unrelated);
const paired=cartridge.export();remaining=1;storage.failNext=true;
await engine.action(11,0);assert.deepEqual(cartridge.export(),paired);
choice=null;remaining=1;await engine.action(11,0);assert.deepEqual(cartridge.export(),paired);
assert(!engine.setPeer(3));assert(!engine.setPeer(777));assert(engine.setPeer(48));
console.log('DIARIES WASM PASS: 49 canonical diaries including maximum-safe ID; large export/import; independent real-duel save/resume; byte-preserving legacy migration; CRC/owner validation; arbitrary async peer gift and atomic rollback');
