/* global acquireVsCodeApi */

const vscode = acquireVsCodeApi();

function $(id) {
  return document.getElementById(id);
}

function qsa(sel) {
  return Array.from(document.querySelectorAll(sel));
}

function nowMs() {
  return Date.now();
}

function safeJson(v) {
  try {
    return JSON.stringify(v, null, 2);
  } catch {
    return String(v);
  }
}

function debounce(fn, ms) {
  let t = 0;
  return (...args) => {
    if (t) clearTimeout(t);
    t = setTimeout(() => fn(...args), ms);
  };
}

function isActiveWithin(id) {
  const root = $(id);
  const el = document.activeElement;
  if (!root || !el) return false;
  return root.contains(el);
}

const rpc = (() => {
  let seq = 1;
  const pending = new Map();

  window.addEventListener('message', (ev) => {
    const msg = ev.data;
    if (!msg || msg.type !== 'response') return;
    const p = pending.get(msg.id);
    if (!p) return;
    pending.delete(msg.id);
    if (msg.ok) p.resolve(msg.data);
    else p.reject(new Error(msg.error || 'unknown error'));
  });

  function request(action, params) {
    const id = String(seq++);
    const payload = { type: 'request', id, action, params: params || {} };
    return new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject, t: nowMs() });
      vscode.postMessage(payload);
    });
  }

  return { request };
})();

// Messages from the extension side.
window.addEventListener('message', (ev) => {
  const msg = ev.data;
  if (!msg || typeof msg !== 'object') return;

  if (msg.type === 'settings') {
    setDashboardAutoRefreshMs(msg.dashboardAutoRefreshMs);
    return;
  }

  if (msg.type === 'init') {
    const sel = (msg && msg.selection) ? msg.selection : {};
    if (sel && sel.domain) selectedDomain = String(sel.domain || '');
    if (sel && sel.target) selectedTarget = String(sel.target || '');
    refreshAll().catch((e) => setRaw({ ok: false, error: String(e && e.message ? e.message : e) }));
  }
});

let apps = [];
let devices = [];
let domainOptionsSig = '';
let targetOptionsSig = '';
let selectedDomain = '';
let selectedTarget = '';
let selectedDevice = null;
let currentTab = 'graph';
let graphDragging = false;

let dashboardAutoRefreshMs = 0;
let dashboardAutoRefreshTimer = 0;
let dashboardAutoRefreshBusy = false;
let dashboardAutoRefreshPauseUntilMs = 0;

function pauseDashboardAutoRefresh(ms) {
  const n = Number(ms);
  if (!Number.isFinite(n) || n <= 0) return;
  dashboardAutoRefreshPauseUntilMs = Math.max(dashboardAutoRefreshPauseUntilMs, nowMs() + n);
}

function targetLabel() {
  return selectedTarget || '';
}

function setBridgeState(text, ok) {
  const el = $('bridgeState');
  el.textContent = text;
  el.style.borderColor = ok ? '' : 'var(--danger)';
}

function setTargetMeta(text) {
  $('targetMeta').textContent = text || '';
}

function setRaw(obj) {
  $('raw').textContent = safeJson(obj);
}

function setTab(name) {
  const tabs = ['graph', 'config', 'signals', 'rpc', 'raw'];
  for (const t of tabs) {
    $(('tab-' + t)).style.display = (t === name) ? '' : 'none';
  }
  qsa('.tabBtn').forEach((b) => {
    b.classList.toggle('active', b.getAttribute('data-tab') === name);
  });
  currentTab = name;
}

function normalizeRefreshMs(ms) {
  const n = Number(ms);
  if (!Number.isFinite(n)) return 0;
  const v = Math.floor(n);
  if (v <= 0) return 0;
  return Math.max(500, v);
}

function setDashboardAutoRefreshMs(ms) {
  dashboardAutoRefreshMs = normalizeRefreshMs(ms);
  if (dashboardAutoRefreshTimer) clearInterval(dashboardAutoRefreshTimer);
  dashboardAutoRefreshTimer = 0;

  document.body.classList.toggle('autoRefreshOn', dashboardAutoRefreshMs > 0);

  if (dashboardAutoRefreshMs > 0) {
    dashboardAutoRefreshTimer = setInterval(() => {
      autoRefreshTick().catch(() => {});
    }, dashboardAutoRefreshMs);
  }
}

async function autoRefreshTick() {
  if (dashboardAutoRefreshBusy) return;
  if (document.visibilityState === 'hidden') return;
  if (dashboardAutoRefreshPauseUntilMs && nowMs() < dashboardAutoRefreshPauseUntilMs) return;
  if (currentTab === 'graph' && graphDragging) return;

  dashboardAutoRefreshBusy = true;
  try {
    await rpc.request('ensureBridge', {});
    setBridgeState('bridge: ok', true);

    // Keep selection stable; update lists/graph/RPCs/signals. Avoid overwriting config edits.
    await refreshAppsAndDomains();
    await refreshDevicesForDomain();
    if (currentTab === 'graph') await refreshGraph();
    if (currentTab === 'config' && !dirty.size && !isActiveWithin('configForm')) await refreshConfig();
    if (currentTab === 'signals' && !isActiveWithin('signalsList')) await refreshSignals();
    if (currentTab === 'rpc' && !isActiveWithin('rpcList')) await refreshRpcs();
  } catch (e) {
    setBridgeState('bridge: error', false);
  } finally {
    dashboardAutoRefreshBusy = false;
  }
}

async function refreshAppsAndDomains() {
  const list = await rpc.request('apps', {});
  apps = Array.isArray(list) ? list : [];

  const doms = apps
    .map((a) => (a && a.domain) ? String(a.domain) : '')
    .filter((d) => d.length)
    .sort((a, b) => a.localeCompare(b));

  const sel = $('selDomain');
  const sig = doms.join('\n');
  if (sig !== domainOptionsSig) {
    domainOptionsSig = sig;
    sel.textContent = '';
    for (const d of doms) {
      const opt = document.createElement('option');
      opt.value = d;
      opt.textContent = d;
      sel.appendChild(opt);
    }
  }

  if (!doms.length) {
    selectedDomain = '';
    devices = [];
    renderTargets();
    return;
  }

  if (!selectedDomain || !doms.includes(selectedDomain)) {
    selectedDomain = doms[0];
  }
  sel.value = selectedDomain;
}

async function refreshDevicesForDomain() {
  if (!selectedDomain) {
    devices = [];
    renderTargets();
    return;
  }

  const list = await rpc.request('devices', { domain: selectedDomain });
  devices = Array.isArray(list) ? list : [];
  devices.sort((a, b) => String(a.object || '').localeCompare(String(b.object || '')));
  renderTargets();
}

function renderTargets() {
  const sel = $('selTarget');

  const opts = [];
  for (const d of devices) {
    const t = d && d.target ? String(d.target) : '';
    if (!t) continue;
    const pids = Array.isArray(d.pids) ? d.pids.join(',') : '';
    const text = `${String(d.object || t)}${pids ? ` (pid=${pids})` : ''}`;
    opts.push({ value: t, text });
  }

  const sig = opts.map((o) => `${o.value}\t${o.text}`).join('\n');

  if (!opts.length) {
    selectedTarget = '';
    selectedDevice = null;
    if (targetOptionsSig !== '') {
      targetOptionsSig = '';
      sel.textContent = '';
      sel.appendChild(new Option('(no targets)', ''));
    } else if (!sel.options.length) {
      sel.appendChild(new Option('(no targets)', ''));
    }
    sel.value = '';
    setTargetMeta('');
    return;
  }

  if (sig !== targetOptionsSig) {
    targetOptionsSig = sig;
    sel.textContent = '';
    for (const o of opts) sel.appendChild(new Option(o.text, o.value));
  }

  const targets = opts.map((o) => o.value);
  if (!selectedTarget || !targets.includes(selectedTarget)) selectedTarget = targets[0];

  sel.value = selectedTarget;
  selectedDevice = devices.find((d) => String(d.target || '') === selectedTarget) || null;
  updateMetaFromSelected_();
}

function updateMetaFromSelected_() {
  if (!selectedDevice) {
    setTargetMeta('');
    return;
  }
  const alive = (selectedDevice.alive !== undefined) ? !!selectedDevice.alive : true;
  const pids = Array.isArray(selectedDevice.pids) ? selectedDevice.pids.join(',') : '';
  setTargetMeta(`${targetLabel()}  |  ${alive ? 'alive' : 'stale'}${pids ? `  |  pid=${pids}` : ''}`);
}

async function refreshAll() {
  try {
    setBridgeState('bridge: connecting…', true);
    await rpc.request('ensureBridge', {});
    setBridgeState('bridge: ok', true);
  } catch (e) {
    setBridgeState('bridge: error', false);
    setRaw({ ok: false, error: String(e && e.message ? e.message : e) });
    return;
  }

  await refreshAppsAndDomains();
  await refreshDevicesForDomain();
  await refreshGraph();
  await refreshConfig();
  await refreshSignals();
  await refreshRpcs();
}

// ---------------- Config UI (adapted from SwBridge UI) ----------------

let cfgOriginal = null;
let cfgEdited = null;
let dirty = new Set();

function deepClone(o) {
  return JSON.parse(JSON.stringify(o));
}

function setPath(obj, path, value) {
  const parts = path.split('/').filter((x) => x.length);
  let cur = obj;
  for (let i = 0; i < parts.length - 1; i++) {
    const k = parts[i];
    if (typeof cur[k] !== 'object' || cur[k] === null || Array.isArray(cur[k])) cur[k] = {};
    cur = cur[k];
  }
  cur[parts[parts.length - 1]] = value;
}

