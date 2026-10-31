// Check the actual WASM link, including its frontend text override.
import fs from 'node:fs/promises';
import assert from 'node:assert/strict';
import { Engine, T } from '../dist/js/engine.js';

const engine = await Engine.load(await fs.readFile(new URL('../dist/tcg.wasm', import.meta.url)));
for (const id of [T.SamTurn4Instr2PracticeDuelText, T.SamTurn4DrMason2PracticeDuelText]) {
    assert.match(engine.text(id), /Press C/);
    assert.doesNotMatch(engine.text(id), /SELECT|press Z|click/);
}
assert.match(engine.text(T.UsefulButtonsBookText), /choose Check/);
assert.doesNotMatch(engine.text(T.UsefulButtonsBookText), /B \+ Down|SELECT|START/);
assert.match(engine.text(T.SelectStaryuPracticeDuelText), /^Select Staryu/);
console.log('WASM CONTROL TEXT PASS: actual web build uses C/Check, with ordinary Select preserved');
