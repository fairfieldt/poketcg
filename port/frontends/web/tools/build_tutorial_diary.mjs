/* Produce a canonical post-tutorial diary by executing the real source story. */
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const work = path.join(web, 'build/tutorial-diary');
await fs.mkdir(path.join(work, 'js/gen'), { recursive: true });
await fs.writeFile(path.join(work, 'package.json'), '{"type":"module"}\n');
for (const name of ['engine.js', 'adventure_engine.js'])
    await fs.copyFile(path.join(web, 'src/js', name), path.join(work, 'js', name));
await fs.copyFile(path.join(web, 'dist/js/gen/consts.js'), path.join(work, 'js/gen/consts.js'));
const { AdventureEngine, Cartridge } = await import(pathToFileURL(path.join(work, 'js/adventure_engine.js')));
const { C } = await import(pathToFileURL(path.join(work, 'js/engine.js')));
const module = await WebAssembly.compile(await fs.readFile(path.join(web, 'dist/tcg.wasm')));
const cartridge = new Cartridge(null);
let engine, duels = 0;
const handlers = {
    frame() {}, message() {}, card() {},
    menu(title, items) {
        if (items[0] === 'Yes' && items[1] === 'No') return 0;
        if (items.includes('Charmander & Friends')) return items.indexOf('Charmander & Friends');
        if (items[0] === 'Normal duel' && items[1] === 'Practice') return 1;
        if (items.length === 8 && items.at(-1) === 'Exit') return 7;
        throw new Error(`Unexpected tutorial choice: ${title}: ${items.join(', ')}`);
    },
    duel(setup, pending) {
        assert(!pending);
        assert.equal(setup.duelists[1].deckId, C.SAMS_PRACTICE_DECK_ID);
        assert.equal(++duels, 1);
        engine.setDuelRng([37, 51, 73]);
        return 0;
    },
};
const originalNow = Date.now;
Date.now = () => 1700000000000;
try {
    engine = await AdventureEngine.load(module, handlers, cartridge);
    assert.equal(await engine.start(2, { fresh: true, name: 'MARK', seed: 123 }), 0);
    assert.equal(duels, 1);
    assert.equal(engine.x.web_adv_event(C.EVENT_MASON_LAB_STATE), C.MASON_LAB_RECEIVED_STARTER_DECK);
    assert.equal(engine.view.totalCards, 90);
    assert.equal(engine.view.map, C.MASON_LABORATORY);
    assert(!cartridge.read('duel-2') && !cartridge.read('duelowner-2'));
    assert(!cartridge.read('adventure1'));
    const save = cartridge.read('adventure2');
    const reloaded = await AdventureEngine.load(module, {
        frame() {},
        message() { throw new Error('Completed tutorial replayed on reload'); },
        duel() { throw new Error('Completed practice duel replayed on reload'); },
    }, cartridge);
    assert.equal(await reloaded.start(2), 0);
    assert.equal(reloaded.view.totalCards, 90);
    const output = path.join(web, 'src/saves/diary2-after-tutorial.json');
    await fs.mkdir(path.dirname(output), { recursive: true });
    await fs.writeFile(output, JSON.stringify({ version: 1, profile: 2, save: Buffer.from(save).toString('base64') }) + '\n');
    console.log(`TUTORIAL DIARY PASS: canonical ${save.length}-byte Diary 2, starter deck, no replay on reload`);
} finally { Date.now = originalNow; }
