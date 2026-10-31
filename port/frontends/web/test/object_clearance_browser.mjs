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
const output = path.join(web, 'build/object-clearance-browser');
fs.mkdirSync(output, { recursive: true });
const html = `<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,"><style>html,body{margin:0;background:#080b18}canvas{display:block;width:960px;height:720px}</style><canvas></canvas>
<script type="module">
import {World3D} from '/port/frontends/web/src/js/world3d.js';
window.world=await new World3D(document.querySelector('canvas'),{baseURL:'/port/world3d/generated/',maxPixelRatio:1}).load();
window.render=async ({map:id,player:position,camera})=>{
 const map=world.metadata.maps[id];
 const npcs=map.npcs.filter(n=>n.npc!==255&&n.x>=0&&n.y>=0&&n.x<map.width&&n.y<map.height).map(n=>({id:n.npc,x:n.x,y:n.y,direction:n.dir,visible:true,animation:n.npc===34?46:0}));
 const player={x:position?.[0]??map.width/2,z:position?.[1]??map.height-6,heading:0,gait:0,bob:0,lean:0};
 const view={map:id,x:player.x,y:player.z,direction:2,overworldSelection:1,npcs,mapPatch:Array(11).fill(0),presentation:{player,world:{x:2.5,z:14,heading:0,gait:0},cursor:{x:2.5,z:14,heading:0,gait:0},npcs:npcs.map(n=>({id:n.id,pose:{x:n.x,z:n.y,heading:180-n.direction*90,gait:0,bob:0,lean:0,floor:id===33&&n.id>=104&&n.id<=109?2.2:n.id===34?.66:0}})),camera:camera||{eye:[map.width/2,Math.max(24,map.height*1.1),map.height+7],target:[map.width/2,0,map.height/2],fov:55}}};
 await world.update(view);world.draw(view,0);
 const gl=world.gl,pixels=new Uint8Array(960*720*4);gl.readPixels(0,0,960,720,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
 const colors=new Set();for(let i=0;i<pixels.length;i+=16)colors.add(pixels.slice(i,i+4).join(','));
 return {map:id,error:gl.getError(),colors:colors.size,paths:world.roomScenes.map(scene=>scene.path)};
};
window.ready=true;
</script>`;
fs.writeFileSync(path.join(output, 'viewer.html'), html);
const port = Number(process.env.OBJECT_CLEARANCE_PORT || 18176);
const server = spawn('python3', ['-m', 'http.server', String(port), '--bind', '127.0.0.1', '--directory', root], { stdio: 'ignore' });
const errors = [];
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
    await page.goto(`http://127.0.0.1:${port}/port/frontends/web/build/object-clearance-browser/viewer.html`);
    await page.waitForFunction(() => window.ready, { timeout: 60000 });
    const results = [];
    for (let map = 0; map < 34; map++) {
        const result = await page.evaluate(spec => window.render(spec), { map });
        assert.equal(result.error, 0, `WebGL error map${map}`);
        assert(result.colors > 12, `Blank map${map}`);
        results.push(result);
        await page.screenshot({ path: path.join(output, `map-${String(map).padStart(2, '0')}.png`) });
    }
    const cases = [
        {name:'lab-technician',map:1,player:[6,6],camera:{eye:[8,13,19],target:[8,0,5],fov:55}},
        {name:'lobby-counter',map:5,player:[6,8],camera:{eye:[13,15,23],target:[13,0,7],fov:58}},
        {name:'lobby-shelves',map:5,player:[22,4],camera:{eye:[20,11,16],target:[21,0,4],fov:58}},
        {name:'lobby-table',map:5,player:[8,18],camera:{eye:[10,10,26],target:[10,0,15],fov:58}},
        {name:'water-promenade',map:12,player:[10,16],camera:{eye:[14,21,30],target:[13,0,12],fov:58}},
        {name:'lightning-pillars',map:15,player:[10,20],camera:{eye:[14,15,34],target:[14,0,17],fov:58}},
        {name:'lightning-shrine',map:15,player:[10,4],camera:{eye:[14,13,18],target:[14,0,4],fov:58}},
        {name:'lightning-entrance',map:15,player:[14,28],camera:{eye:[14,13,39],target:[14,0,25],fov:58}},
        {name:'hall-altar',map:33,player:[10,14],camera:{eye:[12,15,27],target:[12,0,10],fov:58}},
    ];
    for (const {name,...spec} of cases) {
        const result = await page.evaluate(spec => window.render(spec), spec);
        assert.equal(result.error, 0, name);
        await page.screenshot({ path: path.join(output, `${name}.png`) });
        results.push({ name, ...result });
    }
    assert.deepEqual(errors, []);
    fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify({passed:true,results,errors},null,2));
    console.log('OBJECT CLEARANCE BROWSER PASS: all34 maps and9 close views, no WebGL/page/request errors');
} finally {
    if (browser) await browser.close();
    server.kill();
}
