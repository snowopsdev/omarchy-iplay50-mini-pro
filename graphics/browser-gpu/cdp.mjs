import {writeFile,readFile} from 'node:fs/promises';
const base = process.env.CDP_URL || 'http://127.0.0.1:19223';
const what = process.argv[2] || 'gpu';
const targets = await (await fetch(base + '/json/list')).json();
const browser = await (await fetch(base + '/json/version')).json();
let target = targets.find(t => t.type === 'page' && (!process.env.CDP_TARGET || t.url.includes(process.env.CDP_TARGET)));
for(let attempt=0;!target&&attempt<50;attempt++){await new Promise(r=>setTimeout(r,100));const refreshed=await(await fetch(base+'/json/list')).json();target=refreshed.find(t=>t.type==='page'&&(!process.env.CDP_TARGET||t.url.includes(process.env.CDP_TARGET)));}
if(!target&&!['gpu','close','processes'].includes(what))throw new Error('No matching page target');
const url = ['gpu','close','processes'].includes(what) ? browser.webSocketDebuggerUrl : target.webSocketDebuggerUrl;
const remote = new URL(url); remote.host = new URL(base).host;
const ws = new WebSocket(remote);
await new Promise((resolve,reject)=>{ws.onopen=resolve;ws.onerror=reject});
let next=0; const pending = new Map();
ws.onmessage = e => { const d=JSON.parse(e.data); if(d.id && pending.has(d.id)){ pending.get(d.id)(d); pending.delete(d.id); } };
function call(method, params={}) { return new Promise((resolve,reject)=>{const id=++next;pending.set(id,resolve);ws.send(JSON.stringify({id,method,params}));setTimeout(()=>{if(pending.delete(id))reject(new Error('timeout '+method))},30000).unref()}); }
let result;
if(what==='close')result=await call('Browser.close');
else if(what==='processes')result=await call('SystemInfo.getProcessInfo');
else if(what==='gpu')result=await call('SystemInfo.getInfo');
else if(what==='eval-file')result=await call('Runtime.evaluate',{expression:await readFile(process.argv[3],'utf8'),returnByValue:true,awaitPromise:true,userGesture:true});
else if(what==='eval')result=await call('Runtime.evaluate',{expression:process.argv[3],returnByValue:true,awaitPromise:true,userGesture:true});
else if(what==='click-selector') {
 const selector=JSON.stringify(process.argv[3]);
 const r=await call('Runtime.evaluate',{expression:`(()=>{const e=document.querySelector(${selector});if(!e)throw Error('Missing target');const r=e.getBoundingClientRect();if(!r.width||!r.height)throw Error('Hidden target');return {x:r.x+r.width/2,y:r.y+r.height/2}})()`,returnByValue:true});
 const point=r.result?.result?.value;if(!point)throw Error(JSON.stringify(r));
 await call('Input.dispatchMouseEvent',{type:'mousePressed',...point,button:'left',clickCount:1});
 result=await call('Input.dispatchMouseEvent',{type:'mouseReleased',...point,button:'left',clickCount:1});
}
else if(what==='navigate')result=await call('Page.navigate',{url:process.argv[3]});
else if(what==='screenshot') { result=await call('Page.captureScreenshot');await writeFile(process.argv[3],Buffer.from(result.result.data,'base64')); result={saved:process.argv[3]}; }
else throw new Error('unsupported');
console.log(JSON.stringify(result,null,2));ws.close();
