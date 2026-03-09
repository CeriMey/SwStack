const API_BASE = (() => {
  try {
    const u = new URL(window.location.href);
    const override = u.searchParams.get('api');
    if (override) return override.replace(/\/+$/, '');
    if (u.protocol === 'file:') return 'http://localhost:8088';
    return u.origin;
  } catch (e) {
    return '';
  }
})();

function apiUrl(path){
  if (!path) return API_BASE;
  if (typeof path !== 'string') return path;
  if (path.startsWith('http://') || path.startsWith('https://')) return path;
  if (!API_BASE) return path;
  if (path.startsWith('/')) return API_BASE + path;
  return API_BASE + '/' + path;
}

const WS_URL = (() => {
  try {
    const u = new URL(API_BASE || window.location.href);
    const wsPort = parseInt(u.port || '80', 10) + 1;
    u.port = wsPort;
    u.pathname = '/';
    u.protocol = (u.protocol === 'https:') ? 'wss:' : 'ws:';
    return u.toString();
  } catch (e) {
    return '';
  }
})();

async function postJson(url, obj){
  const r = await fetch(apiUrl(url),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)});
  return r.json();
}

let selectedDomain = '';
let selectedObject = '';
function target(){
  if (!selectedDomain || !selectedObject) return '';
  return selectedDomain + '/' + selectedObject;
}

let ws = null;
let wsPollTimer = null;

function wsSend(obj){
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  try { ws.send(JSON.stringify(obj)); } catch {}
}

function wsSetTarget(){
  const t = target();
  if (!t) return;
  wsSend({type:'setTarget', target:t});
  wsSend({type:'getState'});
}

function startStateUpdates(){
  if (wsPollTimer) { clearInterval(wsPollTimer); wsPollTimer = null; }
  if (ws) { try { ws.close(); } catch {} ws = null; }

  if (!WS_URL || typeof WebSocket === 'undefined') {
    wsPollTimer = setInterval(refreshState, 1000);
    refreshState();
    return;
  }

  try {
    ws = new WebSocket(WS_URL);
  } catch (e) {
    ws = null;
    wsPollTimer = setInterval(refreshState, 1000);
    refreshState();
    return;
  }

  ws.addEventListener('open', () => {
    wsSetTarget();
  });

  ws.addEventListener('message', (ev) => {
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    if (!msg || msg.type !== 'state') return;
    const o = {
      target: msg.target || '',
      lastPong: msg.lastPong || '',
      lastConfigAck: msg.lastConfigAck || '',
    };
    document.getElementById('state').textContent = JSON.stringify(o, null, 2);
  });

  ws.addEventListener('close', () => {
    ws = null;
    if (!wsPollTimer) {
      wsPollTimer = setInterval(refreshState, 1000);
      refreshState();
    }
  });
}

function renderSelectableList(containerId, items, getKey, getLabel, onClick){
  const root = document.getElementById(containerId);
  root.textContent = '';
  for (const it of items){
    const b = document.createElement('button');
    b.className = 'secondary';
    b.style.margin = '6px 6px 0 0';
    b.textContent = getLabel(it);
    b.onclick = () => onClick(it);
    root.appendChild(b);
  }
  if (!items.length){
    const p = document.createElement('div');
    p.className = 'muted';
    p.textContent = '(none)';
    root.appendChild(p);
  }
}

function selectTab(name){
  document.getElementById('tabConfig').style.display = (name==='config') ? '' : 'none';
  document.getElementById('tabSignals').style.display = (name==='signals') ? '' : 'none';
  document.getElementById('tabRpc').style.display = (name==='rpc') ? '' : 'none';
  document.getElementById('tabDebug').style.display = (name==='debug') ? '' : 'none';
}

async function reloadApps(){
  const apps = await (await fetch(apiUrl('/api/apps'))).json();
  renderSelectableList('appsList', apps,
    a => a.domain,
    a => `${a.domain} (clients=${a.clientCount})`,
    async (a) => {
      selectedDomain = a.domain;
      selectedObject = '';
      await reloadDevices();
      selectTab('config');
    }
  );
}

async function reloadDevices(){
  if (!selectedDomain){
    document.getElementById('devicesList').textContent = '';
    return;
  }
  const devs = await (await fetch(apiUrl('/api/devices?domain=' + encodeURIComponent(selectedDomain)))).json();
  renderSelectableList('devicesList', devs,
    d => d.object,
    d => `${d.object}`,
    async (d) => {
      selectedObject = d.object;
      wsSetTarget();
      await reloadConfigDoc();
      await reloadSignals();
      await reloadRpcs();
      await refreshState();
    }
  );
}

