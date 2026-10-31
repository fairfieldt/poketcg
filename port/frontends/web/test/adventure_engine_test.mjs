/* Actual browser platform WASM: source intro, choices, story/presentation,
 * source cartridge profile bytes, two-peer transactions, and original routes.
 * Tutorial duel outcomes are supplied; duel-engine tests cover actual play. */
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';
import { checkClearancePoses } from './object_clearance_poses.mjs';
const here = path.dirname(fileURLToPath(import.meta.url)), web = path.resolve(here, '..');
const dist = path.join(web, 'dist'), isolated = path.join(web, 'build/adventure-engine-test');
await fs.mkdir(path.join(isolated, 'js/gen'), { recursive: true });
await fs.writeFile(path.join(isolated, 'package.json'), '{"type":"module"}\n');
for (const name of ['engine.js', 'adventure_engine.js']) await fs.copyFile(path.join(web, 'src/js', name), path.join(isolated, 'js', name));
await fs.copyFile(path.join(dist, 'js/gen/consts.js'), path.join(isolated, 'js/gen/consts.js'));
const { AdventureEngine, Cartridge } = await import(pathToFileURL(path.join(isolated, 'js/adventure_engine.js')));
const { C } = await import(pathToFileURL(path.join(isolated, 'js/engine.js')));
const bytes = await fs.readFile(path.join(dist, 'tcg.wasm'));
const collisionWorld = JSON.parse(await fs.readFile(path.resolve(web, '../../world3d/generated/world.json'), 'utf8'));
const module = await WebAssembly.compile(bytes);
class MemoryStorage {
    constructor() { this.values = new Map(); this.failNext = false; this.writes = 0; }
    getItem(key) { return this.values.get(key) || null; }
    setItem(key, value) { if (this.failNext) { this.failNext = false; throw new Error('simulated storage failure'); } this.values.set(key, value); this.writes++; }
}
const storage = new MemoryStorage(); let cartridge = new Cartridge(storage);
let engine, mode = 'intro', giftRemaining = 0, duels = [], messages = [], frames = [], frameCount = 0;
let pcStep=0,mailVisit=0,glossaryVisit=0,reports=[],starterChoice=0;
const handlers = {
    frame(view) {
        assert(view.map >= 0 && view.map < 34);
        assert(view.presentation && Number.isFinite(view.presentation.player.x));
        checkClearancePoses(collisionWorld, view);
        if (++frameCount > 20000) throw new Error('Story frame budget exceeded');
        frames.push(view);
        return Promise.resolve(); // Exercise actual Asyncify unwind/rewind.
    },
    message(title, text) { messages.push({ title, text }); return Promise.resolve(); },
    menu(title, items, initial, cancel) {
        if(mode==='pc'){
            if(title==='PC')return [1,2,3,4][pcStep++];
            if(title==='Read Mail')return mailVisit++? -1:0;
            if(title==='Received cards')return -1;
            if(title==='Glossary')return glossaryVisit++? -1:0;
            if(title==='Print')return items.length===4?2:0;
            if(title==='Save text to Controller Pak'||title==='Download report')return 0;
        }
        if (mode === 'gift') {
            if (title.startsWith('Cards to send:')) return giftRemaining ? 1 : 0;
            if (items[0] === 'Add') { giftRemaining--; return 0; }
        }
        if (mode === 'recipe') {
            if (title === 'Choose deck to send') return 0;
            if (title === 'Choose recipe slot') return 3;
            if (title === 'Recipe slot') return 0;
        }
        if (title === 'Battle Center') return 0;
        if (items[0] === 'Yes' && items[1] === 'No') return 0;
        if (items.includes('Charmander & Friends')) return starterChoice;
        if (items[0] === 'Normal duel' && items[1] === 'Practice') return 1;
        if (items.length === 8 && items.at(-1) === 'Exit') return 7;
        throw new Error(`Unexpected source menu: ${title} / ${items.join(' | ')} (initial${initial}, cancel${cancel})`);
    },
    report(name,text){reports.push({name,text});return Promise.resolve();},
    card(id) { assert(id >= 1 && id <= 228); return Promise.resolve(); },
    duel(setup, saved) {
        assert(!saved, 'fresh tutorial/link duel unexpectedly resumed');
        duels.push(setup);
        if (mode === 'intro') {
            assert.equal(setup.prizes, 2);
            assert.equal(setup.duelists[1].kind, 'ai');
            assert.equal(setup.duelists[1].deckId, C.SAMS_PRACTICE_DECK_ID);
            assert.equal(setup.duelists[1].name, 'Sam');
        } else if (mode === 'battle') {
            assert.equal(setup.duelists[0].kind, 'human'); assert.equal(setup.duelists[1].kind, 'human');
            assert.equal(setup.prizes, 2);
        } else throw new Error('Unexpected duel');
        engine.setDuelRng([37, 51, 73]);
        return Promise.resolve(0);
    },
};
function profile(number) {
    const data = cartridge.read(`adventure${number}`); assert(data, `Diary${number} missing`);
    assert.equal(new TextDecoder().decode(data.subarray(0, 8)), 'TCGADV02');
    const owSize = data[26] * 256 + data[27], gp = data.slice(48 + owSize);
    assert.equal(gp.length, 6082); assert.equal(new TextDecoder().decode(gp.subarray(0, 4)), 'GP01');
    const collection = gp.slice(4, 260), built = Array.from({ length: 4 }, (_, i) => gp.slice(260 + i * 86, 260 + (i + 1) * 86));
    const recipes = Array.from({ length: 60 }, (_, i) => gp.slice(260 + (4 + i) * 86, 260 + (5 + i) * 86));
    const total = [...collection].reduce((sum, n) => sum + (n === 128 ? 0 : n), 0) + built.reduce((sum, deck) => sum + [...deck.subarray(26)].filter(Boolean).length, 0);
    return { data, gp, collection, built, recipes, total, selected: gp[5764], rng: gp.slice(5765, 5768), name: new TextDecoder().decode(data.subarray(8, 24)).replace(/\0.*$/, '') };
}
engine = await AdventureEngine.load(module, handlers, cartridge);
assert.equal(await engine.start(1, { fresh: true, name: 'MARK', seed: 123 }), 0);
assert.equal(engine.name, 'MARK'); assert.equal(engine.view.totalCards, 90);
assert.equal(duels.length, 1); assert.equal(engine.view.map, C.MASON_LABORATORY);
assert(frames.some(view => view.map === 0 && view.overworldSelection === C.OWMAP_POKEMON_DOME));
assert(frames.some(view => view.map === 0 && view.overworldSelection === C.OWMAP_MASON_LABORATORY));
assert(frames.some(view => view.map === 1 && view.presentation.player.z < 26 && view.presentation.player.z > 8));
assert(frames.some(view => view.map === 1 && view.x === 10 && view.y === 14));
await fs.writeFile(path.join(isolated,'world-travel.json'),JSON.stringify(frames.find(view=>view.map===0&&view.presentation.world.x>5&&view.presentation.world.x<7),null,2));
assert.equal(profile(1).total, 90);
assert(engine.profile(1)); assert(!engine.profile(2));
// Install the shipped tutorial-complete diary without replacing either an
// existing diary or another diary's interrupted duel.
const seedFixture=JSON.parse(await fs.readFile(path.join(web,'src/saves/diary2-after-tutorial.json'),'utf8'));
const seedSave=new Uint8Array(Buffer.from(seedFixture.save,'base64'));
const seedStorage=new MemoryStorage(),seedCartridge=new Cartridge(seedStorage);
const firstDiary=cartridge.read('adventure1'),pendingBytes=new Uint8Array(1660).fill(37);
assert(seedCartridge.commit({adventure1:firstDiary,duel:pendingBytes,duelowner:new Uint8Array([1])}));
const seedEngine=await AdventureEngine.load(module,{},seedCartridge);
assert(seedEngine.installDiary(2,seedSave));assert(seedEngine.profile(2));
assert.deepEqual(seedCartridge.read('adventure1'),firstDiary);
assert.deepEqual(seedCartridge.read('duel'),pendingBytes);
assert.deepEqual(seedCartridge.read('duelowner'),new Uint8Array([1]));
const withSeed=seedCartridge.export();
assert(!seedEngine.installDiary(2,seedSave));assert.deepEqual(seedCartridge.export(),withSeed);
assert(seedCartridge.commit({adventure2:null}));
const corruptSeed=seedSave.slice();corruptSeed[corruptSeed.length-1]^=1;
const withoutSeed=seedCartridge.export();
assert(!seedEngine.installDiary(2,corruptSeed));assert.deepEqual(seedCartridge.export(),withoutSeed);
seedStorage.failNext=true;
assert(!seedEngine.installDiary(2,seedSave));assert.deepEqual(seedCartridge.export(),withoutSeed);
assert(seedEngine.installDiary(2,seedSave));
assert(seedCartridge.commit({duel:null,duelowner:null}));
const seedReload=await AdventureEngine.load(module,{
    message(){throw new Error('Tutorial replayed from Diary2 checkpoint');},
    duel(){throw new Error('Practice duel replayed from Diary2 checkpoint');},
},new Cartridge(seedStorage));
assert.equal(await seedReload.start(2),0);
assert.equal(seedReload.x.web_adv_event(C.EVENT_MASON_LAB_STATE),C.MASON_LAB_RECEIVED_STARTER_DECK);
assert.equal(seedReload.view.totalCards,90);assert.equal(seedReload.view.map,C.MASON_LABORATORY);
starterChoice=1;await engine.start(2, { fresh: true, name: 'RONALD', seed: 456 });starterChoice=0;
assert.equal(engine.name, 'RONALD'); assert.equal(profile(2).total, 90); assert.equal(profile(1).total, 90);
assert(engine.profile(1) && engine.profile(2));
const cleanCartridge=cartridge.export();
assert(cartridge.commit({'duelowner-1':new Uint8Array([1]),'duel-1':new Uint8Array(1660)}));
const otherPending=cartridge.read('duel-1');
assert.equal(await engine.start(2),0,'An independent diary must remain playable');
assert.deepEqual(cartridge.read('duel-1'),otherPending);
assert(cartridge.import(cleanCartridge));
await fs.writeFile(path.join(isolated,'cartridge.json'),cartridge.export());
const exported = cartridge.export();
const restoredStorage = new MemoryStorage(), restored = new Cartridge(restoredStorage);
assert(restored.import(exported)); assert.deepEqual(restored.read('adventure1'), cartridge.read('adventure1'));
assert(!restored.import(new TextEncoder().encode('{"version":2,"files":{}}')));
cartridge = new Cartridge(storage);
engine = await AdventureEngine.load(module, handlers, cartridge);
await engine.start(1); assert.equal(engine.name, 'MARK'); assert.equal(engine.view.totalCards, 90);
const beforeImport=cartridge.export(),invalidCartridge=new Cartridge(null);
assert(invalidCartridge.import(exported));
const invalidDiary=invalidCartridge.read('adventure1');invalidDiary[invalidDiary.length-1]^=1;
assert(invalidCartridge.commit({adventure1:invalidDiary}));
assert(!engine.importCartridge(invalidCartridge.export()));assert.deepEqual(cartridge.export(),beforeImport);
const invalidPending=new Cartridge(null);assert(invalidPending.import(exported));
assert(invalidPending.commit({duelowner:new Uint8Array([1]),duel:new Uint8Array(1660)}));
assert(!engine.importCartridge(invalidPending.export()));assert.deepEqual(cartridge.export(),beforeImport);