function updateDirtyLabel() {
  $('cfgDirty').textContent = dirty.size ? ('dirty: ' + dirty.size) : 'clean';
}

function clearConfigForm() {
  $('configForm').textContent = '';
}

function makeCfgRow(path, kind, value) {
  const row = document.createElement('div');
  row.className = 'cfgRow';

  const lab = document.createElement('label');
  lab.textContent = path;
  row.appendChild(lab);

  let input;
  if (kind === 'bool') {
    input = document.createElement('input');
    input.type = 'checkbox';
    input.checked = !!value;
    input.addEventListener('change', () => {
      if (!cfgEdited) return;
      setPath(cfgEdited, path, !!input.checked);
      dirty.add(path);
      updateDirtyLabel();
      status.textContent = 'dirty';
    });
  } else if (kind === 'int') {
    input = document.createElement('input');
    input.type = 'number';
    input.step = '1';
    input.value = (value ?? 0);
    input.addEventListener('change', () => {
      if (!cfgEdited) return;
      const v = parseInt(input.value || '0', 10);
      setPath(cfgEdited, path, v);
      dirty.add(path);
      updateDirtyLabel();
      status.textContent = 'dirty';
    });
  } else if (kind === 'double') {
    input = document.createElement('input');
    input.type = 'number';
    input.step = 'any';
    input.value = (value ?? 0);
    input.addEventListener('change', () => {
      if (!cfgEdited) return;
      const v = parseFloat(input.value || '0');
      setPath(cfgEdited, path, v);
      dirty.add(path);
      updateDirtyLabel();
      status.textContent = 'dirty';
    });
  } else {
    input = document.createElement('input');
    input.type = 'text';
    input.style.minWidth = '260px';
    input.value = (value ?? '');
    input.addEventListener('change', () => {
      if (!cfgEdited) return;
      setPath(cfgEdited, path, input.value);
      dirty.add(path);
      updateDirtyLabel();
      status.textContent = 'dirty';
    });
  }
  row.appendChild(input);

  const status = document.createElement('div');
  status.className = 'status';
  status.textContent = '';
  row.appendChild(status);

  return row;
}

function renderConfigNode(container, node, prefix) {
  if (node === null || node === undefined) return;
  if (Array.isArray(node)) {
    const row = makeCfgRow(prefix, 'string', JSON.stringify(node));
    const input = row.querySelector('input');
    if (input) input.disabled = true;
    container.appendChild(row);
    return;
  }
  if (typeof node === 'object') {
    const keys = Object.keys(node);
    for (const k of keys) {
      if (k === '__swconfig__') continue;
      const next = prefix ? (prefix + '/' + k) : k;
      const v = node[k];
      if (v !== null && typeof v === 'object' && !Array.isArray(v)) {
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
        container.appendChild(makeCfgRow(next, kind, v));
      }
    }
  }
}

async function refreshConfig() {
  if (!selectedTarget) return;
  const cfg = await rpc.request('configGet', { target: selectedTarget });
  cfgOriginal = cfg;
  cfgEdited = deepClone(cfg);
  dirty = new Set();
  updateDirtyLabel();

  clearConfigForm();
  renderConfigNode($('configForm'), cfgEdited, '');
}

async function sendAllConfig() {
  if (!selectedTarget || !cfgEdited) return;
  const res = await rpc.request('configSetAll', { target: selectedTarget, config: cfgEdited });
  setRaw(res);
  dirty = new Set();
  updateDirtyLabel();
  await refreshConfig();
}

// ---------------- Signals UI ----------------

function clearSignals() {
  $('signalsList').textContent = '';
}

function inputForArg(typeName, initial) {
  const t = (typeName || '').toLowerCase();
  if (t.includes('bool')) {
    const i = document.createElement('input');
    i.type = 'checkbox';
    i.checked = !!initial;
    return { el: i, get: () => !!i.checked };
  }
  if (t.includes('double') || t.includes('float')) {
    const i = document.createElement('input');
    i.type = 'number'; i.step = 'any';
    i.value = (initial ?? 0);
    return { el: i, get: () => parseFloat(i.value || '0') };
  }
  if (t.includes('uint64') || t.includes('__int64') || t.includes('unsigned long long')) {
    const i = document.createElement('input');
    i.type = 'number'; i.step = '1';
    i.value = (initial ?? 0);
    return { el: i, get: () => String(Math.max(0, parseInt(i.value || '0', 10))) };
  }
  if (t.includes('int')) {
    const i = document.createElement('input');
    i.type = 'number'; i.step = '1';
    i.value = (initial ?? 0);
    return { el: i, get: () => parseInt(i.value || '0', 10) };
  }
  const i = document.createElement('input');
  i.type = 'text';
  i.style.minWidth = '180px';
  i.value = (initial ?? '');
  return { el: i, get: () => i.value };
}

function renderSignalEntry(container, entry) {
  const row = document.createElement('div');
  row.className = 'cfgRow';

  const name = entry.signal || '';
  const args = entry.args || [];

  const lab = document.createElement('label');
  lab.textContent = name;
  row.appendChild(lab);

  const argGets = [];
  for (let i = 0; i < args.length; i++) {
    const spec = inputForArg(args[i], '');
    row.appendChild(spec.el);
    argGets.push(spec.get);
  }

  const btn = document.createElement('button');
  btn.className = 'secondary';
  btn.textContent = 'Emit';
  row.appendChild(btn);

  const status = document.createElement('div');
  status.className = 'status';
  status.textContent = '';
  row.appendChild(status);

  btn.addEventListener('click', async () => {
    status.textContent = 'sending…';
    const argsOut = argGets.map((fn) => fn());
    try {
      const res = await rpc.request('signalEmit', { target: selectedTarget, name, args: argsOut });
      status.textContent = res && res.ok ? 'ok' : 'error';
      setRaw(res);
    } catch (e) {
      status.textContent = 'error';
      setRaw({ ok: false, error: String(e && e.message ? e.message : e) });
    }
  });

  container.appendChild(row);
}

async function refreshSignals() {
  if (!selectedTarget) return;
  const list = await rpc.request('signals', { target: selectedTarget });
  clearSignals();
  const root = $('signalsList');
  for (const e of (Array.isArray(list) ? list : [])) {
    if (!e || e.kind !== 'signal') continue;
    renderSignalEntry(root, e);
  }
}

// ---------------- RPC UI ----------------

let rpcDrafts = new Map(); // key -> value

function rpcDraftKey(target, method, argIndex) {
  return `${String(target || '')}|${String(method || '')}#${String(argIndex || '')}`;
}

function captureRpcDraftsFromDom() {
  const out = new Map(rpcDrafts);
  const target = selectedTarget;
  const rows = Array.from(document.querySelectorAll('#rpcList .cfgRow[data-rpc-method]'));
  for (const row of rows) {
    const method = String(row.getAttribute('data-rpc-method') || '');
    const inputs = Array.from(row.querySelectorAll('input[data-arg-index]'));
    for (const input of inputs) {
      const idx = String(input.getAttribute('data-arg-index') || '');
      const key = rpcDraftKey(target, method, idx);
      if (input.type === 'checkbox') out.set(key, !!input.checked);
      else out.set(key, String(input.value ?? ''));
    }
  }
  return out;
}

function clearRpcs() {
  $('rpcList').textContent = '';
}

function formatRpcValue(v) {
  if (v === undefined) return '';
  if (v === null) return 'null';
  if (typeof v === 'string') return v;
  if (typeof v === 'number' || typeof v === 'boolean') return String(v);
  return safeJson(v);
}

