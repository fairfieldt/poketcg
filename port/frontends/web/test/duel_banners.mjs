// Actual showBanner hook, source CSS, seven source labels, two display scales.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
const require=createRequire(import.meta.url);
const web=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const {chromium}=require(path.join(web,'build/world-test/node_modules/playwright'));
const out=path.join(web,'build/duel-banners');fs.mkdirSync(out,{recursive:true});
const labels=["Player's turn","Opponent's turn",'Between turns','Decision','Bench Pokémon','Arena Pokémon','Coin toss'];
const html=`<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,"><link rel="stylesheet" href="/css/style.css">
<style>html,body{margin:0;width:100%;height:100%;overflow:hidden}main{position:relative;width:100%;height:100%;background:#173d38}
main::before,main::after{content:"";position:absolute;left:10%;right:10%;height:17%;border:1px solid #6b8c91;background:#152d39}
main::before{top:10%}main::after{bottom:10%}</style><main><div class="banner hidden"></div></main>
<script type="module">
import {DuelUI} from '/js/duel_ui.js';
const ui=Object.create(DuelUI.prototype);ui.$banner=document.querySelector('.banner');
window.renderBanner=(id,unit)=>{document.documentElement.style.setProperty('--u',unit);ui.showBanner(id);};
window.ready=true;
</script>`;
const server=http.createServer((req,res)=>{
	const pathname=decodeURIComponent(new URL(req.url,'http://127.0.0.1').pathname);
	if(pathname==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);return;}
	const relative=pathname.replace(/^\//,'');
	let file=path.join(web,'src',relative);
	if(!file.startsWith(path.join(web,'src')+path.sep)){res.writeHead(403);res.end();return;}
	if(!fs.existsSync(file))file=path.join(web,'dist',relative);
	if(!fs.existsSync(file)){res.writeHead(404);res.end();return;}
	res.setHeader('Content-Type',({'.js':'application/javascript','.css':'text/css','.png':'image/png'})[path.extname(file)]||'application/octet-stream');
	res.end(fs.readFileSync(file));
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const url=`http://127.0.0.1:${server.address().port}/`;
const errors=[];const results=[];
const browser=await chromium.launch({channel:'chrome',headless:true});
try {
	for(const size of [{name:'native',width:320,height:240,unit:1,scale:1},{name:'large',width:1280,height:960,unit:4,scale:2}]) {
		const page=await browser.newPage({viewport:{width:size.width,height:size.height},deviceScaleFactor:size.scale,reducedMotion:'reduce'});
		page.on('pageerror',error=>errors.push(error.message));
		page.on('requestfailed',request=>errors.push(request.url()));
		await page.goto(url);await page.waitForFunction(()=>window.ready);
		for(let id=0;id<labels.length;id++) {
			await page.evaluate(([id,unit])=>window.renderBanner(id,unit),[id,size.unit]);
			const plaque=page.locator('.duel-banner');
			assert.equal(await plaque.textContent(),labels[id]);
			assert.equal(await plaque.getAttribute('role'),'status');
			assert.equal(await plaque.getAttribute('aria-live'),'polite');
			assert.equal(await plaque.getAttribute('aria-atomic'),'true');
			assert((await plaque.ariaSnapshot()).includes(labels[id]));
			const result=await plaque.evaluate(node=>{
				const label=node.querySelector('.duel-banner-label'),box=node.getBoundingClientRect(),text=label.getBoundingClientRect();
				return {box:{x:box.x,y:box.y,width:box.width,height:box.height},text:{x:text.x,y:text.y,width:text.width,height:text.height},
					background:getComputedStyle(node).backgroundImage,imageRendering:getComputedStyle(node).imageRendering,
					font:getComputedStyle(label).fontFamily,fontSize:getComputedStyle(label).fontSize,animation:getComputedStyle(node).animationName};
			});
			assert(!result.background.includes('box_messages.png'));
			assert.equal(result.imageRendering,'auto');assert.equal(result.animation,'none');
			assert(result.font.includes('Trebuchet'));
			assert(result.text.x>result.box.x+8&&result.text.x+result.text.width<result.box.x+result.box.width-8,`Clipped label ${id}`);
			assert(result.text.y>result.box.y&&result.text.y+result.text.height<result.box.y+result.box.height);
			const file=`${size.name}-${id}.png`;await page.screenshot({path:path.join(out,file)});
			results.push({size:size.name,deviceScaleFactor:size.scale,id,label:labels[id],file,...result});
		}
		for(const invalid of [-1,7,1.5,'0']) {
			await page.evaluate(id=>window.renderBanner(id,1),invalid);
			assert.equal(await page.locator('.duel-banner').count(),0);
			assert(await page.locator('.banner').evaluate(node=>node.classList.contains('hidden')));
		}
		await page.emulateMedia({reducedMotion:'no-preference'});
		await page.evaluate(()=>window.renderBanner(0,1));
		assert(await page.locator('.duel-banner').evaluate(node=>node.getAnimations().every(animation=>
			animation.effect.getKeyframes().every(frame=>!frame.transform||frame.transform==='none'))),'Plaque animation resizes rasterized text');
		await page.close();
	}
	assert.deepEqual(errors,[]);
	fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({passed:true,labels,results,errors},null,2)+'\n');
	console.log('DUEL BANNERS PASS: actual showBanner hook; all7 labels/accessibility; 320x240 and 1280x960 Retina; no clipping, sprite upscaling or browser errors');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
