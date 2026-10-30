import { writeFile } from 'node:fs/promises';
const base = process.env.CDP_URL || 'http://127.0.0.1:19223';
const targets = await (await fetch(base + '/json/list')).json();
const target = targets.find(t => t.type === 'page' && t.url.includes(process.env.CDP_TARGET || 'playback.html'));
if (!target) throw new Error('Requested playback page missing');
const remote = new URL(target.webSocketDebuggerUrl);
remote.host = new URL(base).host;
const ws = new WebSocket(remote);
await new Promise((resolve,reject) => { ws.onopen=resolve; ws.onerror=reject; });
let serial=0; const pending=new Map(); const events=[];
ws.onmessage = e => {
  const message=JSON.parse(e.data);
  if (message.id && pending.has(message.id)) {
    pending.get(message.id)(message); pending.delete(message.id);
  } else if (message.method?.startsWith('Media.')) events.push(message);
};
function call(method,params={}) {
  return new Promise((resolve,reject) => {
    const id=++serial;pending.set(id,resolve);ws.send(JSON.stringify({id,method,params}));
    setTimeout(() => {if(pending.delete(id))reject(new Error('CDP timeout '+method));},15000).unref();
  });
}
await call('Media.enable');
if (process.env.CDP_NO_RELOAD !== '1') await call('Page.reload');
await new Promise(resolve => setTimeout(resolve,10000));
const state=await call('Runtime.evaluate',{returnByValue:true,expression:`(()=>{const v=document.querySelector('video');return v?{src:v.currentSrc,readyState:v.readyState,width:v.videoWidth,height:v.videoHeight,currentTime:v.currentTime,paused:v.paused,error:v.error?{code:v.error.code,message:v.error.message}:null,quality:(()=>{const q=v.getVideoPlaybackQuality();return {total:q.totalVideoFrames,dropped:q.droppedVideoFrames,corrupted:q.corruptedVideoFrames}})()}:null})()`});
const result={target:target.url,events,state};
await writeFile(process.argv[2],JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));ws.close();