storage.failNext=true;assert(!engine.importCartridge(exported));assert.deepEqual(cartridge.export(),beforeImport);

await engine.action(7); assert.equal(engine.view.totalCards, 91);
assert.equal(profile(1).total, 91); assert.equal(profile(2).total, 91);
const pop1 = profile(1).gp, pop2 = profile(2).gp;
await engine.action(7); assert.equal(engine.view.totalCards, 91);
assert.deepEqual(profile(1).gp, pop1); assert.deepEqual(profile(2).gp, pop2);
assert(messages.some(message => /already connected/i.test(message.text)));
const freeId = profile(1).collection.findIndex((n, id) => id > 0 && n > 0 && n < 128);
assert(freeId > 0); const ownBefore = profile(1).collection[freeId], peerBefore = profile(2).collection[freeId];
mode = 'gift'; giftRemaining = 1;
await engine.action(11, 0);
assert.equal(profile(1).total, 90); assert.equal(profile(2).total, 92);
assert.equal(profile(1).collection[freeId], ownBefore - 1);
assert.equal(profile(2).collection[freeId], peerBefore === 128 ? 1 : peerBefore + 1);
mode = 'recipe'; const recipe = profile(1).recipes[0];
await engine.action(11, 2);
assert.deepEqual(profile(2).recipes[3], recipe);
assert.equal(profile(1).total, 90); assert.equal(profile(2).total, 92);
const beforeFailure = [profile(1).gp, profile(2).gp];
mode = 'gift'; giftRemaining = 1; storage.failNext = true;
await engine.action(11, 0);
assert.deepEqual(profile(1).gp, beforeFailure[0]); assert.deepEqual(profile(2).gp, beforeFailure[1]);
// Move each existing complete deck into a different physical slot in a valid
// imported canonical fixture. Ownership and card totals remain unchanged.
function crc32(data){let crc=0xffffffff;for(const value of data){crc^=value;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (~crc)>>>0;}
const relocated={};
for(const [number,slot] of [[1,1],[2,2]]){
 const state=profile(number),data=state.data.slice(),offset=48+data[26]*256+data[27],gp=data.subarray(offset);
 gp.copyWithin(260+slot*86,260,346);gp.fill(0,260,346);gp[5764]=slot;
 new DataView(gp.buffer,gp.byteOffset,gp.byteLength).setUint32(gp.length-4,crc32(gp.subarray(0,gp.length-4)),true);
 relocated['adventure'+number]=data;
}
assert(cartridge.commit(relocated));await engine.start(1);
assert.equal(engine.view.selectedDeck,1);assert.equal(profile(2).selected,2);
mode = 'battle'; const beforeDuels = duels.length;
await engine.action(11, 4);
assert.equal(duels.length, beforeDuels + 1);
assert.deepEqual(duels.at(-1).duelists.map(player => player.name), ['MARK', 'RONALD']);
assert.deepEqual(duels.at(-1).duelists[0].cards, [...profile(1).built[profile(1).selected].subarray(26)]);
assert.deepEqual(duels.at(-1).duelists[1].cards, [...profile(2).built[profile(2).selected].subarray(26)]);
assert.deepEqual(profile(1).rng, profile(2).rng);
assert.equal(cartridge.read('duel'), null);
mode='pc';pcStep=mailVisit=glossaryVisit=0;
const beforeMail=engine.view.totalCards;await engine.action(8);
assert.equal(engine.view.totalCards,beforeMail+10);assert.equal(reports.length,1);
assert(reports[0].text.includes('MARK')&&reports[0].text.includes('Card Album:'));
assert(messages.some(message=>message.title==='Print preview'&&message.text.includes('Cards:')));
const afterMail=engine.view.totalCards;pcStep=mailVisit=glossaryVisit=0;await engine.action(8);
assert.equal(engine.view.totalCards,afterMail);assert.equal(reports.length,2);
// Walk the actual collision grid to the laboratory's original island warp.
mode='navigation';
const world=JSON.parse(await fs.readFile(path.resolve(web,'../../world3d/generated/world.json'),'utf8'));
function routeTo(x,y){
 const state=engine.view,map=world.maps[state.map],blocked=new Set(state.npcs.filter(n=>n.visible).map(n=>n.x+','+n.y));
 const queue=[[state.x,state.y,[]]],seen=new Set([state.x+','+state.y]);
 for(let at=0;at<queue.length;at++){
  const [px,py,steps]=queue[at];if(px===x&&py===y)return steps;
  for(const [dir,dx,dy] of [[0,0,-2],[1,2,0],[2,0,2],[3,-2,0]]){
   const nx=px+dx,ny=py+dy,key=nx+','+ny;
   if(nx<0||ny<0||nx>=map.width||ny>=map.height||seen.has(key)||blocked.has(key)||(map.permissions[ny>>1][nx>>1]&128))continue;
   seen.add(key);queue.push([nx,ny,[...steps,dir]]);
  }
 }
 throw new Error('No source-grid route to '+x+','+y);
}
for(const direction of routeTo(14,28))await engine.action(direction);
assert.equal(engine.view.map,0);assert.equal(engine.view.overworldSelection,C.OWMAP_MASON_LABORATORY);
const atIsland=frames.length;await engine.action(0);assert.equal(engine.view.overworldSelection,C.OWMAP_LIGHTNING_CLUB);
await engine.action(4);assert.equal(engine.view.map,C.LIGHTNING_CLUB_ENTRANCE);
assert(frames.slice(atIsland).some(view=>view.map===0&&view.presentation.world.x>2.5&&view.presentation.world.x<5.5));
// Commit an explicit Diary checkpoint before a fresh browser instance.
await engine.action(6);
cartridge=new Cartridge(storage);engine=await AdventureEngine.load(module,handlers,cartridge);await engine.start(1);
assert.equal(engine.view.map,C.LIGHTNING_CLUB_ENTRANCE);assert.equal(engine.view.totalCards,afterMail);
console.log(`ADVENTURE WASM PASS: actual source intro+practice setup, ${frameCount} animated frames, 90-card starters, two canonical diaries/reload/export/import and independent pending-duel namespaces, paired Card Pop/history, card/recipe gifts, interrupted pair rollback, two-human selected-deck1/2 Battle Center, one-time mail/glossary/print export, collision-grid exit and animated island navigation`);