async function refreshState(){
  const t = target();
  if (!t) return;
  if (ws && ws.readyState === WebSocket.OPEN) {
    wsSetTarget();
    return;
  }
  const r = await fetch(apiUrl('/api/state?target='+encodeURIComponent(t)));
  document.getElementById('state').textContent = JSON.stringify(await r.json(),null,2);
}
async function loadAppsRaw(){
  const r = await fetch(apiUrl('/api/apps'));
  document.getElementById('registry').textContent = JSON.stringify(await r.json(),null,2);
}
async function loadRegistryRaw(){
  const r = await fetch(apiUrl('/api/registry'));
  document.getElementById('registry').textContent = JSON.stringify(await r.json(),null,2);
}
async function loadConnectionsRaw(){
  const t = target();
  const url = t
    ? ('/api/connections?target=' + encodeURIComponent(t))
    : (selectedDomain ? ('/api/connections?domain=' + encodeURIComponent(selectedDomain)) : '');
  if (!url){
    document.getElementById('registry').textContent = '(select an application or device first)';
    return;
  }
  const r = await fetch(apiUrl(url));
  document.getElementById('registry').textContent = JSON.stringify(await r.json(),null,2);
}

let cfgOriginal = null;
let cfgEdited = null;
let dirty = new Set();

function deepClone(o){ return JSON.parse(JSON.stringify(o)); }

function setPath(obj, path, value){
  const parts = path.split('/').filter(x=>x.length);
  let cur = obj;
  for (let i=0;i<parts.length-1;i++){
    const k = parts[i];
    if (typeof cur[k] !== 'object' || cur[k] === null || Array.isArray(cur[k])) cur[k] = {};
    cur = cur[k];
  }
  cur[parts[parts.length-1]] = value;
}

function updateDirtyLabel(){
  const el = document.getElementById('cfgDirty');
  if (!el) return;
  el.textContent = dirty.size ? ('dirty: ' + dirty.size) : 'clean';
}

function setConfigPubIdLabel(pubId){
  const el = document.getElementById('cfgPubId');
  if (!el) return;
  el.textContent = pubId ? ('pubId: ' + pubId) : '';
}

function clearConfigForm(){
  const root = document.getElementById('configForm');
  root.textContent = '';
}

function makeRow(path, kind, value){
  const row = document.createElement('div');
  row.className = 'cfg';

  const lab = document.createElement('label');
  lab.innerHTML = '<span class="path">'+path+'</span>';
  row.appendChild(lab);

  let input;
  if (kind === 'bool'){
    input = document.createElement('input');
    input.type = 'checkbox';
    input.checked = !!value;
    input.addEventListener('change', () => {
      if (!cfgEdited) return;
      setPath(cfgEdited, path, !!input.checked);
      dirty.add(path);
      status.textContent = 'dirty';
      updateDirtyLabel();
    });
  } else if (kind === 'int'){
    input = document.createElement('input');
    input.type = 'number';
    input.step = '1';
    input.value = (value ?? 0);
    input.addEventListener('change', () => {
      if (!cfgEdited) return;
      const v = parseInt(input.value||'0',10);
      setPath(cfgEdited, path, v);
      dirty.add(path);
      status.textContent = 'dirty';
      updateDirtyLabel();
    });
  } else if (kind === 'double'){
    input = document.createElement('input');
    input.type = 'number';
    input.step = 'any';
    input.value = (value ?? 0);
    input.addEventListener('change', () => {
      if (!cfgEdited) return;
      const v = parseFloat(input.value||'0');
      setPath(cfgEdited, path, v);
      dirty.add(path);
      status.textContent = 'dirty';
      updateDirtyLabel();
    });
  } else { // string
    input = document.createElement('input');
    input.type = 'text';
    input.style.minWidth = '260px';
    input.value = (value ?? '');
    input.addEventListener('change', () => {
      if (!cfgEdited) return;
      setPath(cfgEdited, path, input.value);
      dirty.add(path);
      status.textContent = 'dirty';
      updateDirtyLabel();
    });
  }
  row.appendChild(input);

  const status = document.createElement('div');
  status.className = 'status';
  status.textContent = '';
  row.appendChild(status);

  return row;
}

