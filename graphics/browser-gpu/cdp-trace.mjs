// Trace a full-page colour animation over CDP (Chromium/Electron with
// --remote-debugging-port) and print time per event on GPU/Viz/compositor
// threads. Usage: CDP_URL=http://127.0.0.1:PORT node cdp-trace.mjs [SECONDS] [OUT.json]
import {writeFile} from 'node:fs/promises';
const base = process.env.CDP_URL || 'http://127.0.0.1:19224';
const browser = await (await fetch(base + '/json/version')).json();
const targets = await (await fetch(base + '/json/list')).json();
const page = targets.find(t => t.type === 'page');
function conn(url) {
  const remote = new URL(url); remote.host = new URL(base).host;
  const ws = new WebSocket(remote); let next = 0; const pending = new Map(); const events = [];
  ws.onmessage = e => { const d = JSON.parse(e.data); if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); } else events.push(d); };
  const ready = new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
  const call = (method, params = {}) => new Promise(r => { const id = ++next; pending.set(id, r); ws.send(JSON.stringify({id, method, params})); });
  return {ws, ready, call, events};
}
const b = conn(browser.webSocketDebuggerUrl), p = conn(page.webSocketDebuggerUrl);
await b.ready; await p.ready;
const secs = +(process.argv[2] || 3);
await b.call('Tracing.start', {traceConfig: {includedCategories: ['gpu', 'viz', 'toplevel', 'cc', 'disabled-by-default-gpu.service', 'disabled-by-default-viz.gpu_composite_time', 'ui', 'benchmark'], recordMode: 'recordContinuously'}, transferMode: 'ReportEvents'});
await p.call('Runtime.evaluate', {awaitPromise: true, expression: `new Promise(res => { const box = document.createElement('div'); box.style.cssText = 'position:fixed;inset:0;z-index:99999'; document.body.appendChild(box); const t0 = performance.now(); const f = t => { box.style.background = 'hsl(' + (t / 10 % 360) + ',60%,40%)'; if (t - t0 < ${secs * 1000}) requestAnimationFrame(f); else { box.remove(); res(); } }; requestAnimationFrame(f); })`});
const done = new Promise(r => { const iv = setInterval(() => { if (b.events.some(e => e.method === 'Tracing.tracingComplete')) { clearInterval(iv); r(); } }, 50); });
await b.call('Tracing.end'); await done;
const evs = b.events.filter(e => e.method === 'Tracing.dataCollected').flatMap(e => e.params.value);
await writeFile(process.argv[3] || 'trace.json', JSON.stringify({traceEvents: evs}));
const names = {}; for (const e of evs) if (e.ph === 'M' && e.name === 'thread_name') names[e.pid + ':' + e.tid] = e.args.name;
const agg = {};
for (const e of evs) if (e.ph === 'X' && e.dur) { const th = names[e.pid + ':' + e.tid] || '?'; if (!/Gpu|Viz|Compositor|VizCompositor|CrGpuMain|DrmThread/i.test(th)) continue; const k = th + ' | ' + e.name; (agg[k] ||= {n: 0, t: 0, max: 0}); agg[k].n++; agg[k].t += e.dur; agg[k].max = Math.max(agg[k].max, e.dur); }
const rows = Object.entries(agg).sort((a, c) => c[1].t - a[1].t).slice(0, 40);
for (const [k, v] of rows) console.log((v.t / 1000).toFixed(0).padStart(7), 'ms', String(v.n).padStart(5), 'x avg', (v.t / v.n / 1000).toFixed(2).padStart(7), 'max', (v.max / 1000).toFixed(1).padStart(6), ' ', k);
b.ws.close(); p.ws.close();
