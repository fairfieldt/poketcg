import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { parseL643 } from '../src/js/world3d.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../../..');
const world = path.join(root, 'port/world3d/generated');
const metadata = JSON.parse(fs.readFileSync(path.join(world, 'world.json')));
const files = [path.join(world, 'actors.l64'), ...fs.readdirSync(path.join(world, 'maps')).filter(name => name.endsWith('.l64')).map(name => path.join(world, 'maps', name))];
let vertices = 0, triangles = 0, repeating = 0;
for (const file of files) {
    const scene = parseL643(fs.readFileSync(file));
    for (const mesh of scene.meshes) {
        vertices += mesh.vertexCount; triangles += mesh.indexCount / 3;
        if (mesh.texture >= 0 && (mesh.repeatS || mesh.repeatT)) repeating++;
        const raw = new DataView(mesh.vertices);
        for (let i = 0; i < mesh.vertexCount; i++) for (let j = 0; j < 5; j++) assert(Number.isFinite(raw.getFloat32(i * 24 + j * 4, true)));
    }
}
assert.equal(metadata.maps.length, 34);
for (const map of metadata.maps) assert(fs.existsSync(path.join(world, map.scene.replace('rom:/world/', ''))));
assert.equal(metadata.player_model, 7);
assert.equal(metadata.npc_models[1], 9); // Mason remains distinct from Mark/Ronald.
assert.equal(metadata.npc_models[2], 8);
assert(metadata.individual_characters.length >= 49);
const mark = parseL643(fs.readFileSync(path.join(world, 'maps/actor_mark.l64')));
assert(mark.meshes.every(mesh => mesh.model === 7));
assert.deepEqual(new Set(mark.meshes.filter(mesh => mesh.part).map(mesh => mesh.part)), new Set([1, 2, 3, 4]));
const bytes = fs.readFileSync(path.join(world, 'maps/actor_mark.l64'));
assert.throws(() => parseL643(bytes.subarray(0, bytes.length - 1)), /Truncated|Trailing/);
const broken = Buffer.from(bytes); broken[0] = 0;
assert.throws(() => parseL643(broken), /Unsupported/);
const wrongIndex = Buffer.from(bytes), view = new DataView(wrongIndex.buffer, wrongIndex.byteOffset, wrongIndex.byteLength);
const header = 8 + view.getUint16(6) * 32, index = header + 28 + view.getUint32(header + 8) * 24;
view.setUint16(index, 65535);
assert.throws(() => parseL643(wrongIndex), /index out of range/);
const tiled = Buffer.from(bytes), tileView = new DataView(tiled.buffer, tiled.byteOffset, tiled.byteLength);
tileView.setFloat32(header + 28 + 12, 2.5); tileView.setFloat32(header + 28 + 16, -.5);
const tiledMesh = parseL643(tiled).meshes[0];
assert(tiledMesh.repeatS && tiledMesh.repeatT);
assert(mark.meshes.every(mesh => !mesh.repeatS && !mesh.repeatT));
assert(repeating > 0, 'Merged native materials must retain repeating UVs');
console.log(`WORLD3D WEB PASS: ${files.length} scenes, ${vertices} vertices, ${triangles} triangles; all 34 map paths, detailed character mappings/rigs, ${repeating} repeating materials, malformed input rejection`);
