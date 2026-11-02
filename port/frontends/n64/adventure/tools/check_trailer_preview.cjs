const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../../../..'),version=Number(process.argv[2]||5);
const {chromium}=require(path.join(root,'port/frontends/web/build/world-test/node_modules/playwright'));
(async()=>{
 const base='http://ms.tdf.sh:8091/reviews/trailer/';
 const out=path.join(root,`port/reviews/trailer/v${version}`);
 const expected=JSON.parse(fs.readFileSync(path.join(out,'validation.json')));
 const bytes=Buffer.from(await (await fetch(base+'poketcg64-trailer.mp4')).arrayBuffer());
 assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),expected.video_sha256);
 for(const [range,start,end] of [['bytes=100-199',100,200],['bytes=-10',bytes.length-10,bytes.length]]){
  const response=await fetch(base+'poketcg64-trailer.mp4',{headers:{Range:range}});
  assert.equal(response.status,206);assert.deepEqual(Buffer.from(await response.arrayBuffer()),bytes.subarray(start,end));
 }
 assert.equal((await fetch(base+'poketcg64-trailer.mp4',{headers:{Range:`bytes=${bytes.length}-`}})).status,416);
 const browser=await chromium.launch({channel:'chrome',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1600,height:850}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'compare.html');
  await page.waitForFunction(()=>[...document.querySelectorAll('video')].every(v=>v.readyState>=2&&Number.isFinite(v.duration)));
  const sources=await page.locator('video').evaluateAll(vs=>vs.map(v=>v.getAttribute('src')));
  assert.deepEqual(sources,[`v${version-1}/poketcg64-trailer.mp4`,'poketcg64-trailer-no-crt.mp4']);
  await page.locator('#seek').evaluate(el=>{el.value=10;el.dispatchEvent(new Event('input',{bubbles:true}))});
  await page.waitForFunction(()=>[...document.querySelectorAll('video')].every(v=>!v.seeking&&Math.abs(v.currentTime-10)<.1));
  await page.getByRole('button',{name:'Play',exact:true}).click();
  await page.waitForFunction(()=>[...document.querySelectorAll('video')].every(v=>v.currentTime>10.5));
  await page.getByRole('button',{name:'Pause',exact:true}).click();
  if(version>=6){
   const tail=await page.locator('#clean').evaluate(v=>v.duration-2);
   await page.locator('#seek').evaluate((el,t)=>{el.value=t;el.dispatchEvent(new Event('input',{bubbles:true}))},tail);
   await page.waitForFunction(t=>!document.querySelector('#clean').seeking&&Math.abs(document.querySelector('#clean').currentTime-t)<.1,tail);
   await page.getByRole('button',{name:'Play',exact:true}).click();
   await page.waitForFunction(t=>document.querySelector('#clean').currentTime>t+.5,tail);
   await page.getByRole('button',{name:'Pause',exact:true}).click();
   assert.deepEqual(await page.locator('video').evaluateAll(vs=>vs.map(v=>v.muted)),[true,false]);
   await page.locator('#seek').evaluate(el=>{el.value=10;el.dispatchEvent(new Event('input',{bubbles:true}))});
   await page.waitForFunction(()=>[...document.querySelectorAll('video')].every(v=>!v.seeking&&Math.abs(v.currentTime-10)<.1));
  }
  await page.screenshot({path:path.join(out,'comparison-preview.png')});
  assert.equal(await page.locator('#error').isVisible(),false);
  assert.deepEqual(errors,[]);
  fs.writeFileSync(path.join(out,'preview-verification.json'),JSON.stringify({passed:true,url:base+'compare.html',served_video_sha256:expected.video_sha256,sources,seek_and_play:true,current_video_tail_playable:version>=6,byte_ranges:true,page_errors:errors},null,2)+'\n');
  console.log('Served video hash, side-by-side seeking/playback, and browser errors: PASS');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
