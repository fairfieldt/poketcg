import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {World3D,parseL643} from '../src/js/world3d.js';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const generated=path.join(root,'port/world3d/generated');
const metadata=JSON.parse(await fs.readFile(path.join(generated,'world.json')));
const world=Object.create(World3D.prototype);
Object.assign(world,{metadata,modelPaths:new Map(metadata.individual_characters.map(c=>[c.model,c.file])),
 playerModel:metadata.player_model,generation:0,loadedMap:-1,poses:new Map(),
 _scene:async file=>({path:file}),_syncPoses:()=>{},draw:()=>{}});
let cases=0;
for(const override of metadata.map_actor_scenes){
 for(let map=0;map<34;map++){
  assert.equal(world._modelPath(override.model,map),map===33?override.scene:world.modelPaths.get(override.model));cases++;
 }
 const mesh=parseL643(await fs.readFile(path.join(generated,override.scene.replace('rom:/world/',''))));
 assert(mesh.meshes.every(batch=>batch.model===override.model));
}
const npcs=metadata.maps[33].npcs.map(n=>({id:n.npc,x:n.x,y:n.y,direction:n.dir,visible:true}));
for(const map of [33,32,33]){
 await world.update({map,npcs,mapPatch:[]});
 for(const override of metadata.map_actor_scenes)
  assert.equal(world.actorScenes.get(override.model).path,map===33?override.scene:world.modelPaths.get(override.model));
 cases++;
}
await world.update({map:33,npcs:npcs.map(n=>({...n,visible:n.id!==104})),mapPatch:[]});
assert(!world.actorScenes.has(metadata.npc_models[104]));
assert(world.actorScenes.has(metadata.npc_models[105]));cases++;
assert.equal(metadata.npc_models[106],178);assert.equal(metadata.npc_models[109],179);
console.log(`HALL CARD MODELS PASS: ${cases} map/path/visibility/re-entry assertions; canonical sparks unchanged`);