function renderRpcEntry(container, entry) {
  const row = document.createElement('div');
  row.className = 'cfgRow';

  const method = entry.method || '';
  const args = entry.args || [];

  row.setAttribute('data-rpc-method', method);

  const lab = document.createElement('label');
  lab.textContent = method;
  row.appendChild(lab);

  const argGets = [];
  for (let i = 0; i < args.length; i++) {
    const key = rpcDraftKey(selectedTarget, method, i);
    const initial = rpcDrafts.has(key) ? rpcDrafts.get(key) : '';
    const spec = inputForArg(args[i], initial);
    spec.el.setAttribute('data-arg-index', String(i));
    const onChange = () => {
      pauseDashboardAutoRefresh(2500);
      if (spec.el.type === 'checkbox') rpcDrafts.set(key, !!spec.el.checked);
      else rpcDrafts.set(key, String(spec.el.value ?? ''));
    };
    spec.el.addEventListener(spec.el.type === 'checkbox' ? 'change' : 'input', onChange);
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
  pre.style.marginLeft = '270px';
  pre.style.maxHeight = '240px';
  pre.textContent = '';
  pre.style.display = 'none';
  container.appendChild(pre);

  btn.addEventListener('click', async () => {
    status.textContent = 'calling…';
    pre.textContent = '';
    pre.style.display = 'none';
    const argsOut = argGets.map((fn) => fn());
    try {
      const res = await rpc.request('rpcCall', { target: selectedTarget, method, args: argsOut });
      status.textContent = res && res.ok ? 'ok' : 'error';
      setRaw(res);
      if (res && res.ok && Object.prototype.hasOwnProperty.call(res, 'result') && res.result !== null && res.result !== undefined) {
        pre.textContent = formatRpcValue(res.result);
        pre.style.display = '';
      } else if (res && !res.ok) {
        pre.textContent = safeJson(res);
        pre.style.display = '';
      }
    } catch (e) {
      status.textContent = 'error';
      setRaw({ ok: false, error: String(e && e.message ? e.message : e) });
      pre.textContent = String(e && e.message ? e.message : e);
      pre.style.display = '';
    }
  });
}

async function refreshRpcs() {
  if (!selectedTarget) return;
  rpcDrafts = captureRpcDraftsFromDom();
  const list = await rpc.request('rpcs', { target: selectedTarget });
  clearRpcs();
  const root = $('rpcList');
  for (const e of (Array.isArray(list) ? list : [])) {
    if (!e) continue;
    renderRpcEntry(root, e);
  }
}

// ---------------- Graph UI ----------------

let graphState = {
  rawEdges: [],
  rawSig: '',
  rawDomain: '',
  rawNodesSig: '',
  nsOptions: [],
  nsSig: '',
  nodes: [],
  edges: [],
  selected: null, // { kind:'node', id } | { kind:'edge', key, edge }
  pos: new Map(), // nodeId -> { x, y }
  view: { scale: 1, tx: 0, ty: 0 }, // screen = scale*world + translate(tx,ty)
  idAliases: new Map(), // unqualifiedId -> qualifiedId (best-effort)
  layoutDomain: '',
  dragging: false,
  svg: null
};

function defaultGraphView_() {
  return { scale: 1, tx: 0, ty: 0 };
}

function graphLeafName_(id) {
  const s = String(id || '');
  const idx = s.lastIndexOf('/');
  return idx >= 0 ? s.slice(idx + 1) : s;
}

function normalizeGraphNodeId_(id, aliasMap) {
  const s = String(id || '').trim();
  if (!s) return '';
  if (aliasMap && typeof aliasMap.get === 'function') {
    const mapped = aliasMap.get(s);
    if (mapped) return String(mapped);
  }
  return s;
}

function computeGraphIdAliases_(devs, connections) {
  const ids = new Set();
  const deviceTargetById = new Map(); // objectId -> target
  const byTarget = new Map(); // target -> objectId[]
  const connCountById = new Map(); // objectId -> count in connections

  for (const d of (Array.isArray(devs) ? devs : [])) {
    const id = d && d.object ? String(d.object).trim() : '';
    const tgt = d && d.target ? String(d.target).trim() : '';
    if (id) ids.add(id);
    if (id && tgt) {
      deviceTargetById.set(id, tgt);
      const arr = byTarget.get(tgt) || [];
      arr.push(id);
      byTarget.set(tgt, arr);
    }
  }
  for (const c of (Array.isArray(connections) ? connections : [])) {
    if (!c || typeof c !== 'object') continue;
    const a = String(c.object || '').trim();
    const b = String(c.subObject || '').trim();
    if (a) ids.add(a);
    if (b) ids.add(b);
    if (a) connCountById.set(a, (connCountById.get(a) || 0) + 1);
    if (b) connCountById.set(b, (connCountById.get(b) || 0) + 1);
  }

  const alias = new Map();

  const pickCanonical_ = (list) => {
    const uniq = Array.from(new Set(list)).filter((x) => !!x);
    if (!uniq.length) return '';
    uniq.sort((a, b) => {
      const as = a.includes('/') ? 1 : 0;
      const bs = b.includes('/') ? 1 : 0;
      if (as !== bs) return bs - as; // prefer qualified
      if (a.length !== b.length) return b.length - a.length; // then longer
      return a.localeCompare(b);
    });
    return uniq[0];
  };

  const resolveAlias_ = (id) => {
    let cur = String(id || '');
    for (let i = 0; i < 6; i++) {
      const next = alias.get(cur);
      if (!next || next === cur) break;
      cur = String(next);
    }
    return cur;
  };

  // 1) Merge duplicate object names that refer to the same target.
  for (const [tgt, list] of byTarget.entries()) {
    const canon = pickCanonical_(list);
    if (!canon) continue;
    const uniq = Array.from(new Set(list));
    if (uniq.length < 2) continue;
    for (const id of uniq) {
      if (id && id !== canon) alias.set(id, canon);
    }
  }

  // leaf -> qualified ids (a/b -> leaf=b)
  const byLeaf = new Map();
  for (const id of ids) {
    if (!id || !id.includes('/')) continue;
    const leaf = graphLeafName_(id);
    if (!leaf) continue;
    const arr = byLeaf.get(leaf) || [];
    arr.push(id);
    byLeaf.set(leaf, arr);
  }

  // 2) Best-effort fix for inconsistent naming: unqualified -> unique qualified match.
  // Skip if it would merge two different device targets.
  for (const id of ids) {
    if (!id || id.includes('/')) continue;
    if (alias.has(id)) continue;
    const candidates = byLeaf.get(id) || [];
    const uniq = Array.from(new Set(candidates)).filter((x) => !!x);
    if (!uniq.length) continue;

    const tA = deviceTargetById.get(id);
    const knownTargets = uniq
      .map((c) => ({ id: c, tgt: deviceTargetById.get(c) || '' }))
      .filter((x) => !!x.tgt);
    if (tA && knownTargets.length && !knownTargets.some((x) => x.tgt === tA)) {
      // If the unqualified id is a real device, and every qualified candidate points
      // to another device target, don't merge.
      continue;
    }

    const preferred = tA ? uniq.filter((c) => deviceTargetById.get(c) === tA) : [];
    const pool = preferred.length ? preferred : uniq;

    // Rank candidates (prefer device ids, then highest connection count).
    let best = '';
    let bestScore = -Infinity;
    for (const cand of pool) {
      const isDevice = deviceTargetById.has(cand) ? 1 : 0;
      const cnt = connCountById.get(cand) || 0;
      const score = isDevice * 1_000_000 + cnt * 100 + (cand.includes('/') ? 1_000 : 0) + cand.length;
      if (score > bestScore || (score === bestScore && cand.localeCompare(best) < 0)) {
        best = cand;
        bestScore = score;
      }
    }
    if (!best) continue;
    alias.set(id, resolveAlias_(best));
  }
  return alias;
}

function migrateGraphPositions_(aliasMap) {
  if (!aliasMap || typeof aliasMap.get !== 'function' || !aliasMap.size) return;
  for (const [from, to] of aliasMap.entries()) {
    const a = String(from || '').trim();
    const b = String(to || '').trim();
    if (!a || !b || a === b) continue;
    if (graphState.pos.has(a) && !graphState.pos.has(b)) {
      graphState.pos.set(b, graphState.pos.get(a));
    }
    if (graphState.pos.has(a)) graphState.pos.delete(a);
    if (graphState.selected && graphState.selected.kind === 'node' && graphState.selected.id === a) {
      graphState.selected.id = b;
    }
  }
}

function normalizeGraphView_(view) {
  const v = (view && typeof view === 'object') ? view : {};
  const scaleIn = Number(v.scale);
  const txIn = Number(v.tx);
  const tyIn = Number(v.ty);
  return {
    scale: Number.isFinite(scaleIn) ? clamp(scaleIn, 0.15, 8) : 1,
    tx: Number.isFinite(txIn) ? clamp(txIn, -200000, 200000) : 0,
    ty: Number.isFinite(tyIn) ? clamp(tyIn, -200000, 200000) : 0
  };
}

function graphViewMatrix_(view) {
  const v = normalizeGraphView_(view);
  return `matrix(${v.scale} 0 0 ${v.scale} ${v.tx} ${v.ty})`;
}

function updateGraphZoomUi_() {
  const el = $('graphZoom');
  if (!el) return;
  const s = normalizeGraphView_(graphState.view).scale;
  el.textContent = `${Math.round(s * 100)}%`;
}

function applyGraphViewToDom_() {
  const svg = graphState.svg;
  if (!svg) return;
  const vp = svg.querySelector('g[data-viewport]');
  if (!vp) return;
  const v = normalizeGraphView_(graphState.view);
  graphState.view = v;
  vp.setAttribute('transform', graphViewMatrix_(v));
  updateGraphZoomUi_();
}

function serializeGraphLayout_() {
  const nodes = {};
  for (const [id, p] of graphState.pos.entries()) {
    if (!id || !p) continue;
    const x = Number(p.x);
    const y = Number(p.y);
    if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
    nodes[String(id)] = { x: Math.round(x * 10) / 10, y: Math.round(y * 10) / 10 };
  }
  return {
    version: 1,
    domain: String(selectedDomain || ''),
    updatedMs: nowMs(),
    nodes,
    view: normalizeGraphView_(graphState.view)
  };
}

async function loadGraphLayout_(domain) {
  const d = String(domain || '').trim();
  graphState.layoutDomain = d;
  graphState.pos = new Map();
  graphState.view = defaultGraphView_();

  if (!d) return;

  let res = null;
  try {
    res = await rpc.request('graphLayoutGet', { domain: d });
  } catch (_) {
    return;
  }

  const layout = res && res.layout && typeof res.layout === 'object' ? res.layout : null;
  if (!layout) return;

  const nodes = (layout.nodes && typeof layout.nodes === 'object') ? layout.nodes : {};
  for (const k of Object.keys(nodes)) {
    const p = nodes[k];
    if (!p || typeof p !== 'object') continue;
    const x = Number(p.x);
    const y = Number(p.y);
    if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
    graphState.pos.set(String(k), { x, y });
  }

  graphState.view = normalizeGraphView_(layout.view);
}

async function saveGraphLayout_(domain) {
  const d = String(domain || '').trim();
  if (!d) return;
  await rpc.request('graphLayoutSet', { domain: d, layout: serializeGraphLayout_() });
}

const saveGraphLayoutDebounced_ = debounce((domain) => {
  const d = String(domain || '').trim();
  if (!d) return;
  saveGraphLayout_(d).catch(() => {});
}, 350);

function scheduleSaveGraphLayout_(domain) {
  const d = String(domain || selectedDomain || '').trim();
  saveGraphLayoutDebounced_(d);
}

function splitNsObject(id) {
  const s = String(id || '');
  const idx = s.indexOf('/');
  if (idx < 0) return { ns: '', name: s };
  return { ns: s.slice(0, idx), name: s.slice(idx + 1) };
}

function edgeKindFromSignal(sig) {
  const s = String(sig || '');
  if (!s) return 'unknown';
  if (s.startsWith('__cfg__|') || s.startsWith('__config__|')) return 'config';
  if (s.startsWith('__rpc__|') || s.startsWith('__rpc_ret__|')) return 'rpc';
  if (s.startsWith('__')) return 'internal';
  return 'signal';
}

function edgeLabelFromSignal(sig) {
  const s = String(sig || '');
  if (s.startsWith('__cfg__|')) return s.slice('__cfg__|'.length);
  if (s.startsWith('__config__|')) return s.slice('__config__|'.length);
  return s;
}

function edgeKey(e) {
  return `${e.kind}|${e.pub}|${e.signal}|${e.sub}`;
}

function nodeLabel(nodeId) {
  const { ns, name } = splitNsObject(nodeId);
  const selNs = String($('selGraphNs').value || '');
  if (selNs && ns === selNs && name) return name;
  return String(nodeId);
}

function shortEdgeLabel(label) {
  const s = String(label || '');
  if (s.length <= 28) return s;
  return s.slice(0, 26) + '…';
}

function readGraphFilters() {
  return {
    ns: String($('selGraphNs').value || ''),
    showSignals: !!$('chkShowGraphSignals').checked,
    showConfig: !!$('chkShowGraphConfig').checked
  };
}

function updateGraphNsSelect() {
  const sel = $('selGraphNs');
  const sig = graphState.nsOptions.join('\n');
  if (sig === graphState.nsSig && sel.options.length) return;

  graphState.nsSig = sig;

  const prev = String(sel.value || '');
  sel.textContent = '';
  sel.appendChild(new Option('(all)', ''));
  for (const ns of graphState.nsOptions) sel.appendChild(new Option(ns, ns));
  if (prev && graphState.nsOptions.includes(prev)) sel.value = prev;
  else sel.value = '';
}

function graphAllNodes() {
  const set = new Set();
  const alias = graphState.idAliases && typeof graphState.idAliases.get === 'function' ? graphState.idAliases : null;
  for (const d of devices) {
    const id = normalizeGraphNodeId_(d && d.object ? d.object : '', alias);
    if (id) set.add(id);
  }
  for (const e of graphState.rawEdges) {
    if (!e) continue;
    if (e.pub) set.add(String(e.pub));
    if (e.sub) set.add(String(e.sub));
  }
  return Array.from(set);
}

function applyGraphFilters() {
  const f = readGraphFilters();

  const edges = [];
  for (const e of graphState.rawEdges) {
    if (e.kind === 'signal' && !f.showSignals) continue;
    if (e.kind === 'config' && !f.showConfig) continue;

    if (f.ns) {
      const aNs = splitNsObject(e.pub).ns;
      const bNs = splitNsObject(e.sub).ns;
      if (aNs !== f.ns || bNs !== f.ns) continue;
    }

    edges.push(e);
  }

  const nodeSet = new Set();
  for (const n of graphAllNodes()) {
    if (!f.ns) nodeSet.add(n);
    else if (splitNsObject(n).ns === f.ns) nodeSet.add(n);
  }

  const nodes = Array.from(nodeSet).sort((a, b) => String(a).localeCompare(String(b)));

  // Preserve selection if possible.
  if (graphState.selected && graphState.selected.kind === 'edge') {
    const exists = edges.some((e) => edgeKey(e) === graphState.selected.key);
    if (!exists) graphState.selected = null;
  }
  if (graphState.selected && graphState.selected.kind === 'node') {
    const exists = nodes.includes(graphState.selected.id);
    if (!exists) graphState.selected = null;
  }

  graphState.nodes = nodes;
  graphState.edges = edges;

  $('graphSelected').textContent = graphState.selected ? safeJson(graphState.selected) : '';
}

function buildGraphFromConnections(connections, domain, aliasMap) {
  const rawEdges = [];
  const nsSet = new Set();
  const seen = new Set();
  const alias = aliasMap && typeof aliasMap.get === 'function' ? aliasMap : null;

  const list = Array.isArray(connections) ? connections : [];
  for (const c of list) {
    if (!c || typeof c !== 'object') continue;
    const sig = String(c.signal || '');
    const pub = normalizeGraphNodeId_(c.object, alias);
    const sub = normalizeGraphNodeId_(c.subObject, alias);
    if (!sig || !pub || !sub) continue;

    const kind = edgeKindFromSignal(sig);
    if (kind !== 'signal' && kind !== 'config') continue; // graph only shows connections + config
    if (kind === 'config' && pub === sub) continue; // config self-loop is noisy in the graph

    const k = `${kind}|${pub}|${sig}|${sub}`;
    if (seen.has(k)) continue;
    seen.add(k);

    const aNs = splitNsObject(pub).ns;
    const bNs = splitNsObject(sub).ns;
    if (aNs) nsSet.add(aNs);
    if (bNs) nsSet.add(bNs);

    rawEdges.push({
      pub,
      sub,
      kind,
      label: edgeLabelFromSignal(sig),
      signal: sig,
      domain
    });
  }

  const nsOptions = Array.from(nsSet).sort((a, b) => a.localeCompare(b));
  return { rawEdges, nsOptions };
}

function graphRawSignature(rawEdges) {
  return (Array.isArray(rawEdges) ? rawEdges : [])
    .map((e) => edgeKey(e))
    .sort((a, b) => a.localeCompare(b))
    .join('\n');
}

function graphNodesSignature(devs, rawEdges, aliasMap) {
  const alias = aliasMap && typeof aliasMap.get === 'function' ? aliasMap : null;
  const set = new Set();
  for (const d of (Array.isArray(devs) ? devs : [])) {
    const id = normalizeGraphNodeId_(d && d.object ? d.object : '', alias);
    if (id) set.add(id);
  }
  for (const e of (Array.isArray(rawEdges) ? rawEdges : [])) {
    if (!e) continue;
    if (e.pub) set.add(normalizeGraphNodeId_(e.pub, alias));
    if (e.sub) set.add(normalizeGraphNodeId_(e.sub, alias));
  }
  return Array.from(set).sort((a, b) => a.localeCompare(b)).join('\n');
}

function circleLayout(nodes, w, h) {
  const cx = w / 2;
  const cy = h / 2;
  const r = Math.min(w, h) * 0.38;
  const out = new Map();
  const n = nodes.length || 1;
  for (let i = 0; i < nodes.length; i++) {
    const a = (Math.PI * 2 * i) / n - Math.PI / 2;
    out.set(nodes[i], { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) });
  }
  return out;
}