function renderConfigNode(container, node, prefix){
  if (node === null || node === undefined) return;
  if (Array.isArray(node)){
    // arrays not handled; display read-only
    const row = makeRow(prefix, 'string', JSON.stringify(node));
    row.querySelector('input').disabled = true;
    container.appendChild(row);
    return;
  }
  if (typeof node === 'object'){
    const keys = Object.keys(node);
    for (const k of keys){
      if (k === '__swconfig__') continue; // internal meta (not part of the effective config)
      const next = prefix ? (prefix + '/' + k) : k;
      const v = node[k];
      if (v !== null && typeof v === 'object' && !Array.isArray(v)){
        const g = document.createElement('div');
        g.className = 'group';
        const h = document.createElement('h3');
        h.textContent = next;
        g.appendChild(h);
        renderConfigNode(g, v, next);
        container.appendChild(g);
      } else {
        let kind = 'string';
        if (typeof v === 'boolean') kind = 'bool';
        else if (typeof v === 'number') kind = Number.isInteger(v) ? 'int' : 'double';
        else kind = 'string';
        container.appendChild(makeRow(next, kind, v));
      }
    }
    return;
  }
}

async function reloadConfig(){
  const t0 = target();
  if (!t0) return;
  const r = await fetch(apiUrl('/api/config?target='+encodeURIComponent(t0)));
  const cfg = await r.json();
  cfgOriginal = cfg;
  cfgEdited = deepClone(cfg);
  dirty = new Set();
  updateDirtyLabel();
  setConfigPubIdLabel('');

  clearConfigForm();
  const root = document.getElementById('configForm');
  renderConfigNode(root, cfgEdited, '');
}

async function reloadConfigDoc(){
  const t0 = target();
  if (!t0) return;
  const r = await fetch(apiUrl('/api/configDoc?target='+encodeURIComponent(t0)));
  const res = await r.json();
  if (res && res.ok === false){
    alert('load failed: ' + (res.error || 'unknown'));
    return;
  }
  const cfg = res && res.config ? res.config : {};
  cfgOriginal = cfg;
  cfgEdited = deepClone(cfg);
  dirty = new Set();
  updateDirtyLabel();
  setConfigPubIdLabel(res.pubId || '');

  clearConfigForm();
  const root = document.getElementById('configForm');
  renderConfigNode(root, cfgEdited, '');
}

async function sendAllConfig(){
  if (!cfgEdited) return;
  const t = target();
  if (!t) return;
  const res = await postJson('/api/configAll', {target: t, config: cfgEdited});
  if (!res.ok){
    alert('send failed: ' + (res.error || 'unknown'));
    return;
  }
  dirty = new Set();
  updateDirtyLabel();
  await refreshState();
  await reloadConfigDoc();
}

function clearSignals(){
  const root = document.getElementById('signalsList');
  root.textContent = '';
}

function inputForArg(typeName, initial){
  const t = (typeName||'').toLowerCase();
  if (t.includes('bool')){
    const i = document.createElement('input');
    i.type = 'checkbox';
    i.checked = !!initial;
    return {el:i, get:()=>!!i.checked};
  }
  if (t.includes('double') || t.includes('float')){
    const i = document.createElement('input');
    i.type = 'number'; i.step = 'any';
    i.value = (initial ?? 0);
    return {el:i, get:()=>parseFloat(i.value||'0')};
  }
  if (t.includes('uint64') || t.includes('__int64') || t.includes('unsigned long long')){
    const i = document.createElement('input');
    i.type = 'number'; i.step = '1';
    i.value = (initial ?? 0);
    return {el:i, get:()=>String(Math.max(0, parseInt(i.value||'0',10)))};
  }
  if (t.includes('int')){
    const i = document.createElement('input');
    i.type = 'number'; i.step = '1';
    i.value = (initial ?? 0);
    return {el:i, get:()=>parseInt(i.value||'0',10)};
  }
  // SwByteArray / SwString => text
  const i = document.createElement('input');
  i.type = 'text';
  i.style.minWidth = '180px';
  i.value = (initial ?? '');
  return {el:i, get:()=>i.value};
}

