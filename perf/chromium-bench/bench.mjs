// CDP benchmark against a throwaway Chromium on the tablet (via ssh -L).
// Measures: navigation -> load -> first painted frame, and rAF fps while
// scrolling continuously for 5 s.  usage: node bench.mjs LABEL
const base = process.env.CDP_URL || 'http://127.0.0.1:19333';
const label = process.argv[2] || 'run';
const targets = await (await fetch(base + '/json/list')).json();
const target = targets.find(t => t.type === 'page');
const url = new URL(target.webSocketDebuggerUrl); url.host = new URL(base).host;
const ws = new WebSocket(url);
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
let serial = 0; const pending = new Map(); const events = [];
ws.onmessage = e => { const d = JSON.parse(e.data);
  if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); } else events.push(d); };
const call = (method, params = {}) => new Promise((res, rej) => { const id = ++serial; pending.set(id, res);
  ws.send(JSON.stringify({ id, method, params })); setTimeout(() => pending.delete(id) && rej(Error('timeout ' + method)), 60000).unref(); });
const evalv = async expr => (await call('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true })).result.result.value;
await call('Page.enable');
const page = process.env.BENCH_PAGE;
const out = { label };
// Navigation to first frame painted after load.
await call('Page.navigate', { url: 'about:blank' }); await new Promise(r => setTimeout(r, 1500));
const t0 = Date.now(); events.length = 0;
await call('Page.navigate', { url: page });
while (!events.some(e => e.method === 'Page.loadEventFired')) await new Promise(r => setTimeout(r, 10));
out.loadMs = Date.now() - t0;
await evalv('new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))');
out.firstFrameMs = Date.now() - t0;
await new Promise(r => setTimeout(r, 1000));
// Scrolling throughput.
out.scrollFps = await evalv(`new Promise(r=>{let n=0,t0=performance.now(),y=0;function f(t){n++;y+=40;scrollTo(0,y%(document.body.scrollHeight-innerHeight));if(t-t0<5000)requestAnimationFrame(f);else r(+(n*1000/(t-t0)).toFixed(1))}requestAnimationFrame(f)})`);
console.log(JSON.stringify(out));
ws.close();
