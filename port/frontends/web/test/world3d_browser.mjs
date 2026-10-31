import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import assert from 'node:assert/strict';
const require = createRequire(import.meta.url);
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../../..');
const web = path.resolve(here, '..');
const { chromium } = require(path.join(web, 'build/world-test/node_modules/playwright'));
const output = path.join(web, 'build/world-test');
fs.mkdirSync(output, { recursive: true });
const html = `<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,"><style>html,body{margin:0;background:#080b18}canvas{display:block;width:960px;height:720px}</style><canvas></canvas>
<script type="module">
import {World3D} from '/port/frontends/web/src/js/world3d.js';
const world=window.world=await new World3D(document.querySelector('canvas'),{baseURL:'/port/world3d/generated/',maxPixelRatio:1}).load();
window.renderMap=async id=>{
 const map=world.metadata.maps[id];
 const npcs=map.npcs.filter(n=>n.npc!==255&&n.x<map.width&&n.y<map.height).map(n=>({id:n.npc,x:n.x,y:n.y,direction:n.dir,visible:true,animation:n.npc===34?46:0}));
 const player={x:map.width/2,z:map.height-4,heading:0,gait:0,bob:0,lean:0};
 window.view={map:id,x:player.x,y:player.z,direction:2,overworldSelection:1,npcs,mapPatch:Array(11).fill(0),presentation:{player,world:{x:2.5,z:14,heading:0,gait:0},cursor:{x:2.5,z:14,heading:0,gait:0},npcs:npcs.map(n=>({id:n.id,pose:{x:n.x,z:n.y,heading:180-n.direction*90,gait:0,bob:0,lean:0}})),camera:map.camera}};
 window.mode='world';await world.update(window.view);world.draw(window.view);
 const gl=world.gl,pixels=new Uint8Array(960*720*4);gl.readPixels(0,0,960,720,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
 const colors=new Set();for(let i=0;i<pixels.length;i+=16)colors.add(pixels.slice(i,i+4).join(','));
 return {id,colors:colors.size,error:gl.getError(),scenes:world.roomScenes.length,actors:world.actorScenes.size};
};
window.renderPatch=async(id,patch)=>{await window.renderMap(id);window.view.mapPatch=patch;await world.update(window.view);world.draw(window.view);return {paths:world.roomScenes.map(scene=>scene.path),error:world.gl.getError()};};
window.renderPortrait=async()=>{await world.portrait();window.mode='portrait';world.drawPortrait();};
function draw(time){if(window.mode==='portrait')world.drawPortrait(time);else if(window.view)world.draw(window.view,time);requestAnimationFrame(draw)}requestAnimationFrame(draw);
window.ready=true;
</script>`;
fs.writeFileSync(path.join(output, 'viewer.html'), html);
const port = Number(process.env.WORLD_TEST_PORT || 18173), errors = [];
const serverLog=fs.openSync(path.join(output,'http.log'),'w');
const server = spawn('python3', ['-m', 'http.server', String(port), '--bind', '127.0.0.1', '--directory', root], { stdio: ['ignore','ignore',serverLog] });
fs.closeSync(serverLog);
let browser;
try {
    for (let tries = 0;; tries++) {
        try { await fetch(`http://127.0.0.1:${port}/`); break; }
        catch (error) { if (tries > 100) throw error; await new Promise(resolve => setTimeout(resolve, 50)); }
    }
    browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
    const page = await browser.newPage({ viewport: { width: 960, height: 720 }, deviceScaleFactor: 1 });
    page.on('pageerror', error => errors.push(error.message));
    page.on('requestfailed', request => errors.push(request.url()+': '+request.failure()?.errorText));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    await page.goto(`http://127.0.0.1:${port}/port/frontends/web/build/world-test/viewer.html`);
    await page.waitForFunction(() => window.ready, { timeout: 60000 });
    const results = [];
    for (let id = 0; id < 34; id++) {
        const result = await page.evaluate(id => window.renderMap(id), id);
        assert.equal(result.error, 0, `WebGL error at map${id}`); assert(result.colors > 12, `Blank map${id}`);
        results.push(result);
        if ([0, 1, 12, 15, 32].includes(id)) await page.screenshot({ path: path.join(output, `map-${String(id).padStart(2, '0')}.png`) });
    }
    const patchCases=[[1,10,'challenge_machine'],[31,0,'door_open'],[32,1,'door_open']];
    for(const [map,event,name] of patchCases){const patch=Array(11).fill(0);patch[event]=1;const state=await page.evaluate(([map,patch])=>window.renderPatch(map,patch),[map,patch]);assert.equal(state.error,0);assert(state.paths[0].includes(name));}
    const machines=Array.from({length:11},(_,i)=>i>=2&&i%2===0?1:0);
    const state=await page.evaluate(patch=>window.renderPatch(2,patch),machines);assert.equal(state.error,0);assert.equal(state.paths.length,9);
    for(let i=2;i<=9;i++)assert(state.paths.some(path=>path.includes('deck_machine_'+i+'_'+(machines[i]?'on':'off'))));
    const travelFile=path.join(web,'build/adventure-engine-test/world-travel.json');
    if(fs.existsSync(travelFile)) {
        const travel=JSON.parse(fs.readFileSync(travelFile));
        await page.evaluate(async view=>{window.mode='world';window.view=view;await world.update(view);world.draw(view);},travel);
        await page.screenshot({path:path.join(output,'world-travel.png')});
    }
    await page.evaluate(() => window.renderPortrait());
    await page.screenshot({ path: path.join(output, 'naming-portrait.png') });
    assert.deepEqual(errors.filter(error => !error.includes('favicon.ico')), []);
    fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify({ passed: true, maps: results, errors }, null, 2));
    console.log('WORLD3D BROWSER PASS: all34 maps rendered in WebGL, native character aliases/pose/rigs, source door/Challenge patches and all8 independent deck panels, noGL/page errors, Mark naming portrait');
} catch(error) {console.error('Browser errors:',errors);console.error(fs.readFileSync(path.join(output,'http.log'),'utf8').slice(-3000));throw error;}
finally { if (browser) await browser.close(); server.kill(); }
