/* global acquireVsCodeApi */
(function () {
  const vscode = acquireVsCodeApi();

  const $ = (id) => document.getElementById(id);

  const state = {
    launchers: [],
    selectedAbsPath: '',
    selectedConfig: null,
    dirty: false,
    loadingConfig: false,
    busy: false,
    filterText: ''
  };

  function setStatus(text) {
    $('status').textContent = String(text || '');
  }

  function setBusy(on) {
    state.busy = !!on;
    $('btnRefresh').disabled = state.busy;
    const btnNewProj = $('btnNewProject');
    if (btnNewProj) btnNewProj.disabled = state.busy;
    const btnNew = $('btnNewLaunch');
    if (btnNew) btnNew.disabled = state.busy;
    const btnDoctor = $('btnDoctor');
    if (btnDoctor) btnDoctor.disabled = state.busy;
    const btnDashboard = $('btnDashboard');
    if (btnDashboard) btnDashboard.disabled = state.busy;
    const btnOutput = $('btnOutput');
    if (btnOutput) btnOutput.disabled = state.busy;
    const filter = $('launcherFilter');
    if (filter) filter.disabled = state.busy;
    const ds = document.querySelectorAll('[data-action]');
    for (const el of ds) el.disabled = state.busy;
  }

  const pending = new Map();
  function request(action, params) {
    const id = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
    return new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject, action });
      vscode.postMessage({ type: 'request', id, action, params: params || {} });
    });
  }

  window.addEventListener('message', (ev) => {
    const msg = ev.data;
    if (!msg || typeof msg !== 'object') return;
    if (msg.type !== 'response') return;
    const id = String(msg.id || '');
    const p = pending.get(id);
    if (!p) return;
    pending.delete(id);
    if (msg.ok) p.resolve(msg.data);
    else p.reject(new Error(String(msg.error || 'unknown error')));
  });

  function renderLauncherList() {
    const host = $('launcherList');
    host.textContent = '';

    const list = Array.isArray(state.launchers) ? state.launchers : [];
    const q = String(state.filterText || '').trim().toLowerCase();
    const filtered = q
      ? list.filter((m) => {
        const p = String(m.relPath || m.absPath || '').toLowerCase();
        const s = String(m.sys || '').toLowerCase();
        return p.includes(q) || s.includes(q);
      })
      : list;
    $('launcherCount').textContent = list.length ? (q ? `${filtered.length}/${list.length}` : `${list.length}`) : '';

    if (!filtered.length) {
      const empty = document.createElement('div');
      empty.className = 'muted';
      empty.textContent = q ? '(no launchers match filter)' : '(no launchers found)';
      host.appendChild(empty);
      return;
    }

    for (const m of filtered) {
      const item = document.createElement('div');
      item.className = 'listItem' + (m.absPath === state.selectedAbsPath ? ' selected' : '');
      item.tabIndex = 0;

      const row1 = document.createElement('div');
      row1.className = 'row1';
      const name = document.createElement('div');
      name.className = 'name';
      name.textContent = m.relPath || m.absPath || '(launcher)';
      const badge = document.createElement('div');
      badge.className = 'badge';
      badge.textContent = m.running ? `running ${m.pid || ''}`.trim() : 'stopped';
      row1.appendChild(name);
      row1.appendChild(badge);

      const row2 = document.createElement('div');
      row2.className = 'row2';
      row2.textContent = `${m.sys ? `sys=${m.sys}` : 'sys=?'}  nodes=${m.nodesCount || 0}  containers=${m.containersCount || 0}`;

      item.appendChild(row1);
      item.appendChild(row2);

      item.addEventListener('click', () => selectLauncher(m.absPath));
      item.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') selectLauncher(m.absPath);
      });

      host.appendChild(item);
    }
  }

  function renderDetails() {
    const host = $('details');
    host.textContent = '';

    const abs = state.selectedAbsPath;
    const meta = state.launchers.find((x) => x.absPath === abs);
    if (!abs || !meta) {
      const cards = document.createElement('div');
      cards.className = 'cards';

      const c1 = document.createElement('div');
      c1.className = 'card';
      const h1 = document.createElement('div');
      h1.className = 'cardHeader';
      h1.appendChild(cardTitle_('How it works'));
      c1.appendChild(h1);
      const p1 = document.createElement('div');
      p1.className = 'muted';
      p1.textContent = 'Pick a *.launch.json on the left. The folder that contains it is the "project root" used by New node..., Build, and Build + Run.';
      c1.appendChild(p1);
      const quick = document.createElement('div');
      quick.className = 'actions';
      quick.appendChild(actionButtonMini_('New project…', 'secondary', () => doNewProject_()));
      quick.appendChild(actionButtonMini_('New launch.json', 'secondary', () => doNewLaunch_()));
      quick.appendChild(actionButtonMini_('Diagnostics', 'secondary', () => doDiagnostics_()));
      quick.appendChild(actionButtonMini_('Dashboard', 'secondary', () => doOpenDashboard_()));
      c1.appendChild(quick);

      const c2 = document.createElement('div');
      c2.className = 'card';
      const h2 = document.createElement('div');
      h2.className = 'cardHeader';
      h2.appendChild(cardTitle_('Project structure'));
      c2.appendChild(h2);
      const pre = document.createElement('pre');
      pre.textContent = [
        '<MyProject>/',
        '  <MyProject>.launch.json',
        '  src/',
        '    <CMakeProjects>/   (nodes, libs, plugins...)',
        '      CMakeLists.txt',
        '      deps.depend      (1 path per dependency)',
        '  install/             (artifacts copied by SwBuild)',
        '    bin/ lib/ plugins/',
        '  log/'
      ].join('\\n');
      c2.appendChild(pre);
      const p2 = document.createElement('div');
      p2.className = 'muted';
      p2.textContent = 'New node… generates CMake + C++ skeletons: standalone node = <Target>.h/.cpp + <Target>Main.cpp, component = <Component>.h/.cpp + <Plugin>.cpp.';
      c2.appendChild(p2);

      cards.appendChild(c1);
      cards.appendChild(c2);
      host.appendChild(cards);
      return;
    }

    if (state.loadingConfig) {
      const loading = document.createElement('div');
      loading.className = 'muted';
      loading.textContent = 'Loading...';
      host.appendChild(loading);
      return;
    }

    const header = document.createElement('div');
    header.className = 'detailsHeader';
    const cfg = state.selectedConfig && typeof state.selectedConfig === 'object' ? state.selectedConfig : null;
    const sysLabel = cfg && cfg.sys ? String(cfg.sys) : (meta.sys ? String(meta.sys) : '');
    const title = document.createElement('div');
    title.className = 'name';
    title.textContent = sysLabel ? `sys=${sysLabel}` : 'sys=?';
    const path = document.createElement('div');
    path.className = 'path';
    path.textContent = meta.relPath || meta.absPath;
    header.appendChild(title);
    header.appendChild(path);

    const dirOf_ = (p) => {
      const s = String(p || '');
      const i1 = s.lastIndexOf('/');
      const i2 = s.lastIndexOf('\\');
      const i = Math.max(i1, i2);
      return i >= 0 ? s.slice(0, i) : '';
    };
    const join2_ = (base, child) => {
      const b = String(base || '').replace(/[\\/]+$/g, '');
      const sep = b.includes('\\') ? '\\' : '/';
      return b ? `${b}${sep}${child}` : String(child || '');
    };
    const projectDirAbs = dirOf_(meta.absPath);
    const hints = document.createElement('div');
    if (projectDirAbs) {
      const h1 = document.createElement('div');
      h1.className = 'hintLine';
      h1.textContent = `Project folder: ${projectDirAbs}`;
      hints.appendChild(h1);

      const h2 = document.createElement('div');
      h2.className = 'hintLine';
      h2.textContent = `Generated code: ${join2_(projectDirAbs, 'src')}   Build output: ${join2_(projectDirAbs, 'install')}`;
      hints.appendChild(h2);
    }

    const actions = document.createElement('div');
    actions.className = 'actions';
    actions.appendChild(actionButton(meta.running ? 'Stop' : 'Start', meta.running ? 'stopLauncher' : 'startLauncher'));
    actions.appendChild(actionButton('Open JSON', 'openFile', 'secondary'));
    actions.appendChild(actionButton('Reveal folder', 'revealProject', 'secondary'));
    actions.appendChild(actionButton('Open install/bin', 'openInstallBin', 'secondary'));
    actions.appendChild(actionButton('Open log', 'openLog', 'secondary'));
    actions.appendChild(actionButton('Reset', 'resetConfig', 'secondary'));
    actions.appendChild(actionButton('Save', 'saveConfig'));
    actions.appendChild(actionButton('New node…', 'createNodeWizard', 'secondary'));
    actions.appendChild(actionButton('Build', 'buildProject', 'secondary'));
    actions.appendChild(actionButton('Build + Run', 'buildAndRun'));

    if (!cfg) {
      const hint = document.createElement('div');
      hint.className = 'muted';
      hint.textContent = 'No config loaded.';
      host.appendChild(header);
      if (hints.childNodes.length) host.appendChild(hints);
      host.appendChild(actions);
      host.appendChild(hint);
      return;
    }

    const form = document.createElement('div');
    form.className = 'form';
    form.appendChild(formRow('sys', textInput(String(cfg.sys || ''), (v) => { cfg.sys = v; state.dirty = true; updateSaveState_(); })));
    form.appendChild(formRow('duration_ms', numberInput(cfg.duration_ms, (v) => { cfg.duration_ms = v; state.dirty = true; updateSaveState_(); })));

    const containersHeader = document.createElement('div');
    containersHeader.className = 'sectionHeader';
    containersHeader.textContent = 'Containers';
    const containers = Array.isArray(cfg.containers) ? cfg.containers : [];
    const containersList = document.createElement('div');
    containersList.className = 'sectionBody';
    containersList.appendChild(listEditor_(
      containers,
      (c, idx) => renderContainer_(cfg, c, idx),
      () => {
        if (!Array.isArray(cfg.containers)) cfg.containers = [];
        cfg.containers.push(defaultContainer_(cfg));
        state.dirty = true;
        updateSaveState_();
        renderDetails();
      },
      'Add container'
    ));

    const nodesHeader = document.createElement('div');
    nodesHeader.className = 'sectionHeader';
    nodesHeader.textContent = 'Nodes';
    const nodes = Array.isArray(cfg.nodes) ? cfg.nodes : [];
    const nodesList = document.createElement('div');
    nodesList.className = 'sectionBody';
    nodesList.appendChild(listEditor_(
      nodes,
      (n, idx) => renderNode_(cfg, n, idx),
      () => onAction('createNodeWizard', { kind: 'standalone' }),
      'New node...'
    ));

    const kv = document.createElement('div');
    kv.className = 'kv';
    kv.appendChild(kvKey('file stats'));
    kv.appendChild(kvVal(`${meta.running ? 'running' : 'stopped'}  nodes=${meta.nodesCount || 0}  containers=${meta.containersCount || 0}`));

    host.appendChild(header);
    if (hints.childNodes.length) host.appendChild(hints);
    host.appendChild(actions);
    host.appendChild(form);
    host.appendChild(containersHeader);
    host.appendChild(containersList);
    host.appendChild(nodesHeader);
    host.appendChild(nodesList);
    host.appendChild(kv);
  }

  function kvKey(text) {
    const d = document.createElement('div');
    d.className = 'muted';
    d.textContent = text;
    return d;
  }

  function kvVal(text) {
    const d = document.createElement('div');
    d.textContent = text;
    return d;
  }

  function actionButton(label, action, kind) {
    const b = document.createElement('button');
    b.className = 'btn' + (kind ? ` ${kind}` : '');
    b.textContent = label;
    b.dataset.action = action;
    b.addEventListener('click', () => onAction(action));
    return b;
  }

  function updateSaveState_() {
    const save = document.querySelector('[data-action="saveConfig"]');
    if (save) save.disabled = state.busy || !state.dirty;
  }

  async function ensureConfigLoaded_() {
    const absPath = state.selectedAbsPath;
    if (!absPath) return;
    if (state.selectedConfig && !state.loadingConfig) return;
    state.loadingConfig = true;
    renderDetails();
    try {
      const res = await request('readLaunchJson', { absPath });
      state.selectedConfig = res && res.config && typeof res.config === 'object' ? res.config : {};
      state.dirty = false;
    } finally {
      state.loadingConfig = false;
      renderDetails();
      updateSaveState_();
    }
  }

  async function saveSelectedConfig_() {
    const absPath = state.selectedAbsPath;
    const cfg = state.selectedConfig;
    if (!absPath || !cfg || typeof cfg !== 'object') return;
    await request('writeLaunchJson', { absPath, config: cfg });
    state.dirty = false;
    updateSaveState_();
  }

  async function onAction(action, extraParams) {
    const absPath = state.selectedAbsPath;
    const meta = state.launchers.find((x) => x.absPath === absPath);
    if (!meta) return;
    try {
      setBusy(true);
      setStatus(`${action}...`);
      if (action === 'openFile') {
        await request('openFile', { absPath });
      } else if (action === 'revealProject') {
        await request('revealProject', { absPath });
      } else if (action === 'openInstallBin') {
        await request('openInstallBin', { absPath });
      } else if (action === 'openLog') {
        await request('openLog', { absPath });
      } else if (action === 'resetConfig') {
        state.selectedConfig = null;
        state.dirty = false;
        await ensureConfigLoaded_();
      } else if (action === 'startLauncher') {
        if (state.dirty) {
          const ok = confirm('Unsaved changes. Save before starting?');
          if (ok) await saveSelectedConfig_();
        }
        await request('startLauncher', { absPath });
      } else if (action === 'stopLauncher') {
        await request('stopLauncher', { absPath });
      } else if (action === 'saveConfig') {
        await saveSelectedConfig_();
      } else if (action === 'buildProject') {
        if (state.dirty) {
          const ok = confirm('Unsaved changes. Save before build?');
          if (ok) await saveSelectedConfig_();
        }
        await request('buildProject', { absPath });
      } else if (action === 'buildAndRun') {
        if (state.dirty) {
          const ok = confirm('Unsaved changes. Save before build + run?');
          if (ok) await saveSelectedConfig_();
        }
        await request('buildAndRun', { absPath });
      } else if (action === 'createNodeWizard') {
        if (state.dirty) {
          const ok = confirm('Unsaved changes. Save before creating a node?');
          if (ok) await saveSelectedConfig_();
        }
        const extra = (extraParams && typeof extraParams === 'object') ? extraParams : {};
        await request('createNodeWizard', Object.assign({ absPath }, extra));
        state.selectedConfig = null;
        state.dirty = false;
        await ensureConfigLoaded_();
      }
      await refreshLaunchers();
      setStatus('ok');
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    } finally {
      setBusy(false);
      updateSaveState_();
    }
  }

  async function doDiagnostics_() {
    try {
      setBusy(true);
      setStatus('diagnostics...');
      await request('diagnostics', {});
      setStatus('ok');
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    } finally {
      setBusy(false);
      updateSaveState_();
    }
  }

  async function doOpenDashboard_() {
    try {
      setBusy(true);
      setStatus('open dashboard...');
      await request('openDashboard', {});
      setStatus('ok');
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  async function doOpenOutput_() {
    try {
      setBusy(true);
      setStatus('open output...');
      await request('openOutput', {});
      setStatus('ok');
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  async function doNewLaunch_() {
    try {
      setBusy(true);
      setStatus('new launch...');
      const res = await request('createLaunchFile', {});
      await refreshLaunchers();
      const abs = res && res.absPath ? String(res.absPath) : '';
      if (abs) {
        await selectLauncher(abs);
      }
      setStatus('ok');
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    } finally {
      setBusy(false);
      updateSaveState_();
    }
  }

  async function doNewProject_() {
    try {
      setBusy(true);
      setStatus('new project...');
      const res = await request('createProject', {});
      await refreshLaunchers();
      const abs = res && res.absPath ? String(res.absPath) : '';
      if (abs) {
        await selectLauncher(abs);
      }
      setStatus('ok');
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    } finally {
      setBusy(false);
      updateSaveState_();
    }
  }

  async function refreshLaunchers() {
    const res = await request('listLaunchers', {});
    const list = Array.isArray(res && res.launchers) ? res.launchers : [];
    list.sort((a, b) => String(a.relPath || a.absPath || '').localeCompare(String(b.relPath || b.absPath || '')));
    state.launchers = list;
    if (state.selectedAbsPath && !state.launchers.some((x) => x.absPath === state.selectedAbsPath)) {
      state.selectedAbsPath = '';
      state.selectedConfig = null;
      state.dirty = false;
    }
    renderLauncherList();
    renderDetails();
    updateSaveState_();
  }

  async function selectLauncher(absPath) {
    state.selectedAbsPath = String(absPath || '');
    state.selectedConfig = null;
    state.dirty = false;
    renderLauncherList();
    renderDetails();
    try {
      await ensureConfigLoaded_();
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    }
  }

  $('btnRefresh').addEventListener('click', async () => {
    try {
      setBusy(true);
      setStatus('refresh...');
      await refreshLaunchers();
      setStatus('ok');
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  });

  $('btnNewLaunch').addEventListener('click', () => doNewLaunch_());

  const btnNewProject = $('btnNewProject');
  if (btnNewProject) {
    btnNewProject.addEventListener('click', () => doNewProject_());
  }

  const launcherFilter = $('launcherFilter');
  if (launcherFilter) {
    launcherFilter.addEventListener('input', () => {
      state.filterText = launcherFilter.value;
      renderLauncherList();
    });
  }

  const btnDoctor = $('btnDoctor');
  if (btnDoctor) btnDoctor.addEventListener('click', () => doDiagnostics_());

  const btnDashboard = $('btnDashboard');
  if (btnDashboard) btnDashboard.addEventListener('click', () => doOpenDashboard_());

  const btnOutput = $('btnOutput');
  if (btnOutput) btnOutput.addEventListener('click', () => doOpenOutput_());

  (async () => {
    try {
      setBusy(true);
      setStatus('loading...');
      await refreshLaunchers();
      setStatus('');
    } catch (e) {
      setStatus(e && e.message ? e.message : String(e));
    } finally {
      setBusy(false);
      updateSaveState_();
    }
  })();

  function formRow(label, inputEl) {
    const row = document.createElement('div');
    row.className = 'formRow';
    const k = document.createElement('div');
    k.className = 'muted';
    k.textContent = label;
    const v = document.createElement('div');
    v.appendChild(inputEl);
    row.appendChild(k);
    row.appendChild(v);
    return row;
  }

  function textInput(value, onChange) {
    const input = document.createElement('input');
    input.className = 'input';
    input.type = 'text';
    input.value = value;
    input.addEventListener('input', () => onChange(input.value));
    return input;
  }

  function numberInput(value, onChange) {
    const input = document.createElement('input');
    input.className = 'input';
    input.type = 'number';
    input.step = '1';
    input.value = Number.isFinite(Number(value)) ? String(Math.floor(Number(value))) : '0';
    input.addEventListener('input', () => {
      const n = Number(input.value);
      onChange(Number.isFinite(n) ? Math.floor(n) : 0);
    });
    return input;
  }

  function listEditor_(arr, renderItem, onAdd, addLabel) {
    const wrap = document.createElement('div');
    const list = document.createElement('div');
    list.className = 'cards';
    for (let i = 0; i < arr.length; i++) {
      list.appendChild(renderItem(arr[i], i));
    }
    const add = document.createElement('button');
    add.className = 'btn secondary';
    add.textContent = addLabel || 'Add';
    add.addEventListener('click', onAdd);
    wrap.appendChild(list);
    wrap.appendChild(add);
    return wrap;
  }

  function defaultContainer_(cfg) {
    const sys = String((cfg && cfg.sys) || 'sys');
    return {
      ns: sys,
      name: 'container',
      executable: '',
      workingDirectory: '.',
      options: {
        reloadOnCrash: true,
        reloadOnDisconnect: true,
        disconnectTimeoutMs: 5000
      },
      composition: {
        threading: 'same_thread',
        plugins: [],
        components: []
      }
    };
  }

  function defaultNode_(cfg) {
    const sys = String((cfg && cfg.sys) || 'sys');
    return {
      ns: sys,
      name: 'node',
      executable: '',
      workingDirectory: '.',
      options: {
        reloadOnCrash: true,
        reloadOnDisconnect: true,
        disconnectTimeoutMs: 5000
      },
      params: {}
    };
  }

  function renderContainer_(cfg, c, idx) {
    const card = document.createElement('div');
    card.className = 'card';
    const head = document.createElement('div');
    head.className = 'cardHeader';
    head.appendChild(cardTitle_(`Container ${idx + 1}`));
    head.appendChild(cardDangerBtn_('Remove', () => {
      if (!confirm('Remove container?')) return;
      if (!Array.isArray(cfg.containers)) cfg.containers = [];
      cfg.containers.splice(idx, 1);
      state.dirty = true;
      updateSaveState_();
      renderDetails();
    }));
    card.appendChild(head);

    card.appendChild(formRow('ns', textInput(String(c.ns || ''), (v) => { c.ns = v; state.dirty = true; updateSaveState_(); })));
    card.appendChild(formRow('name', textInput(String(c.name || ''), (v) => { c.name = v; state.dirty = true; updateSaveState_(); })));
    card.appendChild(formRow('executable', textInput(String(c.executable || ''), (v) => { c.executable = v; state.dirty = true; updateSaveState_(); })));
    card.appendChild(formRow('workingDirectory', textInput(String(c.workingDirectory || ''), (v) => { c.workingDirectory = v; state.dirty = true; updateSaveState_(); })));

    const comp = c && typeof c.composition === 'object' ? c.composition : null;
    if (!comp) {
      c.composition = { threading: 'same_thread', plugins: [], components: [] };
    }

    const compHeader = document.createElement('div');
    compHeader.className = 'subHeader';
    compHeader.textContent = 'Components';
    card.appendChild(compHeader);

    const compList = document.createElement('div');
    compList.className = 'subList';
    const components = Array.isArray(c.composition.components) ? c.composition.components : [];
    for (let i = 0; i < components.length; i++) {
      compList.appendChild(renderComponent_(cfg, c, components[i], i));
    }
    const addBtn = document.createElement('button');
    addBtn.className = 'btn secondary';
    addBtn.textContent = 'New component...';
    addBtn.addEventListener('click', () => {
      onAction('createNodeWizard', { kind: 'component', preferredContainerKey: containerKey_(c) });
    });

    const addJsonBtn = document.createElement('button');
    addJsonBtn.className = 'btn secondary';
    addJsonBtn.textContent = 'Add component (JSON)';
    addJsonBtn.addEventListener('click', () => {
      if (!Array.isArray(c.composition.components)) c.composition.components = [];
      c.composition.components.push({ type: '', ns: String(c.ns || ''), name: 'component', params: {} });
      state.dirty = true;
      updateSaveState_();
      renderDetails();
    });
    card.appendChild(compList);
    card.appendChild(addBtn);
    card.appendChild(addJsonBtn);

    return card;
  }

  function renderComponent_(cfg, container, comp, idx) {
    const row = document.createElement('div');
    row.className = 'row';
    const left = document.createElement('div');
    left.className = 'rowFields';
    left.appendChild(formRow('container', selectInput_(
      containerKey_(container),
      containerOptions_(cfg),
      (v) => {
        const nextIdx = containerIndexByKey_(cfg, v);
        if (nextIdx < 0) return;
        const next = cfg.containers[nextIdx];
        if (!next || next === container) return;
        if (!Array.isArray(container.composition.components)) container.composition.components = [];
        container.composition.components.splice(idx, 1);
        ensureContainerComposition_(next);
        if (!Array.isArray(next.composition.components)) next.composition.components = [];
        next.composition.components.push(comp);
        state.dirty = true;
        updateSaveState_();
        renderDetails();
      }
    )));
    left.appendChild(formRow('type', textInput(String(comp.type || ''), (v) => { comp.type = v; state.dirty = true; updateSaveState_(); })));
    left.appendChild(formRow('ns', textInput(String(comp.ns || ''), (v) => { comp.ns = v; state.dirty = true; updateSaveState_(); })));
    left.appendChild(formRow('name', textInput(String(comp.name || ''), (v) => { comp.name = v; state.dirty = true; updateSaveState_(); })));
    const right = document.createElement('div');
    right.className = 'rowActions';
    right.appendChild(actionButtonMini_('Make node (JSON)', 'secondary', () => {
      if (!confirm('Convert component to standalone node?')) return;
      if (!Array.isArray(cfg.nodes)) cfg.nodes = [];
      cfg.nodes.push({
        ns: String(comp.ns || ''),
        name: String(comp.name || ''),
        executable: '',
        workingDirectory: '.',
        options: {
          reloadOnCrash: true,
          reloadOnDisconnect: true,
          disconnectTimeoutMs: 5000
        },
        params: (comp && typeof comp.params === 'object' && comp.params) ? comp.params : {}
      });
      const arr = Array.isArray(container.composition.components) ? container.composition.components : [];
      arr.splice(idx, 1);
      container.composition.components = arr;
      state.dirty = true;
      updateSaveState_();
      renderDetails();
    }));
    right.appendChild(cardDangerBtn_('Remove', () => {
      if (!confirm('Remove component?')) return;
      const arr = Array.isArray(container.composition.components) ? container.composition.components : [];
      arr.splice(idx, 1);
      container.composition.components = arr;
      state.dirty = true;
      updateSaveState_();
      renderDetails();
    }));
    row.appendChild(left);
    row.appendChild(right);
    return row;
  }

  function renderNode_(cfg, n, idx) {
    const card = document.createElement('div');
    card.className = 'card';
    const head = document.createElement('div');
    head.className = 'cardHeader';
    head.appendChild(cardTitle_(`Node ${idx + 1}`));
    head.appendChild(cardDangerBtn_('Remove', () => {
      if (!confirm('Remove node?')) return;
      if (!Array.isArray(cfg.nodes)) cfg.nodes = [];
      cfg.nodes.splice(idx, 1);
      state.dirty = true;
      updateSaveState_();
      renderDetails();
    }));
    card.appendChild(head);

    card.appendChild(formRow('ns', textInput(String(n.ns || ''), (v) => { n.ns = v; state.dirty = true; updateSaveState_(); })));
    card.appendChild(formRow('name', textInput(String(n.name || ''), (v) => { n.name = v; state.dirty = true; updateSaveState_(); })));
    card.appendChild(formRow('executable', textInput(String(n.executable || ''), (v) => { n.executable = v; state.dirty = true; updateSaveState_(); })));
    card.appendChild(formRow('workingDirectory', textInput(String(n.workingDirectory || ''), (v) => { n.workingDirectory = v; state.dirty = true; updateSaveState_(); })));

    return card;
  }

  function cardTitle_(text) {
    const d = document.createElement('div');
    d.className = 'cardTitle';
    d.textContent = text;
    return d;
  }

  function cardDangerBtn_(label, onClick) {
    const b = document.createElement('button');
    b.className = 'btn danger';
    b.textContent = label;
    b.addEventListener('click', onClick);
    return b;
  }

  function actionButtonMini_(label, kind, onClick) {
    const b = document.createElement('button');
    b.className = 'btn' + (kind ? ` ${kind}` : '');
    b.textContent = label;
    b.addEventListener('click', onClick);
    return b;
  }

  function selectInput_(value, options, onChange) {
    const sel = document.createElement('select');
    sel.className = 'input';
    const opts = Array.isArray(options) ? options : [];
    for (const o of opts) {
      const opt = document.createElement('option');
      opt.value = String(o.value);
      opt.textContent = String(o.label);
      sel.appendChild(opt);
    }
    sel.value = String(value || '');
    sel.addEventListener('change', () => onChange(sel.value));
    return sel;
  }

  function ensureContainerComposition_(c) {
    if (!c || typeof c !== 'object') return;
    if (!c.composition || typeof c.composition !== 'object') {
      c.composition = { threading: 'same_thread', plugins: [], components: [] };
    }
    if (!Array.isArray(c.composition.plugins)) c.composition.plugins = [];
    if (!Array.isArray(c.composition.components)) c.composition.components = [];
  }

  function containerKey_(c) {
    const ns = c && c.ns ? String(c.ns) : '';
    const name = c && c.name ? String(c.name) : '';
    return `${ns}/${name}`;
  }

  function containerOptions_(cfg) {
    const out = [];
    const cs = Array.isArray(cfg.containers) ? cfg.containers : [];
    for (const c of cs) {
      const key = containerKey_(c);
      out.push({ value: key, label: key || '(container)' });
    }
    return out.length ? out : [{ value: '', label: '(no containers)' }];
  }

  function containerIndexByKey_(cfg, key) {
    const cs = Array.isArray(cfg.containers) ? cfg.containers : [];
    for (let i = 0; i < cs.length; i++) {
      if (containerKey_(cs[i]) === key) return i;
    }
    return -1;
  }

  function suggestComponentType_(cfg, node) {
    const sys = String((cfg && cfg.sys) || (node && node.ns) || 'sys').trim() || 'sys';
    const name = String((node && node.name) || 'Component').trim() || 'Component';
    const cap = name.length ? name[0].toUpperCase() + name.slice(1) : 'Component';
    return `${sys}/${cap}Component`;
  }

  function convertNodeToComponent_(cfg, node, idx) {
    if (!confirm('Convert node to component (composable)?')) return;
    if (!Array.isArray(cfg.containers) || cfg.containers.length === 0) {
      cfg.containers = Array.isArray(cfg.containers) ? cfg.containers : [];
      cfg.containers.push(defaultContainer_(cfg));
    }
    const container = cfg.containers[0];
    ensureContainerComposition_(container);
    container.composition.components.push({
      type: suggestComponentType_(cfg, node),
      ns: String(node.ns || ''),
      name: String(node.name || ''),
      params: (node && typeof node.params === 'object' && node.params) ? node.params : {}
    });
    if (!Array.isArray(cfg.nodes)) cfg.nodes = [];
    cfg.nodes.splice(idx, 1);
    state.dirty = true;
    updateSaveState_();
    renderDetails();
  }
})();