function ensureGraphPositions(w, h, opts) {
  const reset = !!(opts && opts.reset);
  if (reset) graphState.pos = new Map();

  const nodes = graphState.nodes;
  const missing = nodes.filter((n) => !graphState.pos.has(n));
  if (!missing.length) return;

  const layout = circleLayout(missing, w, h);
  for (const n of missing) {
    const p = layout.get(n);
    if (p) graphState.pos.set(n, p);
  }

  // Persist auto-assigned positions so the layout is stable across sessions.
  scheduleSaveGraphLayout_();
}

function clientToSvg(svg, clientX, clientY) {
  const pt = svg.createSVGPoint();
  pt.x = clientX;
  pt.y = clientY;
  const ctm = svg.getScreenCTM();
  if (!ctm) return { x: 0, y: 0 };
  const p = pt.matrixTransform(ctm.inverse());
  return { x: p.x, y: p.y };
}

function clientToWorld(svg, clientX, clientY) {
  const p = clientToSvg(svg, clientX, clientY);
  const v = normalizeGraphView_(graphState.view);
  return {
    x: (p.x - v.tx) / v.scale,
    y: (p.y - v.ty) / v.scale
  };
}

function edgeColor(kind) {
  const styles = getComputedStyle(document.documentElement);
  const cfg = (styles.getPropertyValue('--vscode-charts-blue') || '').trim() || '#4e8cff';
  const sig = (styles.getPropertyValue('--vscode-charts-orange') || '').trim() || '#f0a44b';
  return kind === 'config' ? cfg : sig;
}

