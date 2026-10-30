/* DOM-only verification. No browser, server, device access, or layout claims. */
const fs=require('fs');
const path=require('path');
const assert=require('assert/strict');
const {JSDOM,VirtualConsole}=require('./.validation-deps/node_modules/jsdom');
const html=fs.readFileSync(path.join(__dirname,'omarchy-touch-review.html'),'utf8');
const errors=[],checks=[];const vc=new VirtualConsole();vc.on('jsdomError',e=>errors.push(e.message));
const dom=new JSDOM(html,{runScripts:'dangerously',url:'file://'+path.join(__dirname,'omarchy-touch-review.html'),virtualConsole:vc});
const w=dom.window,doc=w.document,ev=x=>w.eval(x),q=s=>doc.querySelector(s);
function check(label,fn){fn();checks.push(label)}
function run(x){return ev(x)}
function action(a,v=''){run(`act('a',${JSON.stringify(a)},${JSON.stringify(v)})`)}
function surface(s){run(`selected='a';compare=false;chooseSurface(${JSON.stringify(s)})`)}
check('Inline JavaScript boots without errors',()=>assert.equal(errors.length,0));
check('18 unique inventory entries and 3 navigation strategies',()=>{assert.equal(run('SURFACES.length'),18);assert.equal(run('new Set(SURFACES.map(x=>x.id)).size'),18);assert.equal(run('DIRS.length'),3)});
const combinations=[];
check('108 direction × surface × orientation DOM renders; named controls; valid icons; no duplicate IDs',()=>{
for(const orientation of ['landscape','portrait'])for(const d of ['a','b','c'])for(const id of run('SURFACES.map(x=>x.id)')){
run(`orientation=${JSON.stringify(orientation)};selected=${JSON.stringify(d)};compare=false;chooseSurface(${JSON.stringify(id)})`);
assert.ok(q(`[data-surface-render="${d}-${id}"]`),d+'/'+id);
const buttons=[...q(`[data-device="${d}"]`).querySelectorAll('button')];
buttons.forEach(b=>assert.ok((b.getAttribute('aria-label')||b.textContent).trim(),'unnamed '+d+'/'+id));
doc.querySelectorAll('use').forEach(u=>assert.ok(doc.getElementById(u.getAttribute('href').slice(1)),u.getAttribute('href')));
const ids=[...doc.querySelectorAll('[id]')].map(x=>x.id);assert.equal(new Set(ids).size,ids.length);
combinations.push(`${d}/${id}/${orientation}`);
}
});
check('Each direction has distinct structural markup on all 18 surfaces',()=>{
for(const id of run('SURFACES.map(x=>x.id)')){
const output=['a','b','c'].map(d=>{run(`selected='${d}';chooseSurface('${id}')`);return q(`[data-surface-render="${d}-${id}"]`).innerHTML});
assert.equal(new Set(output).size,3,id);
}
});
run("selected='a';orientation='landscape';states.a=INITIAL();surface='home';renderStage()");
check('Home → search raises Keys → launch Terminal → explicit Keys → keyboard typing → recents → split',()=>{
q('[data-device="a"] [data-action="goto"][data-value="search"]').click();
assert.equal(run('states.a.surface'),'search');
assert.equal(run('states.a.keyboard'),true);assert.equal(run('states.a.target'),'search');
for(const char of 'terminal')q(`[data-device="a"] .keyboard [data-action="key"][data-value="${char}"]`).click();
assert.equal(q('[data-target="search"]').value,'terminal');
q('[data-results] [data-action="launch"]').click();assert.equal(run('states.a.app'),'Terminal');assert.equal(run('states.a.keyboard'),false);
action('keyboard');q('[data-target="terminal"]').focus();
for(const char of 'pwd')q(`[data-device="a"] .keyboard [data-action="key"][data-value="${char}"]`).click();
assert.equal(run('states.a.text.terminal'),'pwd');
action('goto','recents');action('makeSplit');assert.equal(run('states.a.split'),true);assert.equal(run('states.a.surface'),'spaces');
action('focusPane','Chromium');assert.equal(run('states.a.activePane'),'Chromium');action('ratio');assert.equal(run('states.a.ratio'),'65:35');action('swap');assert.equal(run('states.a.swapped'),true);
});
check('OSK one click = one character; Shift applies once; symbols; combined modifiers reset; hidden keyboard clears modifiers',()=>{
surface('keyboard');q('[data-target="note"]').focus();run("states.a.text.note='';document.querySelector('[data-target=note]').value=''");
action('mod','Shift');action('key','a');action('key','b');assert.equal(run('states.a.text.note'),'Ab');assert.equal(run('states.a.mods.length'),0);
action('keyPage','123');action('key','@');assert.equal(run('states.a.text.note'),'Ab@');
action('mod','Ctrl');action('mod','Alt');action('key','q');assert.equal(run('states.a.text.note'),'Ab@');assert.equal(run('states.a.mods.length'),0);
action('mod','Super');action('hideKeyboard');assert.equal(run('states.a.mods.length'),0);assert.equal(run('states.a.keyboard'),false);
});
check('Keyboard navigation, Backspace, Delete and focus keep text destination',()=>{
surface('keyboard');q('[data-target="note"]').focus();run("states.a.text.note='abcd';document.querySelector('[data-target=note]').value='abcd';document.querySelector('[data-target=note]').setSelectionRange(2,2)");action('key','Backspace');assert.equal(run('states.a.text.note'),'acd');action('key','Delete');assert.equal(run('states.a.text.note'),'ad');action('key','Home');action('key','x');assert.equal(run('states.a.text.note'),'xad');
});
check('Terminal Ctrl+A means beginning of command; Ctrl+C does not copy; Super clipboard route remains available',()=>{
action('launch','Terminal');action('keyboard');q('[data-target="terminal"]').focus();run("states.a.text.terminal='hello';document.querySelector('[data-target=terminal]').value='hello';document.querySelector('[data-target=terminal]').setSelectionRange(5,5);states.a.clipboard='original'");action('mod','Ctrl');action('key','a');assert.equal(q('[data-target="terminal"]').selectionStart,0);action('mod','Ctrl');action('key','c');assert.equal(run('states.a.clipboard'),'original');assert.equal(run('states.a.mods.length'),0);run("document.querySelector('[data-target=terminal]').setSelectionRange(0,5)");action('mod','Super');action('key','c');assert.equal(run('states.a.clipboard'),'hello');
});
check('Physical keyboard navigation retains the activated OSK key and toggle focus',()=>{
surface('keyboard');action('keyPage','ABC');q('[data-target="note"]').focus();const key=q('.keyboard [data-action="key"][data-value="a"]');key.focus();key.click();assert.equal(doc.activeElement.dataset.action,'key');assert.equal(doc.activeElement.dataset.value,'a');surface('controls');const toggle=q('[data-action="toggle"][data-value="bluetooth"]');toggle.focus();toggle.click();assert.equal(doc.activeElement.dataset.action,'toggle');assert.equal(doc.activeElement.dataset.value,'bluetooth');
});
check('Changing app, focus target, orientation or keyboard page clears modifiers',()=>{
action('mod','Ctrl');action('launch','Chromium');assert.equal(run('states.a.mods.length'),0);surface('keyboard');action('mod','Ctrl');action('keyPage','Fn');assert.equal(run('states.a.mods.length'),0);action('mod','Super');run("rotate('portrait')");assert.equal(run('states.a.mods.length'),0);assert.equal(run('states.a.keyboard'),false);
});
check('Shell Back leaves app history alone and cannot bypass the lock storyboard',()=>{
action('launch','Chromium');action('back');assert.equal(run('states.a.surface'),'terminal');assert.equal(run('states.a.app'),'Chromium');action('appBack');assert.equal(run('states.a.app'),'Chromium');surface('lock');action('back');assert.equal(run('states.a.surface'),'lock');action('fillPassword');action('unlock');
});
check('Modal focus is contained, background is inert, and Cancel restores its opener',()=>{
surface('power');const open=q('[data-action="confirmPower"][data-value="Restart"]');open.focus();open.click();assert.ok(q('.review-controls').inert);const buttons=[...q('.mock-dialog').querySelectorAll('button')];buttons[buttons.length-1].focus();doc.activeElement.dispatchEvent(new w.KeyboardEvent('keydown',{key:'Tab',bubbles:true}));assert.equal(doc.activeElement,buttons[0]);action('cancelModal');assert.ok(!q('.review-controls').inert);assert.equal(doc.activeElement.dataset.action,'confirmPower');
});
check('Quick controls update mock toggles and slider readouts',()=>{
surface('controls');const old=run('states.a.bluetooth');action('toggle','bluetooth');assert.equal(run('states.a.bluetooth'),!old);const input=q('[data-slider="brightness"]');input.value='83';input.dispatchEvent(new w.Event('input',{bubbles:true}));assert.equal(run('states.a.brightness'),83);assert.equal(q('[data-output="brightness"]').textContent,'83%');
});
check('Settings root → drilldown → Back preserves a predictable root',()=>{surface('settings');action('setting','Display');assert.equal(run('states.a.settingsPage'),'Display');action('settingsBack');assert.equal(run('states.a.settingsPage'),'root')});
check('Wi-Fi wrong password, connecting, cancellation, connected and off states',()=>{
surface('wifi');action('chooseNetwork','Workshop');action('connectNetwork');assert.equal(run('states.a.networkStatus'),'Incorrect password');run("states.a.text.network='demo'");action('connectNetwork');assert.equal(run('states.a.networkStatus'),'Connecting…');action('cancelNetwork');assert.equal(run('states.a.networkStatus'),'Available');run("states.a.text.network='demo'");action('connectNetwork');action('completeNetwork');assert.equal(run('states.a.networkStatus'),'Connected');assert.equal(run('states.a.text.network'),'');action('toggle','wifi');assert.equal(run('states.a.networkStatus'),'Offline');
});
check('Notification dismiss, DND and empty history',()=>{surface('notifications');action('restoreNotices');action('dismissNotice','1');assert.equal(run('states.a.notices.length'),2);action('toggle','dnd');assert.equal(run('states.a.dnd'),true);action('clearNotices');assert.equal(run('states.a.notices.length'),0);assert.match(q('[data-device="a"]').textContent,/caught up/)});
check('Mock lock rejects incorrect password and unlocks with demo only',()=>{surface('lock');action('unlock');assert.equal(run('states.a.lockError'),true);action('fillPassword');action('unlock');assert.equal(run('states.a.surface'),'home');assert.equal(run('states.a.text.password'),'')});
check('Power and permission confirmations focus a dialog; Escape cancels; completing is simulated',()=>{
surface('power');action('confirmPower','Restart');assert.ok(q('[role="dialog"]'));assert.ok(doc.activeElement.closest('[role="dialog"]'));doc.activeElement.dispatchEvent(new w.KeyboardEvent('keydown',{key:'Escape',bubbles:true}));assert.equal(run('states.a.modal'),null);action('confirmPower','Shut down');action('finishPower','Shut down');assert.equal(run('states.a.surface'),'states');assert.match(run('states.a.toast'),/simulated/);action('permission');action('permissionResult','Denied');assert.equal(run('states.a.permission'),'Denied');action('permission');action('permissionResult','Allowed once');assert.equal(run('states.a.permission'),'Allowed once');
});
check('File navigation, empty Downloads, save confirmation and mock context clipboard',()=>{
surface('files');action('fileLocation','Downloads');assert.match(q('[data-device="a"]').textContent,/folder is empty/);action('fileLocation','Documents');action('selectFile','field-notes.md');action('fileMode','Save');action('saveFile');assert.equal(run('states.a.modal.kind'),'save');action('finishSave');assert.equal(run('states.a.modal'),null);surface('clipboard');action('selectText');action('copyMock');const before=run('states.a.text.note');action('pasteMock');assert.equal(run('states.a.text.note'),before+'Bring the terminal within reach.');action('clearClipboard');assert.equal(run('states.a.clipboard'),'');
});
check('All 4 OSK pages render in each direction and orientation',()=>{for(const d of ['a','b','c'])for(const o of ['landscape','portrait'])for(const p of ['ABC','123','Fn','Desktop']){run(`selected='${d}';orientation='${o}';chooseSurface('keyboard');act('${d}','keyPage','${p}')`);assert.ok(q(`[data-device="${d}"] .keyboard [data-action="key"], [data-device="${d}"] .keyboard [data-action="desktop"]`));}});
check('Compare shows three independently interactive previews and 54 jump cells',()=>{run("compare=true;surface='controls';DIRS.forEach(d=>states[d.id].surface='controls');renderStage();updateToolbar()");assert.equal(doc.querySelectorAll('[data-device]').length,3);assert.equal(doc.querySelectorAll('[data-matrix-dir]').length,54);const a=run('states.a.bluetooth');run("act('b','toggle','bluetooth')");assert.equal(run('states.a.bluetooth'),a);});
check('Notes are per surface and export excludes mock password and typed fields',()=>{run("compare=false;selected='a';chooseSurface('home')");q('#note').value='Keep a Home button';q('#note').dispatchEvent(new w.Event('input',{bubbles:true}));surface('search');assert.equal(q('#note').value,'');surface('home');assert.equal(q('#note').value,'Keep a Home button');run("states.a.text.password='private-do-not-export'");const payload=run('exportPayload()');assert.match(payload,/Keep a Home button/);assert.ok(!payload.includes('private-do-not-export'));assert.equal(run('persist'),false)});
check('file:// clipboard fallback is visible and selectable',()=>{q('#copyNotes').click();assert.ok(q('#exportFallback').classList.contains('visible'));assert.match(q('#exportText').value,/Keep a Home button/)});
check('No remote runtime assets, no iframe, no fetch, no vh or full-page height',()=>{
assert.equal(doc.querySelectorAll('script[src],link[rel=stylesheet],img[src^=http],iframe,video,audio').length,0);assert.ok(!/\bfetch\(|XMLHttpRequest|WebSocket|sendBeacon/.test(html));assert.ok(!/\d(?:d|s|l)?vh\b/.test(html));assert.ok(!/html\s*,\s*body\s*\{[^}]*height\s*:\s*100%/.test(html));assert.ok(html.includes('prefers-reduced-motion'));assert.ok(html.includes('forced-colors'));
});
check('Refined A dock has six stable grouped controls and marks the current destination',()=>{run("selected='a';compare=false;states.a=INITIAL();surface='home';renderStage()");assert.equal(doc.querySelectorAll('.a .navbottom button').length,6);assert.equal(doc.querySelectorAll('.a .dock-group').length,3);assert.equal(q('.a .dock-item[aria-current="page"]').dataset.value,'home');action('launch','Chromium');assert.ok(q('.a .a-handlebar'));assert.equal(q('.a .navbottom'),null);action('peekDock');assert.equal(q('.a .dock-item[aria-current="page"]').dataset.value,'Chromium');assert.ok(q('.a .navbottom [data-action="keyboard"]'))});
check('Refined A keyboard has four ABC rows, retains all modifiers and exposes every arrow on Fn',()=>{surface('keyboard');action('keyPage','ABC');assert.equal(doc.querySelectorAll('.a .keyboard .keys-row').length,4);for(const name of ['Ctrl','Alt','Super','Shift'])assert.ok(q('.a .keyboard [data-action="mod"][data-value="'+name+'"]'));action('keyPage','Fn');for(const name of ['Left','Down','Up','Right'])assert.ok(q('.a .keyboard [data-action="key"][data-value="'+name+'"]'))});
check('Refined A all eighteen surfaces omit design-decision banners inside the mock OS',()=>{for(const id of run('SURFACES.map(x=>x.id)')){surface(id);assert.equal(doc.querySelectorAll('.a .direction-note').length,0);assert.ok(q('[data-surface-render="a-'+id+'"]'))}});
check('Refined A static recents previews resume the named app and keep Close separate',()=>{run("states.a=INITIAL()");surface('recents');for(const app of ['Terminal','Chromium','Files'])assert.ok(q('.a .recent-preview[data-value="'+app+'"]'));q('.a .recent-preview[data-value="Files"]').click();assert.equal(run('states.a.app'),'Files');assert.equal(run('states.a.surface'),'terminal')});
check('Refined A apps fill the canvas; the dock steps aside for apps and the keyboard',()=>{run("states.a=INITIAL()");surface('home');action('launch','Terminal');assert.ok(q('.a .a-canvas-app .a-app [data-target="terminal"]'));assert.equal(q('.a .page-head'),null);assert.equal(q('.a .navbottom'),null);action('keyboard');assert.ok(q('.a .keyboard'));assert.equal(q('.a .a-handlebar'),null);assert.equal(q('.a .navbottom'),null);action('hideKeyboard');surface('home');action('keyboard');assert.equal(q('.a .navbottom'),null);action('hideKeyboard');assert.ok(q('.a .navbottom'))});
check('Refined A tapping a text field raises Keys for that field; one Hide control while typing',()=>{run("states.a=INITIAL()");surface('wifi');action('chooseNetwork','Workshop');q('.a [data-target="network"]').click();assert.equal(run('states.a.keyboard'),true);assert.equal(run('states.a.target'),'network');assert.equal(doc.querySelectorAll('.a [data-action="hideKeyboard"]').length,1);surface('keyboard');assert.equal(doc.querySelectorAll('.a [data-action="hideKeyboard"]').length,1);assert.equal(doc.querySelectorAll('.a .page-head [data-action="keyboard"]').length,0)});
check('Refined A search Back leaves in one tap; Escape-style Back still hides Keys first',()=>{run("states.a=INITIAL()");surface('home');q('[data-device="a"] [data-action="goto"][data-value="search"]').click();action('leave');assert.equal(run('states.a.surface'),'home');assert.equal(run('states.a.keyboard'),false);q('[data-device="a"] [data-action="goto"][data-value="search"]').click();action('back');assert.equal(run('states.a.surface'),'search');assert.equal(run('states.a.keyboard'),false)});
check('Refined A window actions appear only once two apps are paired; Ratio is landscape-only',()=>{run("rotate('landscape');states.a=INITIAL()");surface('spaces');assert.equal(q('.a [data-action="swap"]'),null);assert.ok(q('.a [data-action="makeSplit"]'));action('makeSplit');for(const a of ['swap','ratio','leaveSplit','moveApp','closeApp'])assert.ok(q('.a [data-action="'+a+'"]'),a);assert.equal(q('.a .a-window-actions [data-action="makeSplit"]'),null);run("rotate('portrait')");assert.equal(q('.a [data-action="ratio"]'),null);assert.ok(q('.a [data-action="swap"]'))});
check('Refined A text editing uses double-tap / long-press menus instead of visible edit buttons',()=>{run("rotate('landscape');states.a=INITIAL()");surface('clipboard');for(const a of ['selectText','copyMock','cutMock','pasteMock','context'])assert.equal(q('.a [data-action="'+a+'"]'),null,a);const tapWord=i=>{for(const t of ['pointerdown','pointerup'])q('.a [data-word="'+i+'"]').dispatchEvent(new w.MouseEvent(t,{bubbles:true}))};tapWord(2);tapWord(2);assert.equal(run('JSON.stringify(states.a.aSel)'),'[2,2]');assert.ok(q('.a .a-edit-menu [data-action="aCopy"]'));action('aSelectSentence');assert.equal(run('JSON.stringify(states.a.aSel)'),'[0,4]');action('aCopy');assert.equal(run('states.a.clipboard'),'Bring the terminal within reach.');assert.equal(q('.a .a-edit-menu'),null);const note=q('.a [data-target="note"]');note.setSelectionRange(note.value.length,note.value.length);note.dispatchEvent(new w.MouseEvent('contextmenu',{bubbles:true,cancelable:true}));assert.equal(run('states.a.aMenu.kind'),'field');const before=run('states.a.text.note');action('aPaste');assert.equal(run('states.a.text.note'),before+'Bring the terminal within reach.');q('.a [data-word="0"]').dispatchEvent(new w.MouseEvent('contextmenu',{bubbles:true,cancelable:true}));assert.ok(q('.a .a-edit-menu'));action('back');assert.equal(run('states.a.aMenu'),null);assert.equal(run('states.a.surface'),'clipboard');surface('lock');const pw=q('.a [data-target="password"]');pw.value='demo';pw.dispatchEvent(new w.Event('input',{bubbles:true}));pw.setSelectionRange(0,4);pw.dispatchEvent(new w.MouseEvent('contextmenu',{bubbles:true,cancelable:true}));assert.equal(q('.a .a-edit-menu [data-action="aCopy"]'),null);assert.equal(q('.a .a-edit-menu [data-action="aCut"]'),null)});
check('No runtime errors after the complete DOM interaction pass',()=>assert.deepEqual(errors,[]));
const result={date:'2026-10-29',environment:'Node + jsdom 26.1.0; file URL; DOM and state only; no browser renderer',passed:checks.length,renderedCombinations:combinations.length,checks,limitations:['No pixel/layout/overflow inspection','No screen reader or real touch evaluation','No compositor, authentication, hardware or OS integration testing']};
fs.writeFileSync(path.join(__dirname,'validation.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));dom.window.close();