function renderSignalEntry(container, entry){
  const row = document.createElement('div');
  row.className = 'cfg';

  const name = entry.signal || '';
  const typeName = entry.typeName || '';
  const args = entry.args || [];

  const lab = document.createElement('label');
  lab.innerHTML = '<span class="path">'+name+'</span>';
  row.appendChild(lab);

  const argGets = [];
  for (let i=0;i<args.length;i++){
    const spec = inputForArg(args[i], '');
    row.appendChild(spec.el);
    argGets.push(spec.get);
  }

  const btn = document.createElement('button');
  btn.className = 'secondary';
  btn.textContent = 'Emit';
  row.appendChild(btn);

  const readBtn = document.createElement('button');
  readBtn.className = 'secondary';
  readBtn.textContent = 'Read';
  row.appendChild(readBtn);

  const status = document.createElement('div');
  status.className = 'status';
  status.textContent = '';
  row.appendChild(status);

  btn.addEventListener('click', async () => {
    status.textContent = 'sending...';
    const t = target();
    const argsOut = argGets.map(fn=>fn());
    const res = await postJson('/api/signal', {target: t, name: name, args: argsOut});
    status.textContent = res.ok ? 'ok' : ('error: ' + (res.error||''));
    await refreshState();
  });

  container.appendChild(row);

  const pre = document.createElement('pre');
  pre.style.marginLeft = '230px';
  pre.style.maxHeight = '200px';
  pre.textContent = '';
  pre.style.display = 'none';
  container.appendChild(pre);

  readBtn.addEventListener('click', async () => {
    status.textContent = 'reading...';
    pre.textContent = '';
    pre.style.display = 'none';
    const t = target();
    if (!t) return;
    const r = await fetch(apiUrl('/api/signalLatest?target=' + encodeURIComponent(t) + '&name=' + encodeURIComponent(name)));
    const res = await r.json();
    if (res && res.ok === false) {
      status.textContent = 'error: ' + (res.error || 'unknown');
      return;
    }
    const args = res && res.args ? res.args : [];
    status.textContent = 'seq=' + (res.seq || '?') + (res.changed ? '' : ' (unchanged)');
    pre.textContent = JSON.stringify(args, null, 2);
    pre.style.display = '';
  });

  const meta = document.createElement('div');
  meta.className = 'muted';
  meta.style.marginLeft = '230px';
  meta.textContent = typeName;
  container.appendChild(meta);
}

async function reloadSignals(){
  const t0 = target();
  if (!t0) return;
  const r = await fetch(apiUrl('/api/signals?target='+encodeURIComponent(t0)));
  const list = await r.json();
  clearSignals();
  const root = document.getElementById('signalsList');
  for (const e of list){
    if (e.kind !== 'signal') continue;
    renderSignalEntry(root, e);
  }
}

function clearRpcs(){
  const root = document.getElementById('rpcList');
  root.textContent = '';
}

function formatRpcValue(v){
  if (v === undefined) return '';
  if (v === null) return 'null';
  if (typeof v === 'string') return v;
  if (typeof v === 'number' || typeof v === 'boolean') return String(v);
  try { return JSON.stringify(v, null, 2); } catch { return String(v); }
}

function renderRpcEntry(container, entry){
  const row = document.createElement('div');
  row.className = 'cfg';

  const method = entry.method || '';
  const typeName = entry.typeName || '';
  const args = entry.args || [];

  const lab = document.createElement('label');
  lab.innerHTML = '<span class="path">'+method+'</span>';
  row.appendChild(lab);

  const argGets = [];
  for (let i=0;i<args.length;i++){
    const spec = inputForArg(args[i], '');
    row.appendChild(spec.el);
    argGets.push(spec.get);
  }

  const btn = document.createElement('button');
  btn.className = 'secondary';
  btn.textContent = 'Call';
  row.appendChild(btn);

  const status = document.createElement('div');
  status.className = 'status';
  status.textContent = '';
  row.appendChild(status);

  container.appendChild(row);

  const pre = document.createElement('pre');
  pre.style.marginLeft = '230px';
  pre.style.maxHeight = '200px';
  pre.textContent = '';
  pre.style.display = 'none';
  container.appendChild(pre);

  const meta = document.createElement('div');
  meta.className = 'muted';
  meta.style.marginLeft = '230px';
  meta.textContent = typeName;
  container.appendChild(meta);

  btn.addEventListener('click', async () => {
    status.textContent = 'calling...';
    pre.textContent = '';
    pre.style.display = 'none';
    const t = target();
    const argsOut = argGets.map(fn=>fn());
    const res = await postJson('/api/rpc', {target: t, method: method, args: argsOut});
    status.textContent = res.ok ? 'ok' : ('error: ' + (res.error||''));
    if (res && res.ok && Object.prototype.hasOwnProperty.call(res, 'result') && res.result !== null && res.result !== undefined) {
      pre.textContent = formatRpcValue(res.result);
      pre.style.display = '';
    } else if (res && res.ok) {
      pre.textContent = '';
      pre.style.display = 'none';
    } else {
      pre.textContent = JSON.stringify(res,null,2);
      pre.style.display = '';
    }
    await refreshState();
  });
}

async function reloadRpcs(){
  const t0 = target();
  if (!t0) return;
  const r = await fetch(apiUrl('/api/rpcs?target='+encodeURIComponent(t0)));
  const list = await r.json();
  clearRpcs();
  const root = document.getElementById('rpcList');
  for (const e of list){
    renderRpcEntry(root, e);
  }
}

startStateUpdates();
// config/signals need a selected target first
reloadApps();
