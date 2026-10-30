// Real-site timing via CDP: navigation phases + paint milestones.
// usage: node sites.mjs URL [URL...]
const base = process.env.CDP_URL || 'http://127.0.0.1:19333';
const targets = await (await fetch(base + '/json/list')).json();
const target = targets.find(t => t.type === 'page');
const url = new URL(target.webSocketDebuggerUrl); url.host = new URL(base).host;
const ws = new WebSocket(url);
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
let serial = 0; const pending = new Map(); const events = [];
ws.onmessage = e => { const d = JSON.parse(e.data);
  if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); } else events.push(d); };
const call = (method, params = {}) => new Promise((res, rej) => { const id = ++serial; pending.set(id, res);
  ws.send(JSON.stringify({ id, method, params })); setTimeout(() => pending.delete(id) && rej(Error('timeout ' + method)), 90000).unref(); });
await call('Page.enable');
if (process.env.BENCH_UA) await call('Network.setUserAgentOverride', { userAgent: process.env.BENCH_UA });
for (const site of process.argv.slice(2)) {
  for (const pass of ['cold', 'warm']) {
    events.length = 0;
    const t0 = Date.now();
    await call('Page.navigate', { url: site });
    while (!events.some(e => e.method === 'Page.loadEventFired') && Date.now() - t0 < 60000) await new Promise(r => setTimeout(r, 20));
    await new Promise(r => setTimeout(r, 4000));   // let late content and LCP settle
    const v = (await call('Runtime.evaluate', { returnByValue: true, awaitPromise: true, expression: `new Promise(r=>{
      const n=performance.getEntriesByType('navigation')[0]||{}; const p={};
      for(const e of performance.getEntriesByType('paint')) p[e.name]=Math.round(e.startTime);
      let lcp=0; new PerformanceObserver(l=>{for(const e of l.getEntries()) lcp=Math.round(e.startTime)}).observe({type:'largest-contentful-paint',buffered:true});
      let lt=0; try{new PerformanceObserver(l=>{for(const e of l.getEntries()) lt+=e.duration}).observe({type:'longtask',buffered:true})}catch(e){}
      const vis=[...document.images].filter(i=>{const b=i.getBoundingClientRect();return b.width>40&&b.top<innerHeight&&b.bottom>0});
      const shown=vis.filter(i=>i.complete&&i.naturalWidth>0).length;
      setTimeout(()=>r({thumbs:shown+'/'+vis.length,ttfb:Math.round(n.responseStart),response:Math.round(n.responseEnd),dcl:Math.round(n.domContentLoadedEventEnd),load:Math.round(n.loadEventEnd),fcp:p['first-contentful-paint'],lcp,longTasksMs:Math.round(lt),resources:performance.getEntriesByType('resource').length}),300)})` })).result.result.value;
    console.log(JSON.stringify({ site: new URL(site).host, pass, ...v }));
  }
}
ws.close();