function edgeGeometry(a, b) {
  if (!a || !b) return { d: '', label: { x: 0, y: 0 } };

  // self-loop (Blueprint-ish)
  if (Math.abs(a.x - b.x) < 0.001 && Math.abs(a.y - b.y) < 0.001) {
    const r = 26;
    const sx = a.x;
    const sy = a.y;
    const c1x = sx + r;
    const c1y = sy - r;
    const c2x = sx + r * 1.6;
    const c2y = sy + r;
    const ex = sx;
    const ey = sy + 0.1;
    const d = `M ${sx.toFixed(1)} ${sy.toFixed(1)} C ${c1x.toFixed(1)} ${c1y.toFixed(1)} ${c2x.toFixed(1)} ${c2y.toFixed(1)} ${ex.toFixed(1)} ${ey.toFixed(1)}`;
    return { d, label: { x: sx + r * 1.6 + 6, y: sy } };
  }

  const dx = b.x - a.x;
  const adx = Math.abs(dx);
  const cBase = clamp(adx * 0.5, 40, 240);
  const cBack = (dx < 0) ? clamp(adx * 0.18, 40, 200) : 0; // more "loop" when going backwards
  const c = cBase + cBack;
  const c1x = a.x + c;
  const c1y = a.y;
  const c2x = b.x - c;
  const c2y = b.y;
  const d = `M ${a.x.toFixed(1)} ${a.y.toFixed(1)} C ${c1x.toFixed(1)} ${c1y.toFixed(1)} ${c2x.toFixed(1)} ${c2y.toFixed(1)} ${b.x.toFixed(1)} ${b.y.toFixed(1)}`;

  // Cubic Bezier point at t=0.5
  const t = 0.5;
  const mt = 1 - t;
  const x = (mt * mt * mt) * a.x
    + 3 * (mt * mt) * t * c1x
    + 3 * mt * (t * t) * c2x
    + (t * t * t) * b.x;
  const y = (mt * mt * mt) * a.y
    + 3 * (mt * mt) * t * c1y
    + 3 * mt * (t * t) * c2y
    + (t * t * t) * b.y;
  return { d, label: { x, y } };
}

function clamp(v, min, max) {
  return Math.max(min, Math.min(max, v));
}

function clearGraph() {
  $('graph').textContent = '';
  $('graphEdgeList').textContent = '';
  graphState.svg = null;
  graphState.dragging = false;
  graphDragging = false;
}

function updateGraphSelectionStyles() {
  const svg = graphState.svg;
  if (!svg) return;

  const sel = graphState.selected;
  const selectedNodeId = (sel && sel.kind === 'node') ? String(sel.id || '') : '';
  const selectedEdgeKey = (sel && sel.kind === 'edge') ? String(sel.key || '') : '';

  svg.querySelectorAll('g[data-node-id]').forEach((g) => {
    const id = String(g.getAttribute('data-node-id') || '');
    const active = !!selectedNodeId && id === selectedNodeId;
    const rect = g.querySelector('rect[data-node-rect]');
    if (rect) {
      const baseW = Number(rect.getAttribute('data-base-stroke-width') || '1') || 1;
      rect.setAttribute('stroke-width', String(active ? baseW + 1 : baseW));
      rect.setAttribute('opacity', active ? '1' : '0.95');
      rect.setAttribute('fill', active
        ? 'color-mix(in srgb, var(--accent) 14%, var(--card2) 86%)'
        : 'color-mix(in srgb, var(--card2) 92%, var(--bg) 8%)');
    } else {
      const circle = g.querySelector('circle');
      if (circle) {
        const baseR = Number(circle.getAttribute('data-base-r') || '9') || 9;
        circle.setAttribute('r', String(active ? baseR + 2 : baseR));
        circle.setAttribute('stroke-width', active ? '2' : '1');
        circle.setAttribute('opacity', active ? '1' : '0.95');
      }
    }
  });

  svg.querySelectorAll('path[data-edge-key]').forEach((p) => {
    const k = String(p.getAttribute('data-edge-key') || '');
    const active = !!selectedEdgeKey && k === selectedEdgeKey;
    const baseW = Number(p.getAttribute('data-base-width') || '1.6') || 1.6;
    const baseOp = String(p.getAttribute('data-base-opacity') || '0.82');
    p.setAttribute('stroke-width', String(active ? baseW + 1 : baseW));
    p.setAttribute('opacity', active ? '1' : baseOp);
  });

  svg.querySelectorAll('text[data-edge-key]').forEach((t) => {
    const k = String(t.getAttribute('data-edge-key') || '');
    const active = !!selectedEdgeKey && k === selectedEdgeKey;
    t.setAttribute('font-weight', active ? '700' : '500');
    t.setAttribute('opacity', active ? '1' : '0.9');
  });

  qsa('#graphEdgeList .edgeItem').forEach((el) => {
    const k = String(el.getAttribute('data-edge-key') || '');
    el.classList.toggle('selected', !!selectedEdgeKey && k === selectedEdgeKey);
  });
}

function setGraphSelected(sel) {
  graphState.selected = sel;
  $('graphSelected').textContent = sel ? safeJson(sel) : '';
  pauseDashboardAutoRefresh(1500);
  updateGraphSelectionStyles();
}

function selectGraphNode(nodeId) {
  setGraphSelected({ kind: 'node', id: String(nodeId || '') });
}

function selectGraphEdge(edge) {
  setGraphSelected({ kind: 'edge', key: edgeKey(edge), edge });
}

