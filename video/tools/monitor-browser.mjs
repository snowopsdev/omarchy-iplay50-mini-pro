// Bounded playback verification, using the already authorized localhost CDP tunnel.
const base = process.env.CDP_URL || 'http://127.0.0.1:19223';
const seconds = Number(process.argv[2] || 600);
if (!Number.isInteger(seconds) || seconds < 10 || seconds > 1800) throw Error('Invalid duration');
const targets = await (await fetch(base + '/json/list')).json();
const target = targets.find(t => t.type === 'page' && t.url.includes(process.env.CDP_TARGET || 'youtube.com/watch'));
if (!target) throw Error('Playback target missing');
const url = new URL(target.webSocketDebuggerUrl); url.host = new URL(base).host;
const ws = new WebSocket(url);
await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
let serial = 0;
const pending = new Map();
ws.onmessage = e => {
  const d = JSON.parse(e.data);
  if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); }
};
function call(method, params) {
  return new Promise((resolve, reject) => {
    const id = ++serial; pending.set(id, resolve);
    ws.send(JSON.stringify({id, method, params}));
    setTimeout(() => { if (pending.delete(id)) reject(Error('CDP timeout')); }, 10000).unref();
  });
}
const expression = `(()=>{const v=document.querySelector('video'),p=document.querySelector('#movie_player');if(!v)return {missing:true};const q=v.getVideoPlaybackQuality();return {time:v.currentTime,width:v.videoWidth,height:v.videoHeight,ready:v.readyState,paused:v.paused,ad:p?.classList.contains('ad-showing'),quality:p?.getPlaybackQuality(),format:p?.getVideoStats().fmt,fullscreen:!!document.fullscreenElement,total:q.totalVideoFrames,dropped:q.droppedVideoFrames,error:v.error?{code:v.error.code,message:v.error.message}:null}})()`;
const start = Date.now();
try {
  do {
    const response = await call('Runtime.evaluate', {expression, returnByValue:true});
    const value = response.result?.result?.value;
    if (!value) throw Error(JSON.stringify(response));
    console.log(JSON.stringify({elapsed:(Date.now()-start)/1000, ...value}));
    if ((Date.now()-start)/1000 >= seconds) break;
    await new Promise(r => setTimeout(r, 10000));
  } while (true);
} finally { ws.close(); }