function renderGraph() {
  clearGraph();
  const root = $('graph');
  const w = root.clientWidth || 800;
  const h = root.clientHeight || 520;

  const nodes = graphState.nodes;
  const edges = graphState.edges;

  $('graphNodes').textContent = String(nodes.length);
  $('graphEdges').textContent = String(edges.length);

  ensureGraphPositions(w, h);
  const pos = graphState.pos;

  const svgNS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(svgNS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
  svg.setAttribute('preserveAspectRatio', 'none');
  svg.style.touchAction = 'none';

  const styles = getComputedStyle(document.documentElement);
  const gridStroke = (styles.getPropertyValue('--fg') || '').trim()
    || (styles.getPropertyValue('--muted') || '').trim()
    || (styles.getPropertyValue('--border') || '').trim()
    || '#555';
  const gridMinorOpacity = '0.12';
  const gridMajorOpacity = '0.22';

  const defs = document.createElementNS(svgNS, 'defs');

  // Arrow marker (uses edge stroke color).
  const marker = document.createElementNS(svgNS, 'marker');
  marker.setAttribute('id', 'arrow');
  marker.setAttribute('viewBox', '0 0 10 10');
  marker.setAttribute('refX', '9.5');
  marker.setAttribute('refY', '5');
  marker.setAttribute('markerWidth', '6');
  marker.setAttribute('markerHeight', '6');
  marker.setAttribute('orient', 'auto');
  marker.setAttribute('markerUnits', 'strokeWidth');
  const markerPath = document.createElementNS(svgNS, 'path');
  markerPath.setAttribute('d', 'M 0 0 L 10 5 L 0 10 z');
  markerPath.setAttribute('fill', 'context-stroke');
  marker.appendChild(markerPath);
  defs.appendChild(marker);

  // Node shadow for better depth.
  const nodeShadow = document.createElementNS(svgNS, 'filter');
  nodeShadow.setAttribute('id', 'nodeShadow');
  nodeShadow.setAttribute('x', '-20%');
  nodeShadow.setAttribute('y', '-20%');
  nodeShadow.setAttribute('width', '140%');
  nodeShadow.setAttribute('height', '140%');
  const nodeShadowDrop = document.createElementNS(svgNS, 'feDropShadow');
  nodeShadowDrop.setAttribute('dx', '0');
  nodeShadowDrop.setAttribute('dy', '2');
  nodeShadowDrop.setAttribute('stdDeviation', '2');
  nodeShadowDrop.setAttribute('flood-color', '#000');
  nodeShadowDrop.setAttribute('flood-opacity', '0.35');
  nodeShadow.appendChild(nodeShadowDrop);
  defs.appendChild(nodeShadow);

  // Blueprint-ish grid (world space).
  const patSmall = document.createElementNS(svgNS, 'pattern');
  patSmall.setAttribute('id', 'gridSmall');
  patSmall.setAttribute('width', '16');
  patSmall.setAttribute('height', '16');
  patSmall.setAttribute('patternUnits', 'userSpaceOnUse');
  const patSmallDot = document.createElementNS(svgNS, 'circle');
  patSmallDot.setAttribute('cx', '8');
  patSmallDot.setAttribute('cy', '8');
  patSmallDot.setAttribute('r', '1');
  patSmallDot.setAttribute('fill', gridStroke);
  patSmallDot.setAttribute('opacity', gridMinorOpacity);
  patSmall.appendChild(patSmallDot);
  defs.appendChild(patSmall);

  const patLarge = document.createElementNS(svgNS, 'pattern');
  patLarge.setAttribute('id', 'gridLarge');
  patLarge.setAttribute('width', '80');
  patLarge.setAttribute('height', '80');
  patLarge.setAttribute('patternUnits', 'userSpaceOnUse');
  const patLargeRect = document.createElementNS(svgNS, 'rect');
  patLargeRect.setAttribute('width', '80');
  patLargeRect.setAttribute('height', '80');
  patLargeRect.setAttribute('fill', 'url(#gridSmall)');
  patLarge.appendChild(patLargeRect);
  const patLargeDot = document.createElementNS(svgNS, 'circle');
  patLargeDot.setAttribute('cx', '40');
  patLargeDot.setAttribute('cy', '40');
  patLargeDot.setAttribute('r', '1.6');
  patLargeDot.setAttribute('fill', gridStroke);
  patLargeDot.setAttribute('opacity', gridMajorOpacity);
  patLarge.appendChild(patLargeDot);
  const patLargePath = document.createElementNS(svgNS, 'path');
  patLargePath.setAttribute('d', 'M 80 0 L 0 0 0 80');
  patLargePath.setAttribute('fill', 'none');
  patLargePath.setAttribute('stroke', gridStroke);
  patLargePath.setAttribute('stroke-width', '1.2');
  patLargePath.setAttribute('opacity', gridMajorOpacity);
  patLarge.appendChild(patLargePath);
  defs.appendChild(patLarge);

  svg.appendChild(defs);

  const viewportG = document.createElementNS(svgNS, 'g');
  viewportG.setAttribute('data-viewport', '1');
  viewportG.setAttribute('transform', graphViewMatrix_(graphState.view));
  svg.appendChild(viewportG);

  const gridRect = document.createElementNS(svgNS, 'rect');
  gridRect.setAttribute('x', '-100000');
  gridRect.setAttribute('y', '-100000');
  gridRect.setAttribute('width', '200000');
  gridRect.setAttribute('height', '200000');
  gridRect.setAttribute('fill', 'url(#gridLarge)');
  gridRect.setAttribute('data-graph-bg', '1');
  viewportG.appendChild(gridRect);

  const edgeG = document.createElementNS(svgNS, 'g');
  const edgeLabelG = document.createElementNS(svgNS, 'g');
  const nodeG = document.createElementNS(svgNS, 'g');
  viewportG.appendChild(edgeG);
  viewportG.appendChild(edgeLabelG);
  viewportG.appendChild(nodeG);

  const nodeStroke = getComputedStyle(document.documentElement).getPropertyValue('--border') || '#888';
  const nodeFill = 'color-mix(in srgb, var(--card2) 92%, var(--bg) 8%)';
  const nodeHeaderFill = 'color-mix(in srgb, var(--accent) 12%, var(--card2) 88%)';
  const portSpacing = 16;
  const headerH = 22;
  const minNodeH = 34;
  const minNodeW = 90;
  const maxNodeW = 200;
  const portPadY = 10;

  const outByNode = new Map();
  const inByNode = new Map();
  for (const n of nodes) {
    outByNode.set(n, []);
    inByNode.set(n, []);
  }
  for (const e of edges) {
    const pub = String(e.pub || '');
    const sub = String(e.sub || '');
    if (pub) (outByNode.get(pub) || outByNode.set(pub, []).get(pub)).push(e);
    if (sub) (inByNode.get(sub) || inByNode.set(sub, []).get(sub)).push(e);
  }

  function edgeOrder(a, b) {
    const ak = (a.kind === 'signal') ? 0 : 1;
    const bk = (b.kind === 'signal') ? 0 : 1;
    if (ak !== bk) return ak - bk;
    const al = String(a.label || '');
    const bl = String(b.label || '');
    const c = al.localeCompare(bl);
    if (c) return c;
    const ap = String(a.pub || '');
    const bp = String(b.pub || '');
    if (ap !== bp) return ap.localeCompare(bp);
    const as = String(a.sub || '');
    const bs = String(b.sub || '');
    return as.localeCompare(bs);
  }

  const nodeInfo = new Map(); // nodeId -> { w, h, outs, ins, outIndex, inIndex }
  for (const n of nodes) {
    const outs = (outByNode.get(n) || []).slice().sort(edgeOrder);
    const ins = (inByNode.get(n) || []).slice().sort(edgeOrder);
    const maxPorts = Math.max(outs.length, ins.length);
    const label = nodeLabel(n);
    const wNode = clamp(34 + label.length * 7, minNodeW, maxNodeW);
    const hNode = Math.max(minNodeH, headerH + (maxPorts * portSpacing) + portPadY);
    const outIndex = new Map();
    const inIndex = new Map();
    outs.forEach((e, i) => outIndex.set(edgeKey(e), i));
    ins.forEach((e, i) => inIndex.set(edgeKey(e), i));
    nodeInfo.set(n, { w: wNode, h: hNode, outs, ins, outIndex, inIndex });
  }

  function portAnchor(nodeId, edge, dir) {
    const p = pos.get(nodeId);
    const info = nodeInfo.get(nodeId);
    if (!p || !info) return p || { x: 0, y: 0 };
    const key = edgeKey(edge);
    const idx = (dir === 'out') ? info.outIndex.get(key) : info.inIndex.get(key);
    const i = (idx === undefined || idx === null) ? 0 : Number(idx);
    const y0 = (-info.h / 2) + headerH + (portSpacing / 2);
    const y = y0 + i * portSpacing;
    const x = (dir === 'out') ? (info.w / 2) : (-info.w / 2);
    return { x: p.x + x, y: p.y + y };
  }

  const edgeElsByNode = new Map(); // nodeId -> edgeEl[]
  const nodeEls = new Map(); // nodeId -> g

  function addEdgeRef(nodeId, edgeEl) {
    const k = String(nodeId || '');
    if (!k) return;
    const arr = edgeElsByNode.get(k) || [];
    arr.push(edgeEl);
    edgeElsByNode.set(k, arr);
  }

  function updateEdgeEl(edgeEl) {
    const a = portAnchor(edgeEl.edge.pub, edgeEl.edge, 'out');
    const b = portAnchor(edgeEl.edge.sub, edgeEl.edge, 'in');
    if (!a || !b) return;
    const geom = edgeGeometry(a, b);
    edgeEl.path.setAttribute('d', geom.d);
    if (edgeEl.label) {
      edgeEl.label.setAttribute('x', geom.label.x.toFixed(1));
      edgeEl.label.setAttribute('y', geom.label.y.toFixed(1));
    }
  }

  function updateEdgesForNode(nodeId) {
    const arr = edgeElsByNode.get(String(nodeId || ''));
    if (!arr) return;
    for (const edgeEl of arr) updateEdgeEl(edgeEl);
  }

  // edges + labels
  for (const e of edges) {
    const a = portAnchor(e.pub, e, 'out');
    const b = portAnchor(e.sub, e, 'in');
    if (!a || !b) continue;

    const key = edgeKey(e);
    const color = edgeColor(e.kind);
    const baseW = (e.kind === 'config') ? 2.2 : 1.6;
    const baseOp = (e.kind === 'config') ? 0.9 : 0.82;

    const geom = edgeGeometry(a, b);

    const path = document.createElementNS(svgNS, 'path');
    path.setAttribute('d', geom.d);
    path.setAttribute('fill', 'none');
    path.setAttribute('stroke', color);
    path.setAttribute('stroke-width', String(baseW));
    path.setAttribute('stroke-linecap', 'round');
    path.setAttribute('stroke-linejoin', 'round');
    path.setAttribute('vector-effect', 'non-scaling-stroke');
    path.setAttribute('opacity', String(baseOp));
    if (e.kind === 'config') path.setAttribute('stroke-dasharray', '6 4');
    path.setAttribute('marker-end', 'url(#arrow)');
    path.setAttribute('data-edge-key', key);
    path.setAttribute('data-base-width', String(baseW));
    path.setAttribute('data-base-opacity', String(baseOp));
    path.style.cursor = 'pointer';
    path.addEventListener('click', (ev) => {
      ev.stopPropagation();
      selectGraphEdge(e);
    });
    edgeG.appendChild(path);

    let labelEl = null;
    const label = shortEdgeLabel(e.label);
    if (label) {
      const t = document.createElementNS(svgNS, 'text');
      t.textContent = label;
      t.setAttribute('x', geom.label.x.toFixed(1));
      t.setAttribute('y', geom.label.y.toFixed(1));
      t.setAttribute('text-anchor', 'middle');
      t.setAttribute('dominant-baseline', 'middle');
      t.setAttribute('font-size', '10');
      t.setAttribute('fill', color);
      t.setAttribute('data-edge-key', key);
      t.style.cursor = 'pointer';
      t.style.paintOrder = 'stroke';
      t.style.stroke = getComputedStyle(document.documentElement).getPropertyValue('--card2') || '#000';
      t.style.strokeWidth = '4px';
      t.addEventListener('click', (ev) => {
        ev.stopPropagation();
        selectGraphEdge(e);
      });
      edgeLabelG.appendChild(t);
      labelEl = t;
    }

    const edgeEl = { edge: e, key, path, label: labelEl };
    addEdgeRef(e.pub, edgeEl);
    addEdgeRef(e.sub, edgeEl);
  }

  // nodes (draggable)
  let drag = null; // { nodeId, pointerId, ox, oy, startClientX, startClientY, moved }
  let pan = null; // { pointerId, startPt, startView, startClientX, startClientY, moved }
  for (const n of nodes) {
    const p = pos.get(n);
    if (!p) continue;

    const g = document.createElementNS(svgNS, 'g');
    g.setAttribute('transform', `translate(${p.x.toFixed(1)},${p.y.toFixed(1)})`);
    g.setAttribute('data-node-id', n);
    g.style.cursor = 'grab';

    const info = nodeInfo.get(n) || { w: minNodeW, h: minNodeH, outs: [], ins: [], outIndex: new Map(), inIndex: new Map() };

    const rect = document.createElementNS(svgNS, 'rect');
    rect.setAttribute('x', (-info.w / 2).toFixed(1));
    rect.setAttribute('y', (-info.h / 2).toFixed(1));
    rect.setAttribute('width', info.w.toFixed(1));
    rect.setAttribute('height', info.h.toFixed(1));
    rect.setAttribute('rx', '8');
    rect.setAttribute('ry', '8');
    rect.setAttribute('fill', nodeFill);
    rect.setAttribute('stroke', nodeStroke);
    rect.setAttribute('stroke-width', '1');
    rect.setAttribute('vector-effect', 'non-scaling-stroke');
    rect.setAttribute('filter', 'url(#nodeShadow)');
    rect.setAttribute('data-node-rect', '1');
    rect.setAttribute('data-base-stroke-width', '1');
    g.appendChild(rect);

    const header = document.createElementNS(svgNS, 'rect');
    header.setAttribute('x', (-info.w / 2).toFixed(1));
    header.setAttribute('y', (-info.h / 2).toFixed(1));
    header.setAttribute('width', info.w.toFixed(1));
    header.setAttribute('height', headerH.toFixed(1));
    header.setAttribute('rx', '8');
    header.setAttribute('ry', '8');
    header.setAttribute('fill', nodeHeaderFill);
    header.setAttribute('opacity', '0.95');
    g.appendChild(header);

    const title = document.createElementNS(svgNS, 'text');
    title.setAttribute('x', '0');
    title.setAttribute('y', (-info.h / 2 + 15).toFixed(1));
    title.setAttribute('text-anchor', 'middle');
    title.setAttribute('fill', getComputedStyle(document.body).color);
    title.setAttribute('font-size', '12');
    title.setAttribute('font-weight', '600');
    title.textContent = nodeLabel(n);
    g.appendChild(title);

    const divider = document.createElementNS(svgNS, 'line');
    divider.setAttribute('x1', (-info.w / 2 + 6).toFixed(1));
    divider.setAttribute('x2', (info.w / 2 - 6).toFixed(1));
    divider.setAttribute('y1', (-info.h / 2 + headerH).toFixed(1));
    divider.setAttribute('y2', (-info.h / 2 + headerH).toFixed(1));
    divider.setAttribute('stroke', nodeStroke);
    divider.setAttribute('vector-effect', 'non-scaling-stroke');
    divider.setAttribute('opacity', '0.35');
    g.appendChild(divider);

    // left: slots (incoming), right: signals (outgoing)
    const y0 = (-info.h / 2) + headerH + (portSpacing / 2);

    for (let i = 0; i < info.ins.length; i++) {
      const e = info.ins[i];
      const y = y0 + i * portSpacing;
      const color = edgeColor(e.kind);

      const port = document.createElementNS(svgNS, 'circle');
      port.setAttribute('cx', (-info.w / 2).toFixed(1));
      port.setAttribute('cy', y.toFixed(1));
      port.setAttribute('r', '3');
      port.setAttribute('fill', color);
      port.setAttribute('opacity', '0.92');
      g.appendChild(port);

      const lab = document.createElementNS(svgNS, 'text');
      lab.setAttribute('x', (-info.w / 2 - 6).toFixed(1));
      lab.setAttribute('y', y.toFixed(1));
      lab.setAttribute('text-anchor', 'end');
      lab.setAttribute('dominant-baseline', 'middle');
      lab.setAttribute('font-size', '10');
      lab.setAttribute('fill', color);
      lab.textContent = shortEdgeLabel(e.label);
      g.appendChild(lab);
    }

    for (let i = 0; i < info.outs.length; i++) {
      const e = info.outs[i];
      const y = y0 + i * portSpacing;
      const color = edgeColor(e.kind);

      const port = document.createElementNS(svgNS, 'circle');
      port.setAttribute('cx', (info.w / 2).toFixed(1));
      port.setAttribute('cy', y.toFixed(1));
      port.setAttribute('r', '3');
      port.setAttribute('fill', color);
      port.setAttribute('opacity', '0.92');
      g.appendChild(port);

      const lab = document.createElementNS(svgNS, 'text');
      lab.setAttribute('x', (info.w / 2 + 6).toFixed(1));
      lab.setAttribute('y', y.toFixed(1));
      lab.setAttribute('text-anchor', 'start');
      lab.setAttribute('dominant-baseline', 'middle');
      lab.setAttribute('font-size', '10');
      lab.setAttribute('fill', color);
      lab.textContent = shortEdgeLabel(e.label);
      g.appendChild(lab);
    }

    g.addEventListener('pointerdown', (ev) => {
      if (ev.pointerType === 'mouse' && ev.button !== 0) return;
      ev.preventDefault();
      ev.stopPropagation();
      pauseDashboardAutoRefresh(2500);

      const cur = pos.get(n) || { x: 0, y: 0 };
      const pt = clientToWorld(svg, ev.clientX, ev.clientY);
      drag = {
        nodeId: n,
        pointerId: ev.pointerId,
        ox: cur.x - pt.x,
        oy: cur.y - pt.y,
        startClientX: ev.clientX,
        startClientY: ev.clientY,
        moved: false
      };
      graphState.dragging = true;
      graphDragging = true;
      selectGraphNode(n);
      svg.setPointerCapture(ev.pointerId);
      g.style.cursor = 'grabbing';
    });

    nodeG.appendChild(g);
    nodeEls.set(n, g);
  }

  svg.addEventListener('pointermove', (ev) => {
    if (drag && ev.pointerId === drag.pointerId) {
      ev.preventDefault();
      pauseDashboardAutoRefresh(2500);

      const dx = ev.clientX - drag.startClientX;
      const dy = ev.clientY - drag.startClientY;
      if (!drag.moved && (dx * dx + dy * dy) > 9) drag.moved = true;

      const pt = clientToWorld(svg, ev.clientX, ev.clientY);
      const x = pt.x + drag.ox;
      const y = pt.y + drag.oy;
      pos.set(drag.nodeId, { x, y });

      const g = nodeEls.get(drag.nodeId);
      if (g) g.setAttribute('transform', `translate(${x.toFixed(1)},${y.toFixed(1)})`);
      updateEdgesForNode(drag.nodeId);
      return;
    }

    if (pan && ev.pointerId === pan.pointerId) {
      ev.preventDefault();
      pauseDashboardAutoRefresh(2500);

      const dx = ev.clientX - pan.startClientX;
      const dy = ev.clientY - pan.startClientY;
      if (!pan.moved && (dx * dx + dy * dy) > 9) pan.moved = true;

      const pt = clientToSvg(svg, ev.clientX, ev.clientY);
      const start = pan.startPt;
      const base = normalizeGraphView_(pan.startView);
      graphState.view = {
        scale: base.scale,
        tx: base.tx + (pt.x - start.x),
        ty: base.ty + (pt.y - start.y)
      };
      applyGraphViewToDom_();
    }
  });

  function stopDrag(ev) {
    if (!drag || ev.pointerId !== drag.pointerId) return;
    ev.preventDefault();
    pauseDashboardAutoRefresh(800);
    graphState.dragging = false;
    graphDragging = false;

    const g = nodeEls.get(drag.nodeId);
    if (g) g.style.cursor = 'grab';

    const moved = drag.moved;
    const nodeId = drag.nodeId;
    drag = null;

    try { svg.releasePointerCapture(ev.pointerId); } catch {}

    // If it was a click (no drag), sync selection to target.
    if (!moved) {
      const alias = graphState.idAliases && typeof graphState.idAliases.get === 'function' ? graphState.idAliases : null;
      const d = devices.find((x) => normalizeGraphNodeId_(x && x.object ? x.object : '', alias) === nodeId);
      if (d && d.target) {
        selectedTarget = String(d.target);
        $('selTarget').value = selectedTarget;
        selectedDevice = d;
        updateMetaFromSelected_();
        refreshConfig().catch(() => {});
        refreshSignals().catch(() => {});
        refreshRpcs().catch(() => {});
      }
    } else {
      saveGraphLayout_(selectedDomain).catch(() => {});
    }
  }

  function stopPan(ev) {
    if (!pan || ev.pointerId !== pan.pointerId) return;
    ev.preventDefault();
    pauseDashboardAutoRefresh(800);
    graphState.dragging = false;
    graphDragging = false;

    svg.classList.remove('panning');

    const moved = pan.moved;
    pan = null;

    try { svg.releasePointerCapture(ev.pointerId); } catch {}

    if (!moved) {
      setGraphSelected(null);
    } else {
      saveGraphLayout_(selectedDomain).catch(() => {});
    }
  }

  function stopAny(ev) {
    stopDrag(ev);
    stopPan(ev);
  }

  svg.addEventListener('pointerup', stopAny);
  svg.addEventListener('pointercancel', stopAny);
  svg.addEventListener('lostpointercapture', () => {
    graphState.dragging = false;
    graphDragging = false;
    drag = null;
    pan = null;
    svg.classList.remove('panning');
  });

  svg.addEventListener('pointerdown', (ev) => {
    if (drag || pan) return;
    if (ev.pointerType === 'mouse' && ev.button !== 0) return;

    const isBg = (ev.target === svg) || (ev.target && ev.target.getAttribute && ev.target.getAttribute('data-graph-bg') === '1');
    if (!isBg) return;

    ev.preventDefault();
    pauseDashboardAutoRefresh(2500);

    const pt = clientToSvg(svg, ev.clientX, ev.clientY);
    pan = {
      pointerId: ev.pointerId,
      startPt: pt,
      startView: normalizeGraphView_(graphState.view),
      startClientX: ev.clientX,
      startClientY: ev.clientY,
      moved: false
    };
    graphState.dragging = true;
    graphDragging = true;
    svg.classList.add('panning');
    svg.setPointerCapture(ev.pointerId);
  });

  svg.addEventListener('wheel', (ev) => {
    ev.preventDefault();
    pauseDashboardAutoRefresh(1200);

    const v = normalizeGraphView_(graphState.view);

    // Trackpad pinch is delivered as Ctrl+wheel in Chromium.
    const zoomMode = !!(ev.ctrlKey || ev.metaKey);
    if (zoomMode) {
      const pt = clientToSvg(svg, ev.clientX, ev.clientY);
      const wx = (pt.x - v.tx) / v.scale;
      const wy = (pt.y - v.ty) / v.scale;

      // Smooth exponential zoom.
      const factor = Math.exp(-Number(ev.deltaY || 0) * 0.0015);
      const scale = clamp(v.scale * factor, 0.15, 8);
      const tx = pt.x - wx * scale;
      const ty = pt.y - wy * scale;
      graphState.view = { scale, tx, ty };
    } else {
      // Wheel pans the view.
      graphState.view = {
        scale: v.scale,
        tx: v.tx - Number(ev.deltaX || 0),
        ty: v.ty - Number(ev.deltaY || 0)
      };
    }

    applyGraphViewToDom_();
    scheduleSaveGraphLayout_();
  }, { passive: false });

  root.appendChild(svg);
  graphState.svg = svg;
  applyGraphViewToDom_();

  const list = $('graphEdgeList');
  const top = edges.slice(0, 60);
  for (const e of top) {
    const div = document.createElement('div');
    div.className = 'edgeItem';
    div.setAttribute('data-edge-key', edgeKey(e));
    div.style.borderLeft = `4px solid ${edgeColor(e.kind)}`;
    div.textContent = `${e.pub}  --${e.label}-->  ${e.sub}`;
    div.addEventListener('click', () => selectGraphEdge(e));
    list.appendChild(div);
  }
  if (edges.length > top.length) {
    const div = document.createElement('div');
    div.className = 'muted';
    div.textContent = `… ${edges.length - top.length} more`;
    list.appendChild(div);
  }

  updateGraphSelectionStyles();
}

async function refreshGraph(opts) {
  if (!selectedDomain) return;
  const domain = selectedDomain;

  if (domain !== graphState.layoutDomain) {
    await loadGraphLayout_(domain);
  }

  const conns = await rpc.request('connections', { domain });
  const aliases = computeGraphIdAliases_(devices, conns);
  graphState.idAliases = aliases;
  migrateGraphPositions_(aliases);

  const g = buildGraphFromConnections(conns, domain, aliases);
  const sig = graphRawSignature(g.rawEdges);
  const nodesSig = graphNodesSignature(devices, g.rawEdges, aliases);
  const force = !!(opts && opts.force);

  if (!force && domain === graphState.rawDomain && sig === graphState.rawSig && nodesSig === graphState.rawNodesSig) return;

  graphState.rawDomain = domain;
  graphState.rawSig = sig;
  graphState.rawNodesSig = nodesSig;
  graphState.rawEdges = g.rawEdges;
  graphState.nsOptions = g.nsOptions;
  updateGraphNsSelect();
  applyGraphFilters();
  renderGraph();
}

// ---------------- Actions ----------------

async function attachSelected() {
  if (!selectedTarget) return;
  const res = await rpc.request('attach', { target: selectedTarget });
  setRaw(res);
}

async function killSelectedTarget() {
  if (!selectedTarget) return;
  const res = await rpc.request('killTarget', { target: selectedTarget });
  setRaw(res);
  await refreshAll();
}

async function killSelectedDomain() {
  if (!selectedDomain) return;
  const res = await rpc.request('killDomain', { domain: selectedDomain });
  setRaw(res);
  await refreshAll();
}

function bindUi() {
  // tabs
  qsa('.tabBtn').forEach((b) => {
    b.addEventListener('click', () => setTab(b.getAttribute('data-tab')));
  });

  $('bridgeState').addEventListener('click', () => refreshAll().catch((e) => setRaw({ ok: false, error: String(e && e.message ? e.message : e) })));
  $('btnRefresh').addEventListener('click', () => refreshAll().catch((e) => setRaw({ ok: false, error: String(e && e.message ? e.message : e) })));
  $('btnOpenBridge').addEventListener('click', () => rpc.request('openBridge', {}).catch(() => {}));

  $('selDomain').addEventListener('change', async () => {
    selectedDomain = String($('selDomain').value || '');
    await refreshDevicesForDomain();
    await refreshGraph();
  });
  $('selTarget').addEventListener('change', async () => {
    selectedTarget = String($('selTarget').value || '');
    selectedDevice = devices.find((d) => String(d.target || '') === selectedTarget) || null;
    updateMetaFromSelected_();
    await refreshConfig();
    await refreshSignals();
    await refreshRpcs();
  });

  $('btnAttach').addEventListener('click', () => attachSelected().catch((e) => setRaw({ ok: false, error: String(e && e.message ? e.message : e) })));
  $('btnKillTarget').addEventListener('click', () => killSelectedTarget().catch((e) => setRaw({ ok: false, error: String(e && e.message ? e.message : e) })));
  $('btnKillDomain').addEventListener('click', () => killSelectedDomain().catch((e) => setRaw({ ok: false, error: String(e && e.message ? e.message : e) })));

  $('btnReloadGraph').addEventListener('click', () => refreshGraph({ force: true }).catch(() => {}));
  $('btnResetLayout').addEventListener('click', async () => {
    pauseDashboardAutoRefresh(1500);
    graphState.pos = new Map();
    graphState.view = defaultGraphView_();
    updateGraphZoomUi_();
    if (selectedDomain) {
      await rpc.request('graphLayoutClear', { domain: selectedDomain }).catch(() => {});
    }
    renderGraph();
  });

  const zoomCenter_ = (factor) => {
    const root = $('graph');
    const w = root.clientWidth || 800;
    const h = root.clientHeight || 520;
    const v = normalizeGraphView_(graphState.view);
    const cx = w / 2;
    const cy = h / 2;
    const wx = (cx - v.tx) / v.scale;
    const wy = (cy - v.ty) / v.scale;
    const scale = clamp(v.scale * factor, 0.15, 8);
    graphState.view = {
      scale,
      tx: cx - wx * scale,
      ty: cy - wy * scale
    };
    applyGraphViewToDom_();
    scheduleSaveGraphLayout_();
  };

  const fitGraph_ = () => {
    const root = $('graph');
    const w = root.clientWidth || 800;
    const h = root.clientHeight || 520;
    const ids = graphState.nodes;
    if (!ids.length) return;

    // Conservative bounds (approx node size), good enough for "Fit".
    const estW = 240;
    const estH = 160;
    let minX = Infinity;
    let minY = Infinity;
    let maxX = -Infinity;
    let maxY = -Infinity;
    for (const id of ids) {
      const p = graphState.pos.get(id);
      if (!p) continue;
      minX = Math.min(minX, p.x - estW / 2);
      maxX = Math.max(maxX, p.x + estW / 2);
      minY = Math.min(minY, p.y - estH / 2);
      maxY = Math.max(maxY, p.y + estH / 2);
    }
    if (!Number.isFinite(minX) || !Number.isFinite(minY) || !Number.isFinite(maxX) || !Number.isFinite(maxY)) return;

    const pad = 30;
    const ww = Math.max(10, maxX - minX);
    const wh = Math.max(10, maxY - minY);
    const scale = clamp(Math.min((w - pad * 2) / ww, (h - pad * 2) / wh), 0.15, 8);

    graphState.view = {
      scale,
      tx: pad - minX * scale,
      ty: pad - minY * scale
    };
    applyGraphViewToDom_();
    scheduleSaveGraphLayout_();
  };

  $('btnZoomIn').addEventListener('click', () => zoomCenter_(1.2));
  $('btnZoomOut').addEventListener('click', () => zoomCenter_(1 / 1.2));
  $('btnFitGraph').addEventListener('click', () => fitGraph_());
  $('btnResetView').addEventListener('click', () => {
    graphState.view = defaultGraphView_();
    applyGraphViewToDom_();
    scheduleSaveGraphLayout_();
  });
  const rerenderGraph = debounce(() => {
    pauseDashboardAutoRefresh(1500);
    applyGraphFilters();
    renderGraph();
  }, 60);
  $('selGraphNs').addEventListener('change', rerenderGraph);
  $('chkShowGraphSignals').addEventListener('change', rerenderGraph);
  $('chkShowGraphConfig').addEventListener('change', rerenderGraph);

  $('btnReloadConfig').addEventListener('click', () => refreshConfig().catch(() => {}));
  $('btnSendAllConfig').addEventListener('click', () => sendAllConfig().catch((e) => setRaw({ ok: false, error: String(e && e.message ? e.message : e) })));

  $('btnReloadSignals').addEventListener('click', () => refreshSignals().catch(() => {}));
  $('btnReloadRpcs').addEventListener('click', () => refreshRpcs().catch(() => {}));

  $('btnRawApps').addEventListener('click', async () => setRaw(await rpc.request('apps', {})));
  $('btnRawDevices').addEventListener('click', async () => setRaw(await rpc.request('devices', { domain: selectedDomain })));
  $('btnRawSignals').addEventListener('click', async () => setRaw(await rpc.request('signals', { target: selectedTarget })));
  $('btnRawRpcs').addEventListener('click', async () => setRaw(await rpc.request('rpcs', { target: selectedTarget })));
  $('btnRawConnections').addEventListener('click', async () => setRaw(await rpc.request('connections', { domain: selectedDomain })));
}

let graphResizeObserver_ = null;
const rerenderGraphOnResize_ = debounce(() => {
  const root = $('graph');
  if (!root) return;
  const w = root.clientWidth || 0;
  const h = root.clientHeight || 0;
  if (w < 10 || h < 10) return;
  if (!graphState.svg) return;
  renderGraph();
}, 80);

function ensureGraphResizeObserver_() {
  if (graphResizeObserver_ || typeof ResizeObserver === 'undefined') return;
  const root = $('graph');
  if (!root) return;
  graphResizeObserver_ = new ResizeObserver(() => rerenderGraphOnResize_());
  graphResizeObserver_.observe(root);
}

bindUi();
ensureGraphResizeObserver_();
vscode.postMessage({ type: 'ready' });
refreshAll().catch((e) => {
  setBridgeState('bridge: error', false);
  setRaw({ ok: false, error: String(e && e.message ? e.message : e) });
});
