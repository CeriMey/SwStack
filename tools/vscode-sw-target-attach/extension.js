const vscode = require('vscode');
const cp = require('child_process');
const crypto = require('crypto');
const fs = require('fs');
const http = require('http');
const https = require('https');
const path = require('path');
const { URL } = require('url');

let bridgeProc_ = null;
let bridgeStartedByUs_ = false;
let dashboardPanel_ = null;
let dashboardInitSelection_ = null; // { domain?: string, target?: string }
let dashboardReady_ = false;
let outputChannel_ = null;
let requestRefreshAll_ = () => {}; // set in activate()
let extensionContext_ = null; // set in activate()

// SwLaunch process management (keyed by launcher JSON file path).
const launchProcs_ = new Map(); // absPath -> { proc, meta }
const launcherCache_ = new Map(); // absPath -> { mtimeMs, meta }

function isObject_(v) {
  return v !== null && typeof v === 'object' && !Array.isArray(v);
}

function output_() {
  if (!outputChannel_) outputChannel_ = vscode.window.createOutputChannel('coreSw');
  return outputChannel_;
}

function log_(line) {
  try {
    output_().appendLine(String(line));
  } catch (_) {
    // ignore
  }
}

function showOutput_() {
  try {
    output_().show(true);
  } catch (_) {
    // ignore
  }
}

function delay_(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function workspaceFolderPath_() {
  const ws = vscode.workspace.workspaceFolders;
  if (!ws || !ws.length) return '';
  return ws[0].uri.fsPath || '';
}

function workspaceFolderPaths_() {
  const ws = vscode.workspace.workspaceFolders;
  if (!ws || !ws.length) return [];
  const out = [];
  for (const f of ws) {
    const p = f && f.uri && f.uri.fsPath ? String(f.uri.fsPath) : '';
    if (p) out.push(p);
  }
  return out;
}

function candidateRepoRoots_() {
  const roots = workspaceFolderPaths_();
  const candidates = [];

  const looksLikeRepoRoot = (dirAbs) => {
    if (!dirAbs) return false;
    try {
      if (fs.existsSync(path.join(dirAbs, 'bin'))) return true;
      if (fs.existsSync(path.join(dirAbs, 'src', 'core'))) return true;
    } catch (_) {
      // ignore
    }
    return false;
  };

  for (const r of roots) {
    if (!r) continue;
    candidates.push(r);

    // If the workspace folder doesn't look like the coreSw repo root, also try its parent.
    if (!looksLikeRepoRoot(r)) {
      const parent = path.dirname(r);
      if (parent && parent !== r && looksLikeRepoRoot(parent)) candidates.push(parent);
    }
  }

  const out = [];
  const seen = new Set();
  for (const p of candidates) {
    const k = normPathKey_(p);
    if (!k || seen.has(k)) continue;
    seen.add(k);
    out.push(p);
  }
  return out;
}

function expandWorkspaceFolder_(input) {
  if (!input || typeof input !== 'string') return '';
  const ws = workspaceFolderPath_();
  if (!ws) return input;
  return input.replace(/\$\{workspaceFolder\}/g, ws);
}

function resolveSwapiExecutable_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const raw = cfg.get('swapiPath', 'swapi');
  const swapiFromCfg = expandWorkspaceFolder_(String(raw || '').trim()) || 'swapi';

  // If the user explicitly configured a path, trust it.
  if (swapiFromCfg && swapiFromCfg !== 'swapi') return swapiFromCfg;

  const roots = candidateRepoRoots_();
  if (!roots.length) return swapiFromCfg;

  const isWin = process.platform === 'win32';
  const exe = isWin ? 'swapi.exe' : 'swapi';
  const candidates = [];
  for (const r of roots) {
    candidates.push(path.join(r, 'bin', exe));
  }
  for (const r of roots) {
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwAPI', 'SwApi', 'Release', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwAPI', 'SwApi', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwAPI', 'SwApi', 'Debug', exe));
    candidates.push(path.join(r, 'build-check', 'SwNode', 'SwAPI', 'SwApi', 'Release', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwAPI', 'SwApi', 'Release', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwAPI', 'SwApi', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwAPI', 'SwApi', 'Debug', exe));
    candidates.push(path.join(r, 'build-wsl', 'SwNode', 'SwAPI', 'SwApi', 'swapi'));
  }

  for (const p of candidates) {
    try {
      if (p && fs.existsSync(p)) return p;
    } catch (_) {
      // ignore
    }
  }

  return swapiFromCfg;
}

function resolveSwBridgeExecutable_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const raw = String(cfg.get('swBridgePath', '') || '').trim();
  const fromCfg = expandWorkspaceFolder_(raw);
  if (fromCfg) return fromCfg;

  const roots = candidateRepoRoots_();
  if (!roots.length) return '';

  const isWin = process.platform === 'win32';
  const exe = isWin ? 'SwBridge.exe' : 'SwBridge';
  const candidates = [];
  for (const r of roots) {
    candidates.push(path.join(r, 'bin', exe));
  }
  for (const r of roots) {
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwAPI', 'SwBridge', 'Release', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwAPI', 'SwBridge', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwAPI', 'SwBridge', 'Debug', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwAPI', 'SwBridge', 'Release', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwAPI', 'SwBridge', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwAPI', 'SwBridge', 'Debug', exe));
  }

  for (const p of candidates) {
    try {
      if (p && fs.existsSync(p)) return p;
    } catch (_) {
      // ignore
    }
  }

  return '';
}

function resolveSwLaunchExecutable_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const raw = String(cfg.get('swLaunchPath', '') || '').trim();
  const fromCfg = expandWorkspaceFolder_(raw);
  if (fromCfg) return fromCfg;

  const roots = candidateRepoRoots_();
  if (!roots.length) return '';

  const isWin = process.platform === 'win32';
  const exe = isWin ? 'SwLaunch.exe' : 'SwLaunch';
  const candidates = [];
  for (const r of roots) {
    candidates.push(path.join(r, 'bin', exe));
  }
  for (const r of roots) {
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwLaunch', 'Release', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwLaunch', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwLaunch', 'Debug', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwLaunch', 'Release', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwLaunch', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwLaunch', 'Debug', exe));
  }

  for (const p of candidates) {
    try {
      if (p && fs.existsSync(p)) return p;
    } catch (_) {
      // ignore
    }
  }

  return '';
}

function resolveSwBuildExecutable_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const raw = String(cfg.get('swBuildPath', '') || '').trim();
  const fromCfg = expandWorkspaceFolder_(raw);
  if (fromCfg) return fromCfg;

  const roots = candidateRepoRoots_();
  if (!roots.length) return '';

  const isWin = process.platform === 'win32';
  const exe = isWin ? 'SwBuild.exe' : 'SwBuild';
  const candidates = [];
  for (const r of roots) {
    candidates.push(path.join(r, 'bin', exe));
  }
  for (const r of roots) {
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwBuild', 'Release', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwBuild', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwBuild', 'Debug', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwBuild', 'Release', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwBuild', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwBuild', 'Debug', exe));
  }

  for (const p of candidates) {
    try {
      if (p && fs.existsSync(p)) return p;
    } catch (_) {
      // ignore
    }
  }

  return '';
}

function bridgePort_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const v = Number(cfg.get('bridgePort', 8088));
  if (!Number.isFinite(v)) return 8088;
  const p = Math.floor(v);
  if (p < 1 || p > 65535) return 8088;
  return p;
}

function bridgeBaseUrl_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const raw = String(cfg.get('bridgeUrl', '') || '').trim();
  const base = raw ? raw.replace(/\/+$/, '') : `http://127.0.0.1:${bridgePort_()}`;
  // SwBridge listens on IPv4 only (AF_INET). On many systems "localhost" resolves to ::1 first,
  // which breaks requests. Force IPv4 when host=localhost.
  try {
    const u = new URL(base);
    if (u.hostname === 'localhost') u.hostname = '127.0.0.1';
    return u.toString().replace(/\/+$/, '');
  } catch (_) {
    return base;
  }
}

function bridgeAutoStart_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  return !!cfg.get('bridgeAutoStart', true);
}

function bridgeRequestTimeoutMs_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const v = Number(cfg.get('bridgeTimeoutMs', 1500));
  if (!Number.isFinite(v)) return 1500;
  return Math.max(200, Math.floor(v));
}

function swapiTimeoutMs_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const n = cfg.get('swapiTimeoutMs', 4000);
  const v = Number(n);
  if (!Number.isFinite(v)) return 4000;
  return Math.max(100, Math.floor(v));
}

function targetsAutoRefreshMs_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const v = Number(cfg.get('autoRefreshMs', 1500));
  if (!Number.isFinite(v)) return 1500;
  const ms = Math.floor(v);
  if (ms <= 0) return 0;
  return Math.max(250, ms);
}

function dashboardAutoRefreshMs_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const v = Number(cfg.get('dashboardAutoRefreshMs', 1500));
  if (!Number.isFinite(v)) return 1500;
  const ms = Math.floor(v);
  if (ms <= 0) return 0;
  return Math.max(500, ms);
}

function launcherSearchPatterns_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const raw = cfg.get('launcherSearchPatterns', ['**/*.launch.json', '**/launch_*.json']);
  if (!Array.isArray(raw) || !raw.length) return ['**/*.launch.json', '**/launch_*.json'];
  const out = [];
  for (const x of raw) {
    const s = String(x || '').trim();
    if (s) out.push(s);
  }
  return out.length ? out : ['**/*.launch.json', '**/launch_*.json'];
}

function launcherSearchExclude_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const s = String(cfg.get('launcherSearchExclude', '**/{build,build-*,install,log}/**') || '').trim();
  return s || '**/{build,build-*,install,log}/**';
}

function launcherMaxResults_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const v = Number(cfg.get('launcherMaxResults', 200));
  if (!Number.isFinite(v)) return 200;
  return Math.max(10, Math.floor(v));
}

function httpRequest_(url, options) {
  const method = (options && options.method) ? String(options.method) : 'GET';
  const headers = Object.assign({}, (options && options.headers) ? options.headers : {});
  const body = (options && options.body !== undefined) ? options.body : undefined;
  const timeoutMs = (options && options.timeoutMs !== undefined) ? Number(options.timeoutMs) : bridgeRequestTimeoutMs_();

  return new Promise((resolve, reject) => {
    const lib = url.protocol === 'https:' ? https : http;
    const req = lib.request(url, { method, headers }, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => {
        const buf = chunks.length ? Buffer.concat(chunks) : Buffer.alloc(0);
        resolve({
          status: res.statusCode || 0,
          headers: res.headers || {},
          text: buf.toString('utf8')
        });
      });
    });
    req.on('error', reject);
    if (Number.isFinite(timeoutMs) && timeoutMs > 0) {
      req.setTimeout(timeoutMs, () => req.destroy(new Error(`timeout after ${timeoutMs}ms`)));
    }
    if (body !== undefined) req.write(body);
    req.end();
  });
}

async function bridgeRequestJson_(method, apiPath, opts) {
  const base = bridgeBaseUrl_();
  const url = new URL(base + apiPath);
  const query = (opts && opts.query) ? opts.query : undefined;
  if (query && typeof query === 'object') {
    for (const [k, v] of Object.entries(query)) {
      if (v === undefined || v === null) continue;
      url.searchParams.set(k, String(v));
    }
  }

  let bodyText;
  const headers = { Accept: 'application/json' };
  if (opts && opts.body !== undefined) {
    bodyText = JSON.stringify(opts.body);
    headers['Content-Type'] = 'application/json; charset=utf-8';
    headers['Content-Length'] = String(Buffer.byteLength(bodyText, 'utf8'));
  }

  const res = await httpRequest_(url, {
    method: String(method || 'GET'),
    headers,
    body: bodyText,
    timeoutMs: (opts && opts.timeoutMs !== undefined) ? Number(opts.timeoutMs) : bridgeRequestTimeoutMs_()
  });

  let data = null;
  const text = String(res.text || '').trim();
  if (text) {
    try {
      data = JSON.parse(text);
    } catch (e) {
      const msg = `SwBridge ${method} ${url.toString()}: invalid JSON response: ${e && e.message ? e.message : String(e)}`;
      throw new Error(`${msg}\nBody:\n${text.slice(0, 2000)}`);
    }
  }

  if (res.status >= 200 && res.status < 300) {
    return data;
  }

  const err = (data && typeof data === 'object')
    ? (data.error || data.message || data.msg || '')
    : '';
  const suffix = err ? `: ${err}` : (text ? `: ${text.slice(0, 2000)}` : '');
  throw new Error(`SwBridge ${method} ${url.toString()}: HTTP ${res.status}${suffix}`);
}

async function isBridgeAlive_() {
  try {
    await bridgeRequestJson_('GET', '/api/apps', { timeoutMs: Math.min(800, bridgeRequestTimeoutMs_()) });
    return true;
  } catch (_) {
    return false;
  }
}

async function startBridgeProcess_() {
  if (bridgeProc_ && bridgeProc_.pid && bridgeProc_.exitCode === null) return bridgeProc_;

  const exe = resolveSwBridgeExecutable_();
  if (!exe || !fs.existsSync(exe)) {
    throw new Error('SwBridge executable not found. Put it in workspace bin/ (ex: bin/SwBridge.exe) or set coreSw.swBridgePath.');
  }

  const port = bridgePort_();
  const args = [String(port)];
  const cwd = path.dirname(exe);

  log_(`[coreSw] starting SwBridge: ${exe} ${args.join(' ')}`);
  const child = cp.spawn(exe, args, { cwd, windowsHide: true });
  bridgeProc_ = child;
  bridgeStartedByUs_ = true;

  const forward = (prefix, chunk) => {
    const s = String(chunk || '');
    const lines = s.split(/\r?\n/).filter((l) => l.length);
    for (const line of lines) log_(`${prefix}${line}`);
  };

  if (child.stdout) child.stdout.on('data', (c) => forward('[SwBridge] ', c));
  if (child.stderr) child.stderr.on('data', (c) => forward('[SwBridge] ', c));

  child.on('exit', (code, signal) => {
    log_(`[coreSw] SwBridge exited (pid=${child.pid}) code=${code} signal=${signal || ''}`);
    if (bridgeProc_ === child) bridgeProc_ = null;
    bridgeStartedByUs_ = false;
  });
  child.on('error', (e) => {
    log_(`[coreSw] SwBridge spawn error: ${e && e.message ? e.message : String(e)}`);
  });

  return child;
}

async function stopBridgeProcess_() {
  const child = bridgeProc_;
  if (!child || !child.pid || child.exitCode !== null) {
    bridgeProc_ = null;
    bridgeStartedByUs_ = false;
    return false;
  }

  const pid = child.pid;
  log_(`[coreSw] stopping SwBridge pid=${pid}`);

  if (process.platform === 'win32') {
    await new Promise((resolve) => {
      cp.execFile('taskkill', ['/PID', String(pid), '/T', '/F'], { encoding: 'utf8' }, () => resolve());
    });
  } else {
    try {
      process.kill(pid, 'SIGTERM');
    } catch (_) {
      // ignore
    }
  }

  bridgeProc_ = null;
  bridgeStartedByUs_ = false;
  return true;
}

async function ensureBridgeRunning_() {
  if (await isBridgeAlive_()) return true;

  if (!bridgeAutoStart_()) {
    throw new Error(`SwBridge not reachable at ${bridgeBaseUrl_()}. Start it (SwBridge.exe ${bridgePort_()}) or enable coreSw.bridgeAutoStart.`);
  }

  await startBridgeProcess_();

  const deadline = nowMs_() + 5000;
  let lastErr = '';
  while (nowMs_() < deadline) {
    try {
      if (await isBridgeAlive_()) return true;
    } catch (e) {
      lastErr = e && e.message ? e.message : String(e);
    }
    await delay_(150);
  }

  throw new Error(`SwBridge did not become ready at ${bridgeBaseUrl_()} (last error: ${lastErr || 'unknown'})`);
}

function isProcAlive_(child) {
  return !!(child && child.pid && child.exitCode === null);
}

function getLaunchEntry_(absPath) {
  const k = normPathKey_(absPath);
  if (!k) return undefined;
  const entry = launchProcs_.get(k);
  if (!entry) return undefined;
  if (isProcAlive_(entry.proc)) return entry;
  launchProcs_.delete(k);
  return undefined;
}

async function startSwLaunch_(launcherMeta) {
  if (!launcherMeta || !launcherMeta.absPath) throw new Error('launch: missing launcher meta');
  const absPath = String(launcherMeta.absPath);
  const key = normPathKey_(absPath);
  if (!key) throw new Error('launch: invalid launcher path');

  const existing = getLaunchEntry_(absPath);
  if (existing) {
    vscode.window.showInformationMessage(`coreSw: already running: ${launcherMeta.relPath} (pid=${existing.proc.pid})`);
    return existing;
  }

  const exe = resolveSwLaunchExecutable_();
  if (!exe || !fs.existsSync(exe)) {
    throw new Error('SwLaunch executable not found. Put it in workspace bin/ (ex: bin/SwLaunch.exe) or set coreSw.swLaunchPath.');
  }

  const args = [`--config_file=${absPath}`];
  const cwd = path.dirname(absPath) || workspaceFolderPath_() || undefined;

  log_(`[coreSw] starting SwLaunch: ${exe} ${args.join(' ')}`);
  const child = cp.spawn(exe, args, { cwd, windowsHide: true });

  const forward = (prefix, chunk) => {
    const s = String(chunk || '');
    const lines = s.split(/\r?\n/).filter((l) => l.length);
    for (const line of lines) log_(`${prefix}${line}`);
  };
  if (child.stdout) child.stdout.on('data', (c) => forward('[SwLaunch] ', c));
  if (child.stderr) child.stderr.on('data', (c) => forward('[SwLaunch] ', c));

  child.on('exit', (code, signal) => {
    log_(`[coreSw] SwLaunch exited (pid=${child.pid}) code=${code} signal=${signal || ''} file=${launcherMeta.relPath}`);
    launchProcs_.delete(key);
    requestRefreshAll_();
  });
  child.on('error', (e) => {
    log_(`[coreSw] SwLaunch spawn error: ${e && e.message ? e.message : String(e)}`);
  });

  const entry = { proc: child, meta: launcherMeta };
  launchProcs_.set(key, entry);
  return entry;
}

async function stopSwLaunch_(absPath) {
  const entry = getLaunchEntry_(absPath);
  if (!entry) return false;
  const pid = entry.proc.pid;
  if (!pid) return false;

  log_(`[coreSw] stopping SwLaunch pid=${pid} file=${entry.meta && entry.meta.relPath ? entry.meta.relPath : absPath}`);
  if (process.platform === 'win32') {
    await new Promise((resolve) => {
      cp.execFile('taskkill', ['/PID', String(pid), '/T', '/F'], { encoding: 'utf8' }, () => resolve());
    });
  } else {
    try {
      process.kill(pid, 'SIGTERM');
    } catch (_) {
      // ignore
    }
  }

  launchProcs_.delete(normPathKey_(absPath));
  requestRefreshAll_();
  return true;
}

function toPosixPath_(p) {
  return String(p || '').replace(/\\/g, '/');
}

function stripExtension_(absPath) {
  if (!absPath) return '';
  const parsed = path.parse(absPath);
  if (!parsed || !parsed.dir) return parsed && parsed.name ? parsed.name : absPath;
  return path.join(parsed.dir, parsed.name);
}

function relFrom_(fromDirAbs, toAbs) {
  const rel = path.relative(fromDirAbs, toAbs);
  return toPosixPath_(rel || '');
}

function defaultProcessOptions_() {
  return { reloadOnCrash: true, reloadOnDisconnect: true, disconnectTimeoutMs: 5000 };
}

function resolveSwComponentContainerExecutable_() {
  const roots = candidateRepoRoots_();
  if (!roots.length) return '';

  const isWin = process.platform === 'win32';
  const exe = isWin ? 'SwComponentContainer.exe' : 'SwComponentContainer';
  const candidates = [];
  for (const r of roots) {
    candidates.push(path.join(r, 'bin', exe));
  }
  for (const r of roots) {
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwComponentContainer', 'Release', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwComponentContainer', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build-win', 'SwNode', 'SwComponentContainer', 'Debug', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwComponentContainer', 'Release', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwComponentContainer', 'RelWithDebInfo', exe));
    candidates.push(path.join(r, 'build', 'SwNode', 'SwComponentContainer', 'Debug', exe));
  }

  for (const p of candidates) {
    try {
      if (p && fs.existsSync(p)) return p;
    } catch (_) {
      // ignore
    }
  }
  return '';
}

function runSwBuild_(args, cwdAbs) {
  const exe = resolveSwBuildExecutable_();
  if (!exe || !fs.existsSync(exe)) {
    throw new Error('SwBuild executable not found. Put it in workspace bin/ (ex: bin/SwBuild.exe) or set coreSw.swBuildPath.');
  }
  const cwd = cwdAbs || workspaceFolderPath_() || undefined;

  return new Promise((resolve, reject) => {
    log_(`[coreSw] starting SwBuild: ${exe} ${args.join(' ')}`);
    const child = cp.spawn(exe, args, { cwd, windowsHide: true });

    const forward = (prefix, chunk) => {
      const s = String(chunk || '');
      const lines = s.split(/\r?\n/).filter((l) => l.length);
      for (const line of lines) log_(`${prefix}${line}`);
    };
    if (child.stdout) child.stdout.on('data', (c) => forward('[SwBuild] ', c));
    if (child.stderr) child.stderr.on('data', (c) => forward('[SwBuild] ', c));

    child.on('error', (e) => {
      reject(new Error(e && e.message ? e.message : String(e)));
    });
    child.on('exit', (code, signal) => {
      if (code === 0) {
        resolve({ ok: true });
        return;
      }
      reject(new Error(`SwBuild failed (code=${code} signal=${signal || ''})`));
    });
  });
}

async function scanStaticLibDeps_(srcRootAbs) {
  const out = [];
  let entries = [];
  try {
    entries = await fs.promises.readdir(srcRootAbs, { withFileTypes: true });
  } catch (_) {
    return out;
  }

  for (const e of entries) {
    if (!e || !e.isDirectory()) continue;
    const dirName = String(e.name || '').trim();
    if (!dirName || dirName.startsWith('.')) continue;
    const cmakePath = path.join(srcRootAbs, dirName, 'CMakeLists.txt');
    if (!fs.existsSync(cmakePath)) continue;
    let raw = '';
    try {
      raw = await fs.promises.readFile(cmakePath, 'utf8');
    } catch (_) {
      continue;
    }
    const m = String(raw).match(/add_library\s*\(\s*([A-Za-z0-9_]+)\s+STATIC\b/i);
    if (!m) continue;
    const libName = String(m[1] || '').trim();
    if (!libName) continue;
    out.push({ dirName, libName, dirAbs: path.join(srcRootAbs, dirName) });
  }

  out.sort((a, b) => String(a.libName).localeCompare(String(b.libName)));
  return out;
}

function validCppIdent_(s) {
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(String(s || ''));
}

function cap1_(s) {
  const t = String(s || '');
  return t.length ? t[0].toUpperCase() + t.slice(1) : t;
}

function camelFrom_(raw) {
  const parts = String(raw || '').split(/[^A-Za-z0-9]+/).filter(Boolean);
  if (!parts.length) return '';
  return parts.map((p) => cap1_(p)).join('');
}

function escapeRegExp_(s) {
  return String(s || '').replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

async function ensureEmptyDir_(dirAbs) {
  if (fs.existsSync(dirAbs)) {
    const entries = await fs.promises.readdir(dirAbs);
    if (entries && entries.length) throw new Error(`directory not empty: ${dirAbs}`);
    return;
  }
  await fs.promises.mkdir(dirAbs, { recursive: true });
}

async function ensureDir_(dirAbs) {
  if (fs.existsSync(dirAbs)) {
    let st;
    try {
      st = await fs.promises.stat(dirAbs);
    } catch (e) {
      throw new Error(`failed to stat directory: ${dirAbs} (${e && e.message ? e.message : String(e)})`);
    }
    if (!st.isDirectory()) throw new Error(`not a directory: ${dirAbs}`);
    return;
  }
  await fs.promises.mkdir(dirAbs, { recursive: true });
}

async function writeFileNoOverwrite_(absPath, content) {
  if (fs.existsSync(absPath)) throw new Error(`file already exists: ${absPath}`);
  await fs.promises.writeFile(absPath, content, 'utf8');
}

async function writeFileIfMissing_(absPath, content) {
  if (fs.existsSync(absPath)) return false;
  await fs.promises.writeFile(absPath, content, 'utf8');
  return true;
}

async function ensureFileHas_(absPath, regex, appendContent) {
  let raw = '';
  try {
    raw = await fs.promises.readFile(absPath, 'utf8');
  } catch (_) {
    return false;
  }

  const re = regex instanceof RegExp ? regex : null;
  if (!re) throw new Error('ensureFileHas_: regex must be a RegExp');
  if (re.test(raw)) return false;

  const base = String(raw || '').replace(/\s*$/, '');
  const extra = String(appendContent || '');
  const suffix = extra.endsWith('\n') ? extra : extra + '\n';
  const out = (base ? base + '\n\n' : '') + suffix;
  await fs.promises.writeFile(absPath, out, 'utf8');
  return true;
}

function cmakeIncludeCoreDirs_() {
  return [
    '    ${CORE_SW_ROOT}/src',
    '    ${CORE_SW_ROOT}/src/core',
    '    ${CORE_SW_ROOT}/src/core/runtime',
    '    ${CORE_SW_ROOT}/src/core/object',
    '    ${CORE_SW_ROOT}/src/core/types',
    '    ${CORE_SW_ROOT}/src/core/io',
    '    ${CORE_SW_ROOT}/src/core/fs',
    '    ${CORE_SW_ROOT}/src/core/platform',
    '    ${CORE_SW_ROOT}/src/core/remote'
  ];
}

function renderFindLibBlock_(targetName, deps) {
  if (!deps.length) return '';
  const lines = [];
  for (const d of deps) {
    const varName = `${String(d.libName).toUpperCase()}_LIB`;
    lines.push('find_library(' + varName + ' NAMES ' + d.libName + ' PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)');
  }
  const cond = deps.map((d) => `NOT ${String(d.libName).toUpperCase()}_LIB`).join(' OR ');
  const names = deps.map((d) => d.libName).join('/');
  lines.push(`if(${cond})`);
  lines.push('    message(FATAL_ERROR "' + names + ' not found in ${CMAKE_INSTALL_PREFIX}/lib. Build with SwBuild (check deps.depend).")');
  lines.push('endif()');
  const libs = deps.map((d) => '"${' + `${String(d.libName).toUpperCase()}_LIB` + '}"').join(' ');
  lines.push(`target_link_libraries(${targetName} PRIVATE ${libs})`);
  return lines.join('\n');
}

async function createStandaloneNodeProject_(projectRootAbs, targetName, defaultNs, defaultName, deps) {
  const srcRoot = path.join(projectRootAbs, 'src');
  await fs.promises.mkdir(srcRoot, { recursive: true });

  const projDir = path.join(srcRoot, targetName);
  await ensureDir_(projDir);

  const header = `#pragma once

#include "SwRemoteObject.h"
#include "SwSharedMemorySignal.h"

class SwTimer;

class ${targetName} : public SwRemoteObject {
public:
    ${targetName}(const SwString& sysName,
                 const SwString& nameSpace,
                 const SwString& objectName,
                 SwObject* parent = nullptr);

private:
    void tick_();

    SW_REGISTER_SHM_SIGNAL(tick, int, SwString);

    int seq_{0};
    SwTimer* timer_{nullptr};
};
`;

  const source = `#include \"${targetName}.h\"

#include \"SwDebug.h\"
#include \"SwTimer.h\"

${targetName}::${targetName}(const SwString& sysName,
                             const SwString& nameSpace,
                             const SwString& objectName,
                             SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent),
      timer_(new SwTimer(this)) {
    SwObject::connect(timer_, &SwTimer::timeout, [this]() { tick_(); });
    timer_->start(500);
}

void ${targetName}::tick_() {
    ++seq_;
    (void)emit tick(seq_, SwString(\"tick\"));
    swDebug() << \"[${targetName}]\" << objectName() << \"tick seq=\" << seq_;
}
`;

  const main = `#include \"SwRemoteObjectNode.h\"

#include \"${targetName}.h\"

SW_REMOTE_OBJECT_NODE(${targetName}, \"${defaultNs}\", \"${defaultName}\")
`;

  const depIncludeDirs = deps.map((d) => '    ${CMAKE_CURRENT_LIST_DIR}/../' + d.dirName);

  const cmake = [
    'cmake_minimum_required(VERSION 3.10)',
    `project(${targetName})`,
    '',
    'set(CMAKE_CXX_STANDARD 11)',
    'set(CMAKE_CXX_STANDARD_REQUIRED ON)',
    '',
    'if(MSVC)',
    '    add_compile_options(/Zc:preprocessor)',
    'endif()',
    '',
    // CORE_SW_ROOT can be either the project root's parent (coreSw repo) or the project root itself
    // (when opening a project folder directly in VS Code).
    'set(CORE_SW_ROOT \"${CMAKE_CURRENT_LIST_DIR}/../..\")',
    'get_filename_component(CORE_SW_ROOT \"${CORE_SW_ROOT}\" ABSOLUTE)',
    'if(NOT EXISTS \"${CORE_SW_ROOT}/src/core\")',
    '    set(CORE_SW_ROOT \"${CMAKE_CURRENT_LIST_DIR}/../../..\")',
    '    get_filename_component(CORE_SW_ROOT \"${CORE_SW_ROOT}\" ABSOLUTE)',
    'endif()',
    'if(NOT EXISTS \"${CORE_SW_ROOT}/src/core\")',
    '    message(FATAL_ERROR \"CORE_SW_ROOT not found (expected <coreSw root>/src/core).\")',
    'endif()',
    '',
    `add_executable(${targetName}`,
    `    ${targetName}.cpp`,
    `    ${targetName}Main.cpp`,
    ')',
    '',
    `target_include_directories(${targetName} PRIVATE`,
    ...cmakeIncludeCoreDirs_(),
    '    ${CMAKE_CURRENT_LIST_DIR}',
    ...depIncludeDirs,
    ')',
    '',
    renderFindLibBlock_(targetName, deps),
    '',
    'if(WIN32)',
    `    target_link_libraries(${targetName} PRIVATE crypt32)`,
    'endif()',
    '',
    'if(NOT WIN32)',
    `    target_link_libraries(${targetName} PRIVATE crypto)`,
    'endif()',
    ''
  ].filter((l) => l !== '').join('\n') + '\n';

  const depFileLines = deps.map((d) => toPosixPath_(path.relative(projDir, d.dirAbs)));
  const depFile = depFileLines.length ? depFileLines.join('\n') + '\n' : '';

  await writeFileIfMissing_(path.join(projDir, `${targetName}.h`), header);
  await writeFileIfMissing_(path.join(projDir, `${targetName}.cpp`), source);
  const mainPath = path.join(projDir, `${targetName}Main.cpp`);
  await writeFileIfMissing_(mainPath, main);
  await writeFileIfMissing_(path.join(projDir, 'CMakeLists.txt'), cmake);
  await writeFileIfMissing_(path.join(projDir, 'deps.depend'), depFile);

  // If the main file already existed, ensure the registration macro is present (required to run the node).
  const mainMacroRe = new RegExp(`\\bSW_REMOTE_OBJECT_NODE\\s*\\(\\s*${escapeRegExp_(targetName)}\\b`);
  const mainAppend = `#include \"SwRemoteObjectNode.h\"

#include \"${targetName}.h\"

SW_REMOTE_OBJECT_NODE(${targetName}, \"${defaultNs}\", \"${defaultName}\")
`;
  await ensureFileHas_(mainPath, mainMacroRe, mainAppend);

  return { projDirAbs: projDir };
}

async function createComponentPluginProject_(projectRootAbs, pluginName, cppNamespace, componentClassName, componentType, deps) {
  const srcRoot = path.join(projectRootAbs, 'src');
  await fs.promises.mkdir(srcRoot, { recursive: true });

  const projDir = path.join(srcRoot, pluginName);
  await ensureDir_(projDir);

  const header = `#pragma once

#include \"SwRemoteObject.h\"

namespace ${cppNamespace} {

class ${componentClassName} : public SwRemoteObject {
public:
    ${componentClassName}(const SwString& sysName,
                          const SwString& nameSpace,
                          const SwString& objectName,
                          SwObject* parent = nullptr);

    SwString echo(const SwString& msg);
};

} // namespace ${cppNamespace}
`;

  const source = `#include \"${componentClassName}.h\"

#include \"SwDebug.h\"

namespace ${cppNamespace} {

${componentClassName}::${componentClassName}(const SwString& sysName,
                                            const SwString& nameSpace,
                                            const SwString& objectName,
                                            SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent) {
    ipcExposeRpc(echo, this, &${componentClassName}::echo);
}

SwString ${componentClassName}::echo(const SwString& msg) {
    swDebug() << \"[${componentClassName}]\" << objectName() << \"echo:\" << msg;
    return SwString(\"echo: \") + msg;
}

} // namespace ${cppNamespace}
`;

  const plugin = `#include \"SwRemoteObjectComponent.h\"

#include \"${componentClassName}.h\"

  SW_REGISTER_COMPONENT_NODE_AS(${cppNamespace}::${componentClassName}, \"${componentType}\");
`;

  const depIncludeDirs = deps.map((d) => '    ${CMAKE_CURRENT_LIST_DIR}/../' + d.dirName);

  const cmake = [
    'cmake_minimum_required(VERSION 3.10)',
    `project(${pluginName})`,
    '',
    'set(CMAKE_CXX_STANDARD 11)',
    'set(CMAKE_CXX_STANDARD_REQUIRED ON)',
    '',
    'if(MSVC)',
    '    add_compile_options(/Zc:preprocessor)',
    'endif()',
    '',
    // CORE_SW_ROOT can be either the project root's parent (coreSw repo) or the project root itself
    // (when opening a project folder directly in VS Code).
    'set(CORE_SW_ROOT \"${CMAKE_CURRENT_LIST_DIR}/../..\")',
    'get_filename_component(CORE_SW_ROOT \"${CORE_SW_ROOT}\" ABSOLUTE)',
    'if(NOT EXISTS \"${CORE_SW_ROOT}/src/core\")',
    '    set(CORE_SW_ROOT \"${CMAKE_CURRENT_LIST_DIR}/../../..\")',
    '    get_filename_component(CORE_SW_ROOT \"${CORE_SW_ROOT}\" ABSOLUTE)',
    'endif()',
    'if(NOT EXISTS \"${CORE_SW_ROOT}/src/core\")',
    '    message(FATAL_ERROR \"CORE_SW_ROOT not found (expected <coreSw root>/src/core).\")',
    'endif()',
    '',
    `add_library(${pluginName} SHARED`,
    `    ${pluginName}.cpp`,
    `    ${componentClassName}.cpp`,
    ')',
    '',
    '# Export the plugin registration symbol from SwRemoteObjectComponent.h.',
    `target_compile_definitions(${pluginName} PRIVATE SW_COMPONENT_PLUGIN=1)`,
    '',
    `target_include_directories(${pluginName} PRIVATE`,
    ...cmakeIncludeCoreDirs_(),
    '    ${CMAKE_CURRENT_LIST_DIR}',
    ...depIncludeDirs,
    ')',
    '',
    renderFindLibBlock_(pluginName, deps),
    '',
    'if(WIN32)',
    `    target_link_libraries(${pluginName} PRIVATE crypt32)`,
    'endif()',
    '',
    'if(NOT WIN32)',
    `    target_link_libraries(${pluginName} PRIVATE crypto)`,
    'endif()',
    ''
  ].filter((l) => l !== '').join('\n') + '\n';

  const depFileLines = deps.map((d) => toPosixPath_(path.relative(projDir, d.dirAbs)));
  const depFile = depFileLines.length ? depFileLines.join('\n') + '\n' : '';

  await writeFileIfMissing_(path.join(projDir, `${componentClassName}.h`), header);
  await writeFileIfMissing_(path.join(projDir, `${componentClassName}.cpp`), source);
  const pluginPath = path.join(projDir, `${pluginName}.cpp`);
  await writeFileIfMissing_(pluginPath, plugin);
  await writeFileIfMissing_(path.join(projDir, 'CMakeLists.txt'), cmake);
  await writeFileIfMissing_(path.join(projDir, 'deps.depend'), depFile);

  // If the plugin file already existed, ensure the registration macro is present.
  const pluginMacroRe = new RegExp(
    `\\bSW_REGISTER_COMPONENT_NODE_AS\\s*\\(\\s*${escapeRegExp_(cppNamespace)}\\s*::\\s*${escapeRegExp_(componentClassName)}\\b`
  );
  const pluginAppend = `#include \"SwRemoteObjectComponent.h\"

#include \"${componentClassName}.h\"

SW_REGISTER_COMPONENT_NODE_AS(${cppNamespace}::${componentClassName}, \"${componentType}\");
`;
  await ensureFileHas_(pluginPath, pluginMacroRe, pluginAppend);

  return { projDirAbs: projDir };
}

function nowMs_() {
  return Date.now();
}

function runSwapi_(args) {
  const swapiExe = resolveSwapiExecutable_();
  const timeout = swapiTimeoutMs_();
  return new Promise((resolve, reject) => {
    const child = cp.execFile(swapiExe, args, { encoding: 'utf8', timeout, maxBuffer: 16 * 1024 * 1024 }, (err, stdout, stderr) => {
      if (err) {
        const msg = [
          `swapi failed: ${err.message || String(err)}`,
          stderr ? `stderr:\n${stderr}` : '',
          stdout ? `stdout:\n${stdout}` : ''
        ].filter(Boolean).join('\n');
        reject(new Error(msg));
        return;
      }
      resolve({ stdout: String(stdout || ''), stderr: String(stderr || '') });
    });
    // If the timeout triggers, execFile will kill the process, but we keep a ref just in case.
    if (!child) {
      reject(new Error('failed to start swapi'));
    }
  });
}

function parseJsonStrict_(label, raw) {
  try {
    return JSON.parse(raw);
  } catch (e) {
    const preview = raw.length > 2000 ? raw.slice(0, 2000) + '\n...(truncated)...' : raw;
    throw new Error(`${label}: JSON parse failed: ${e && e.message ? e.message : String(e)}\nOutput:\n${preview}`);
  }
}

function normPathKey_(p) {
  if (!p) return '';
  const abs = path.isAbsolute(p) ? p : path.resolve(p);
  return (process.platform === 'win32') ? abs.toLowerCase() : abs;
}

function looksLikeLauncherJson_(root, absPath) {
  if (!isObject_(root)) return false;
  const sys = root.sys;
  if (typeof sys !== 'string' || !sys.trim()) return false;

  const nodesArr = Array.isArray(root.nodes) ? root.nodes : null;
  const containersArr = Array.isArray(root.containers) ? root.containers : null;
  const hasNodes = !!(nodesArr && nodesArr.length > 0);
  const hasContainers = !!(containersArr && containersArr.length > 0);
  if (hasNodes || hasContainers) return true;

  // Allow listing "empty" skeleton launcher files (so Project UI can create nodes afterwards),
  // but only when the filename clearly looks like a launcher config.
  const fileName = absPath ? String(path.basename(absPath)).toLowerCase() : '';
  const nameLooksLauncher = fileName.endsWith('.launch.json') || fileName.includes('launcher') || fileName.includes('launch');
  if (nameLooksLauncher && (nodesArr || containersArr)) return true;

  return false;
}

function extractLauncherMeta_(root, absPath) {
  if (!looksLikeLauncherJson_(root, absPath)) return null;

  const abs = String(absPath || '');
  const wsFolders = workspaceFolderPaths_();
  let bestRel = '';
  for (const ws of wsFolders) {
    if (!ws) continue;
    const rel = path.relative(ws, abs);
    if (!rel || rel.startsWith('..') || path.isAbsolute(rel)) continue;
    if (!bestRel || rel.length < bestRel.length) bestRel = rel;
  }
  const relPath = bestRel || abs;

  const sys = String(root.sys || '').trim();
  const durationMs = (root.duration_ms !== undefined) ? Number(root.duration_ms) : 0;
  const nodesCount = Array.isArray(root.nodes) ? root.nodes.length : 0;
  const containersCount = Array.isArray(root.containers) ? root.containers.length : 0;

  return {
    sys,
    durationMs: Number.isFinite(durationMs) ? Math.floor(durationMs) : 0,
    nodesCount,
    containersCount,
    absPath,
    relPath
  };
}

async function readLauncherMetaFromFile_(absPath) {
  const key = normPathKey_(absPath);
  if (!key) return null;

  let st;
  try {
    st = fs.statSync(absPath);
  } catch (_) {
    launcherCache_.delete(key);
    return null;
  }
  const mtimeMs = st && st.mtimeMs ? Number(st.mtimeMs) : 0;

  const cached = launcherCache_.get(key);
  if (cached && cached.mtimeMs === mtimeMs) return cached.meta;

  let raw;
  try {
    raw = await fs.promises.readFile(absPath, 'utf8');
  } catch (_) {
    return null;
  }

  let parsed;
  try {
    parsed = JSON.parse(String(raw || ''));
  } catch (_) {
    return null;
  }

  const meta = extractLauncherMeta_(parsed, absPath);
  if (!meta) {
    launcherCache_.delete(key);
    return null;
  }

  launcherCache_.set(key, { mtimeMs, meta });
  return meta;
}

async function findLauncherJsonFiles_() {
  const patterns = launcherSearchPatterns_();
  const exclude = launcherSearchExclude_();
  const maxResults = launcherMaxResults_();

  const out = new Map(); // key -> absPath
  for (const pat of patterns) {
    let uris = [];
    try {
      uris = await vscode.workspace.findFiles(pat, exclude, maxResults);
    } catch (_) {
      try {
        uris = await vscode.workspace.findFiles(pat, undefined, maxResults);
      } catch (_) {
        // ignore
      }
    }
    for (const u of uris) {
      const p = u && u.fsPath ? String(u.fsPath) : '';
      const k = normPathKey_(p);
      if (k) out.set(k, p);
    }
  }
  return Array.from(out.values());
}

async function pickDomain_(defaultDomain) {
  if (defaultDomain) return defaultDomain;

  const { stdout } = await runSwapi_(['app', 'list', '--json']);
  const apps = parseJsonStrict_('swapi app list', stdout);
  const items = [];
  items.push({ label: '(all domains)', description: 'list targets across all domains', domain: '' });

  if (Array.isArray(apps)) {
    for (const a of apps) {
      if (!a || typeof a !== 'object') continue;
      const domain = String(a.domain || '');
      if (!domain) continue;
      const clientCount = (a.clientCount !== undefined) ? Number(a.clientCount) : undefined;
      const desc = Number.isFinite(clientCount) ? `clients=${clientCount}` : '';
      items.push({ label: domain, description: desc, domain });
    }
  }

  if (items.length <= 1) {
    // No domains reported (apps registry empty). Let the user type one.
    const typed = await vscode.window.showInputBox({
      title: 'Sw: Domain (sys)',
      prompt: 'Enter a domain (sys) to list targets, or leave empty to list all domains',
      value: ''
    });
    return (typed || '').trim();
  }

  const pick = await vscode.window.showQuickPick(items, {
    title: 'Sw: Select domain',
    placeHolder: 'Select a domain (sys) to list targets',
    matchOnDescription: true
  });
  if (!pick) return undefined;
  return pick.domain;
}

function formatPidList_(pids) {
  if (!Array.isArray(pids) || !pids.length) return 'pids=<none>';
  const xs = [];
  for (const x of pids) {
    const n = Number(x);
    if (Number.isFinite(n) && n > 0) xs.push(String(Math.floor(n)));
  }
  return xs.length ? `pids=${xs.join(',')}` : 'pids=<none>';
}

async function pickTargetPid_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const defaultDomain = String(cfg.get('defaultDomain', '') || '').trim();
  const includeStale = !!cfg.get('includeStaleNodes', false);

  const domain = await pickDomain_(defaultDomain);
  if (domain === undefined) return undefined; // user cancelled

  const args = ['node', 'list', '--json'];
  if (domain) args.push('--domain', domain);
  if (includeStale) args.push('--include-stale');

  const { stdout } = await runSwapi_(args);
  const nodes = parseJsonStrict_('swapi node list', stdout);

  if (!Array.isArray(nodes) || !nodes.length) {
    vscode.window.showWarningMessage('Sw: no targets found (registry empty?)');
    return undefined;
  }

  const items = [];
  for (const n of nodes) {
    if (!n || typeof n !== 'object') continue;
    const target = String(n.target || '');
    if (!target) continue;
    const alive = (n.alive !== undefined) ? !!n.alive : true;
    const pidsDesc = formatPidList_(n.pids);
    const desc = `${alive ? 'alive' : 'stale'} ${pidsDesc}`;
    items.push({ label: target, description: desc, node: n });
  }
  items.sort((a, b) => a.label.localeCompare(b.label));

  const pick = await vscode.window.showQuickPick(items, {
    title: 'Sw: Select target',
    placeHolder: 'Pick a <sys>/<target> to attach',
    matchOnDescription: true
  });
  if (!pick) return undefined;

  const rawPids = Array.isArray(pick.node.pids) ? pick.node.pids : [];
  const pids = [];
  for (const x of rawPids) {
    const pid = Number(x);
    if (Number.isFinite(pid) && pid > 0) pids.push(Math.floor(pid));
  }

  if (!pids.length) {
    vscode.window.showErrorMessage(`Sw: no PID found for target ${pick.label}`);
    return undefined;
  }

  if (pids.length === 1) {
    return String(pids[0]);
  }

  const pidPick = await vscode.window.showQuickPick(
    pids.map((pid) => ({ label: String(pid), pid })),
    { title: 'Sw: Select PID', placeHolder: 'Multiple PIDs found for this target' }
  );
  if (!pidPick) return undefined;
  return String(pidPick.pid);
}

function normalizePidList_(pidsRaw) {
  const out = [];
  if (!Array.isArray(pidsRaw)) return out;
  for (const x of pidsRaw) {
    const pid = Number(x);
    if (Number.isFinite(pid) && pid > 0) out.push(Math.floor(pid));
  }
  out.sort((a, b) => a - b);
  return out;
}

async function pickPidFromNode_(node) {
  const pids = normalizePidList_(node && node.pids);
  if (!pids.length) return undefined;
  if (pids.length === 1) return String(pids[0]);

  const pick = await vscode.window.showQuickPick(
    pids.map((pid) => ({ label: String(pid), pid })),
    { title: 'Sw: Select PID', placeHolder: 'Multiple PIDs found for this target' }
  );
  if (!pick) return undefined;
  return String(pick.pid);
}

function debuggerType_() {
  const cfg = vscode.workspace.getConfiguration('coreSw');
  const raw = String(cfg.get('debuggerType', 'cppvsdbg') || '').trim();
  return raw || 'cppvsdbg';
}

function splitTargetString_(target) {
  if (!target || typeof target !== 'string') return { domain: '', object: '' };
  let s = target.replace(/\\/g, '/').trim();
  while (s.startsWith('/')) s = s.slice(1);
  while (s.endsWith('/')) s = s.slice(0, -1);
  if (!s) return { domain: '', object: '' };
  const slash = s.indexOf('/');
  if (slash < 0) return { domain: '', object: s };
  return { domain: s.slice(0, slash), object: s.slice(slash + 1) };
}

function resolveDomainObjectFromNode_(node) {
  let domain = String((node && node.domain) || '').trim();
  let object = String((node && node.object) || '').trim();

  if ((!domain || !object) && node && node.target) {
    const t = splitTargetString_(String(node.target));
    if (!domain) domain = t.domain;
    if (!object) object = t.object;
  }

  return { domain, object };
}

async function swapiNodeInfo_(domain, object) {
  if (!domain || !object) return undefined;
  const { stdout } = await runSwapi_(['node', 'info', object, '--domain', domain, '--json']);
  const info = parseJsonStrict_('swapi node info', stdout);
  return isObject_(info) ? info : undefined;
}

async function refreshNodeInfo_(node) {
  const { domain, object } = resolveDomainObjectFromNode_(node);
  if (!domain || !object) return node;

  try {
    const info = await swapiNodeInfo_(domain, object);
    return info || node;
  } catch (_) {
    return node;
  }
}

async function pidExists_(pid) {
  const pidNum = Number(pid);
  if (!Number.isFinite(pidNum) || pidNum <= 0) return false;
  const pidInt = Math.floor(pidNum);

  if (process.platform === 'win32') {
    return await new Promise((resolve) => {
      cp.execFile('tasklist', ['/FI', `PID eq ${pidInt}`, '/FO', 'CSV', '/NH'], { encoding: 'utf8' }, (err, stdout) => {
        if (err) {
          // Best-effort: if tasklist fails, don't block attach.
          resolve(true);
          return;
        }
        const out = String(stdout || '').trim();
        if (!out) return resolve(false);
        if (/No tasks are running/i.test(out)) return resolve(false);
        // If it starts with a quoted image name, we have a row.
        if (out.startsWith('"')) return resolve(true);
        resolve(out.includes(String(pidInt)));
      });
    });
  }

  try {
    process.kill(pidInt, 0);
    return true;
  } catch (e) {
    // EPERM means "exists but not permitted".
    if (e && e.code === 'EPERM') return true;
    return false;
  }
}

async function attachToPid_(pid, labelForName) {
  if (!pid) return false;

  const folder = (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length)
    ? vscode.workspace.workspaceFolders[0]
    : undefined;

  const type = debuggerType_();
  const name = labelForName ? `Attach: ${labelForName}` : `Attach PID ${pid}`;

  try {
    const ok = await vscode.debug.startDebugging(folder, {
      name,
      type,
      request: 'attach',
      processId: String(pid)
    });
    if (!ok) {
      vscode.window.showErrorMessage(`Sw: VS Code refused to start debug session (type=${type}).`);
    }
    return !!ok;
  } catch (e) {
    vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
    return false;
  }
}

class DomainItem_ extends vscode.TreeItem {
  constructor(domain, targetCount) {
    super(domain, targetCount > 0 ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None);
    this.domain = domain;
    this.id = `coreSwDomain:${domain}`;
    this.contextValue = 'coreSwDomain';
    this.iconPath = new vscode.ThemeIcon('server');
    this.description = targetCount > 0 ? `${targetCount}` : '';
    this.tooltip = `${domain} (${targetCount} target${targetCount === 1 ? '' : 's'})`;
  }
}

class TargetItem_ extends vscode.TreeItem {
  constructor(node) {
    const domain = String((node && node.domain) || '');
    const object = String((node && node.object) || (node && node.objectName) || (node && node.target) || '');
    super(object, vscode.TreeItemCollapsibleState.None);

    this.node = node;
    const key = String((node && node.target) || '') || `${domain}/${object}`;
    this.id = `coreSwTarget:${key}`;
    this.contextValue = 'coreSwTarget';

    const alive = !!(node && node.alive);
    const pids = normalizePidList_(node && node.pids);
    const pidsText = pids.length ? pids.join(',') : '<none>';

    this.description = `${alive ? 'alive' : 'stale'} pid=${pidsText}`;
    this.tooltip = [
      `target: ${domain}/${object}`,
      `alive: ${alive ? 'true' : 'false'}`,
      `pids: ${pidsText}`,
      (node && node.lastSeenMs !== undefined) ? `lastSeenMs: ${node.lastSeenMs}` : ''
    ].filter(Boolean).join('\n');

    this.iconPath = new vscode.ThemeIcon(alive ? 'debug' : 'circle-slash');

    // One-click dashboard on selection (attach is available via context menu).
    this.command = {
      command: 'coreSw.openDashboard',
      title: 'Open Dashboard',
      arguments: [this]
    };
  }
}

class CoreSwTargetsProvider_ {
  constructor() {
    this._onDidChangeTreeData = new vscode.EventEmitter();
    this.onDidChangeTreeData = this._onDidChangeTreeData.event;

    this._loaded = false;
    this._loading = false;
    this._domains = []; // [{ domain, nodes: [] }]
    this._err = '';
  }

  refresh() {
    this._loaded = false;
    this._err = '';
    this._onDidChangeTreeData.fire();
  }

  getTreeItem(element) {
    return element;
  }

  async getChildren(element) {
    if (!this._loaded && !this._loading) {
      this._loading = true;
      try {
        await this._load_();
      } finally {
        this._loading = false;
        this._loaded = true;
      }
    }

    if (!element) {
      const items = [];
      if (this._err) {
        const it = new vscode.TreeItem(`Error: ${this._err}`, vscode.TreeItemCollapsibleState.None);
        it.iconPath = new vscode.ThemeIcon('error');
        items.push(it);
      }
      if (!this._domains.length) {
        const it = new vscode.TreeItem(this._loading ? '(loading...)' : '(no domains)', vscode.TreeItemCollapsibleState.None);
        it.iconPath = new vscode.ThemeIcon('info');
        items.push(it);
        return items;
      }
      return items.concat(this._domains.map((d) => new DomainItem_(d.domain, d.nodes.length)));
    }

    if (element instanceof DomainItem_) {
      const d = this._domains.find((x) => x.domain === element.domain);
      if (!d || !d.nodes.length) return [];
      return d.nodes.map((n) => new TargetItem_(n));
    }

    return [];
  }

  async _load_() {
    const cfg = vscode.workspace.getConfiguration('coreSw');
    const includeStale = !!cfg.get('includeStaleNodes', false);

    const nodeArgs = ['node', 'list', '--json'];
    if (includeStale) nodeArgs.push('--include-stale');

    let appsRes, nodesRes;
    try {
      [appsRes, nodesRes] = await Promise.all([
        runSwapi_(['app', 'list', '--json']),
        runSwapi_(nodeArgs)
      ]);
    } catch (e) {
      this._err = (e && e.message) ? e.message : String(e);
      return;
    }

    const apps = parseJsonStrict_('swapi app list', appsRes.stdout);
    const nodes = parseJsonStrict_('swapi node list', nodesRes.stdout);

    const domainSet = new Set();
    if (Array.isArray(apps)) {
      for (const a of apps) {
        if (!isObject_(a)) continue;
        const d = String(a.domain || '');
        if (d) domainSet.add(d);
      }
    }

    const perDomain = new Map(); // domain -> nodes[]
    if (Array.isArray(nodes)) {
      for (const n of nodes) {
        if (!isObject_(n)) continue;
        const d = String(n.domain || '');
        if (d) domainSet.add(d);
        const key = d || '<unknown>';
        if (!perDomain.has(key)) perDomain.set(key, []);
        perDomain.get(key).push(n);
      }
    }

    const domains = Array.from(domainSet);
    domains.sort((a, b) => a.localeCompare(b));

    // Ensure we also show domains that only exist in nodes (or unknown).
    for (const k of perDomain.keys()) {
      if (k === '<unknown>') continue;
      if (!domainSet.has(k)) domains.push(k);
    }
    if (perDomain.has('<unknown>')) domains.push('<unknown>');

    const dedup = new Set();
    const finalDomains = [];
    for (const d of domains) {
      if (dedup.has(d)) continue;
      dedup.add(d);
      finalDomains.push(d);
    }

    const newDomains = finalDomains.map((d) => {
      const list = perDomain.get(d) || [];
      list.sort((a, b) => String(a.object || '').localeCompare(String(b.object || '')));
      return { domain: d, nodes: list };
    });

    this._err = '';
    this._domains = newDomains;
  }
}

class LauncherDomainItem_ extends vscode.TreeItem {
  constructor(domain, launcherCount) {
    super(domain, launcherCount > 0 ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None);
    this.domain = domain;
    this.id = `coreSwLaunchDomain:${domain}`;
    this.contextValue = 'coreSwLaunchDomain';
    this.iconPath = new vscode.ThemeIcon('rocket');
    this.description = launcherCount > 0 ? `${launcherCount}` : '';
    this.tooltip = `${domain} (${launcherCount} launcher${launcherCount === 1 ? '' : 's'})`;
  }
}

class LauncherFileItem_ extends vscode.TreeItem {
  constructor(meta) {
    const fileName = meta && meta.absPath ? path.basename(meta.absPath) : '(launcher)';
    super(fileName, vscode.TreeItemCollapsibleState.None);

    this.launcher = meta;
    this.id = `coreSwLauncher:${meta && meta.absPath ? normPathKey_(meta.absPath) : fileName}`;

    const entry = meta && meta.absPath ? getLaunchEntry_(meta.absPath) : undefined;
    const running = !!entry;
    const pid = running && entry.proc && entry.proc.pid ? entry.proc.pid : 0;
    this.contextValue = running ? 'coreSwLauncherRunning' : 'coreSwLauncher';

    const parts = [];
    if (meta && meta.sys) parts.push(`sys=${meta.sys}`);
    if (meta) parts.push(`nodes=${meta.nodesCount || 0}`);
    if (meta) parts.push(`containers=${meta.containersCount || 0}`);
    if (running) parts.push(`running pid=${pid}`);
    this.description = parts.join('  ');

    this.tooltip = [
      meta && meta.relPath ? `file: ${meta.relPath}` : '',
      meta && meta.absPath ? `abs: ${meta.absPath}` : '',
      meta && meta.durationMs ? `duration_ms: ${meta.durationMs}` : 'duration_ms: 0'
    ].filter(Boolean).join('\n');

    this.iconPath = new vscode.ThemeIcon(running ? 'debug-stop' : 'debug-start');
    this.command = {
      command: 'coreSw.launchLauncher',
      title: 'Launch',
      arguments: [this]
    };
  }
}

class CoreSwLaunchersProvider_ {
  constructor() {
    this._onDidChangeTreeData = new vscode.EventEmitter();
    this.onDidChangeTreeData = this._onDidChangeTreeData.event;

    this._loaded = false;
    this._loading = false;
    this._domains = []; // [{ domain, launchers: meta[] }]
    this._err = '';
  }

  refresh() {
    this._loaded = false;
    this._err = '';
    this._onDidChangeTreeData.fire();
  }

  getTreeItem(element) {
    return element;
  }

  async getChildren(element) {
    if (!this._loaded && !this._loading) {
      this._loading = true;
      try {
        await this._load_();
      } finally {
        this._loading = false;
        this._loaded = true;
      }
    }

    if (!element) {
      const items = [];
      if (this._err) {
        const it = new vscode.TreeItem(`Error: ${this._err}`, vscode.TreeItemCollapsibleState.None);
        it.iconPath = new vscode.ThemeIcon('error');
        items.push(it);
      }
      if (!this._domains.length) {
        const it = new vscode.TreeItem(this._loading ? '(loading...)' : '(no launchers found)', vscode.TreeItemCollapsibleState.None);
        it.iconPath = new vscode.ThemeIcon('info');
        items.push(it);
        return items;
      }
      return items.concat(this._domains.map((d) => new LauncherDomainItem_(d.domain, d.launchers.length)));
    }

    if (element instanceof LauncherDomainItem_) {
      const d = this._domains.find((x) => x.domain === element.domain);
      if (!d || !d.launchers.length) return [];
      return d.launchers.map((m) => new LauncherFileItem_(m));
    }

    return [];
  }

  allLaunchers() {
    const out = [];
    for (const d of this._domains) {
      for (const m of d.launchers) out.push(m);
    }
    return out;
  }

  async ensureLoaded() {
    if (this._loaded && !this._loading) return;
    await this.getChildren(undefined);
  }

  async _load_() {
    const files = await findLauncherJsonFiles_();
    const perDomain = new Map(); // sys -> meta[]

    for (const absPath of files) {
      const meta = await readLauncherMetaFromFile_(absPath);
      if (!meta) continue;
      const sys = meta.sys || '<unknown>';
      if (!perDomain.has(sys)) perDomain.set(sys, []);
      perDomain.get(sys).push(meta);
    }

    const domains = Array.from(perDomain.keys());
    domains.sort((a, b) => a.localeCompare(b));

    const newDomains = domains.map((d) => {
      const list = perDomain.get(d) || [];
      list.sort((a, b) => String(a.relPath || '').localeCompare(String(b.relPath || '')));
      return { domain: d, launchers: list };
    });

    this._err = '';
    this._domains = newDomains;
  }
}

function webviewNonce_() {
  return crypto.randomBytes(16).toString('hex');
}

function loadDashboardHtml_(context, webview) {
  const nonce = webviewNonce_();
  const htmlPath = path.join(context.extensionPath, 'webview', 'dashboard.html');
  const cssDisk = vscode.Uri.joinPath(context.extensionUri, 'webview', 'dashboard.css');
  const jsDisk = vscode.Uri.joinPath(context.extensionUri, 'webview', 'dashboard.js');
  const cssUri = webview.asWebviewUri(cssDisk);
  const jsUri = webview.asWebviewUri(jsDisk);

  let html = fs.readFileSync(htmlPath, 'utf8');
  html = html.replace(/\{\{nonce\}\}/g, nonce);
  html = html.replace(/\{\{cspSource\}\}/g, webview.cspSource);
  html = html.replace(/\{\{cssUri\}\}/g, cssUri.toString());
  html = html.replace(/\{\{jsUri\}\}/g, jsUri.toString());
  return html;
}

function loadProjectHtml_(context, webview) {
  const nonce = webviewNonce_();
  const htmlPath = path.join(context.extensionPath, 'webview', 'project.html');
  const cssDisk = vscode.Uri.joinPath(context.extensionUri, 'webview', 'project.css');
  const jsDisk = vscode.Uri.joinPath(context.extensionUri, 'webview', 'project.js');
  const cssUri = webview.asWebviewUri(cssDisk);
  const jsUri = webview.asWebviewUri(jsDisk);

  let html = fs.readFileSync(htmlPath, 'utf8');
  html = html.replace(/\{\{nonce\}\}/g, nonce);
  html = html.replace(/\{\{cspSource\}\}/g, webview.cspSource);
  html = html.replace(/\{\{cssUri\}\}/g, cssUri.toString());
  html = html.replace(/\{\{jsUri\}\}/g, jsUri.toString());
  return html;
}

class CoreSwProjectViewProvider_ {
  constructor(context, launchersProvider) {
    this._context = context;
    this._launchersProvider = launchersProvider;
    this._view = null;
  }

  resolveWebviewView(view) {
    this._view = view;
    view.webview.options = {
      enableScripts: true,
      localResourceRoots: [
        vscode.Uri.joinPath(this._context.extensionUri, 'webview'),
        vscode.Uri.joinPath(this._context.extensionUri, 'resources')
      ]
    };

    view.webview.html = loadProjectHtml_(this._context, view.webview);

    view.webview.onDidReceiveMessage(async (msg) => {
      if (!msg || typeof msg !== 'object') return;
      if (msg.type !== 'request') return;

      const id = String(msg.id || '');
      const action = String(msg.action || '');
      const params = isObject_(msg.params) ? msg.params : {};

      try {
        const data = await this._handle_(action, params);
        view.webview.postMessage({ type: 'response', id, ok: true, data });
      } catch (e) {
        view.webview.postMessage({ type: 'response', id, ok: false, error: e && e.message ? e.message : String(e) });
      }
    });
  }

  async _handle_(action, params) {
    if (action === 'listLaunchers') {
      await this._launchersProvider.ensureLoaded();
      const launchers = this._launchersProvider.allLaunchers().map((m) => {
        const entry = m && m.absPath ? getLaunchEntry_(m.absPath) : undefined;
        const running = !!entry;
        const pid = running && entry.proc && entry.proc.pid ? entry.proc.pid : 0;
        return Object.assign({}, m, { running, pid });
      });
      return { launchers };
    }

    if (action === 'openFile') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('openFile: missing absPath');
      const doc = await vscode.workspace.openTextDocument(vscode.Uri.file(absPath));
      await vscode.window.showTextDocument(doc, { preview: false });
      return { ok: true };
    }

    if (action === 'openOutput') {
      showOutput_();
      return { ok: true };
    }

    if (action === 'diagnostics') {
      return await runDiagnostics_();
    }

    if (action === 'openDashboard') {
      await vscode.commands.executeCommand('coreSw.openDashboard');
      return { ok: true };
    }

    if (action === 'revealProject') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('revealProject: missing absPath');
      const dir = path.dirname(absPath);
      await vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(dir));
      return { ok: true, absPath: dir };
    }

    if (action === 'openInstallBin') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('openInstallBin: missing absPath');
      const dir = path.dirname(absPath);
      const binDir = path.join(dir, 'install', 'bin');
      if (!fs.existsSync(binDir)) {
        vscode.window.showInformationMessage('coreSw: install/bin does not exist yet (build first).');
        await vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(dir));
        return { ok: false, absPath: binDir };
      }
      await vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(binDir));
      return { ok: true, absPath: binDir };
    }

    if (action === 'openLog') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('openLog: missing absPath');
      const dir = path.dirname(absPath);
      const logDir = path.join(dir, 'log');
      if (!fs.existsSync(logDir)) {
        vscode.window.showInformationMessage('coreSw: log/ does not exist yet.');
        await vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(dir));
        return { ok: false, absPath: logDir };
      }
      await vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(logDir));
      return { ok: true, absPath: logDir };
    }

    if (action === 'startLauncher') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('startLauncher: missing absPath');
      const meta = await readLauncherMetaFromFile_(absPath);
      if (!meta) throw new Error(`startLauncher: not a SwLaunch JSON: ${absPath}`);
      await startSwLaunch_(meta);
      this._launchersProvider.refresh();
      return { ok: true };
    }

    if (action === 'stopLauncher') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('stopLauncher: missing absPath');
      const ok = await stopSwLaunch_(absPath);
      this._launchersProvider.refresh();
      return { ok };
    }

    if (action === 'readLaunchJson') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('readLaunchJson: missing absPath');
      const raw = await fs.promises.readFile(absPath, 'utf8');
      const parsed = JSON.parse(String(raw || ''));
      return { config: parsed };
    }

    if (action === 'writeLaunchJson') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('writeLaunchJson: missing absPath');
      const cfg = isObject_(params.config) ? params.config : undefined;
      if (!cfg) throw new Error('writeLaunchJson: missing config object');
      const raw = JSON.stringify(cfg, null, 2) + '\n';
      await fs.promises.writeFile(absPath, raw, 'utf8');
      this._launchersProvider.refresh();
      return { ok: true };
    }

    if (action === 'createProject') {
      const wsFolders = vscode.workspace.workspaceFolders;
      if (!wsFolders || !wsFolders.length) throw new Error('createProject: no workspace folder is open');

      let ws = workspaceFolderPath_();
      if (wsFolders.length > 1) {
        const items = wsFolders.map((f) => ({
          label: f && f.name ? String(f.name) : 'workspace',
          description: f && f.uri && f.uri.fsPath ? String(f.uri.fsPath) : '',
          abs: f && f.uri && f.uri.fsPath ? String(f.uri.fsPath) : ''
        }));
        const picked = await vscode.window.showQuickPick(items, {
          title: 'coreSw: New project',
          placeHolder: 'Select the workspace folder where the project will be created'
        });
        if (!picked || !picked.abs) return { ok: false, cancelled: true };
        ws = picked.abs;
      }
      if (!ws) throw new Error('createProject: workspace folder path is empty');

      const projectNameRaw = await vscode.window.showInputBox({
        title: 'coreSw: New project',
        prompt: 'Project folder name (created inside the workspace)',
        value: 'SwProject',
        validateInput: (v) => {
          const name = String(v || '').trim();
          if (!name) return 'required';
          if (name === '.' || name === '..') return 'invalid name';
          if (/[<>:"/\\\\|?*]/.test(name)) return 'invalid characters';
          if (/[\\/]/.test(name)) return 'do not include path separators';
          return undefined;
        }
      });
      if (projectNameRaw === undefined) return { ok: false, cancelled: true };
      const projectName = String(projectNameRaw).trim();

      const sysRaw = await vscode.window.showInputBox({
        title: 'coreSw: New project',
        prompt: 'Domain (sys)',
        value: 'sys',
        validateInput: (v) => (String(v || '').trim() ? undefined : 'required')
      });
      if (sysRaw === undefined) return { ok: false, cancelled: true };
      const sys = String(sysRaw).trim();

      const projectDirAbs = path.join(ws, projectName);
      await ensureEmptyDir_(projectDirAbs);

      await fs.promises.mkdir(path.join(projectDirAbs, 'src'), { recursive: true });
      await fs.promises.mkdir(path.join(projectDirAbs, 'install'), { recursive: true });
      await fs.promises.mkdir(path.join(projectDirAbs, 'log'), { recursive: true });

      const gitignorePath = path.join(projectDirAbs, '.gitignore');
      if (!fs.existsSync(gitignorePath)) {
        await fs.promises.writeFile(gitignorePath, 'build/\nlog/\ninstall/\n', 'utf8');
      }

      const launchAbsPath = path.join(projectDirAbs, `${projectName}.launch.json`);
      const cfg = { sys, duration_ms: 0, containers: [], nodes: [] };
      await writeFileNoOverwrite_(launchAbsPath, JSON.stringify(cfg, null, 2) + '\n');
      this._launchersProvider.refresh();

      try {
        const doc = await vscode.workspace.openTextDocument(vscode.Uri.file(launchAbsPath));
        await vscode.window.showTextDocument(doc, { preview: false });
      } catch (_) {
        // ignore
      }

      return { ok: true, absPath: launchAbsPath };
    }

    if (action === 'createLaunchFile') {
      const sysRaw = await vscode.window.showInputBox({
        title: 'coreSw: New launch.json',
        prompt: 'Domain (sys)',
        value: 'sys',
        validateInput: (v) => (String(v || '').trim() ? undefined : 'required')
      });
      if (sysRaw === undefined) return { ok: false, cancelled: true };
      const sys = String(sysRaw).trim();

      const ws = workspaceFolderPath_();
      const defaultUri = ws ? vscode.Uri.file(path.join(ws, `${sys}.launch.json`)) : undefined;
      const uri = await vscode.window.showSaveDialog({
        title: 'coreSw: Create launch.json',
        defaultUri,
        filters: { 'JSON': ['json'] }
      });
      if (!uri) return { ok: false, cancelled: true };

      const cfg = { sys, duration_ms: 0, containers: [], nodes: [] };
      await fs.promises.writeFile(uri.fsPath, JSON.stringify(cfg, null, 2) + '\n', 'utf8');
      this._launchersProvider.refresh();

      try {
        const doc = await vscode.workspace.openTextDocument(uri);
        await vscode.window.showTextDocument(doc, { preview: false });
      } catch (_) {
        // ignore
      }

      return { ok: true, absPath: uri.fsPath };
    }

    if (action === 'buildProject') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('buildProject: missing absPath');
      const rootAbs = path.dirname(absPath);
      const args = ['--root', rootAbs, '--scan', 'src', '--verbose'];
      if (params.clean) args.push('--clean');
      await vscode.window.withProgress(
        { location: vscode.ProgressLocation.Notification, title: `coreSw: SwBuild (${path.basename(rootAbs)})...` },
        async () => {
          await runSwBuild_(args, workspaceFolderPath_() || undefined);
        }
      );
      return { ok: true };
    }

    if (action === 'buildAndRun') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('buildAndRun: missing absPath');
      await this._handle_('buildProject', { absPath, clean: !!params.clean });
      const meta = await readLauncherMetaFromFile_(absPath);
      if (!meta) throw new Error(`buildAndRun: not a SwLaunch JSON: ${absPath}`);
      await startSwLaunch_(meta);
      this._launchersProvider.refresh();
      return { ok: true };
    }

    if (action === 'createNodeWizard') {
      const absPath = String(params.absPath || '').trim();
      if (!absPath) throw new Error('createNodeWizard: missing absPath');

      const launchDirAbs = path.dirname(absPath);
      const raw = await fs.promises.readFile(absPath, 'utf8');
      const cfg = JSON.parse(String(raw || ''));
      if (!isObject_(cfg)) throw new Error('createNodeWizard: launch JSON root must be an object');

      const sys = String(cfg.sys || '').trim() || 'sys';

      const objectName = await vscode.window.showInputBox({
        title: 'coreSw: New node',
        prompt: 'Object name (ex: alpha)',
        value: 'node',
        validateInput: (v) => (String(v || '').trim() ? undefined : 'required')
      });
      if (objectName === undefined) return { ok: false, cancelled: true };
      const objName = String(objectName).trim();

      const ns = await vscode.window.showInputBox({
        title: 'coreSw: New node',
        prompt: 'Namespace (ns) (ex: pn)',
        value: sys,
        validateInput: (v) => (String(v || '').trim() ? undefined : 'required')
      });
      if (ns === undefined) return { ok: false, cancelled: true };
      const nsName = String(ns).trim();

      const kindParam = String(params.kind || '').trim().toLowerCase();
      let kind = '';
      if (kindParam === 'standalone' || kindParam === 'component') {
        kind = kindParam;
      } else {
        const kindPick = await vscode.window.showQuickPick(
          [
            { label: 'Standalone (executable)', kind: 'standalone' },
            { label: 'Composable (component plugin)', kind: 'component' }
          ],
          { title: 'coreSw: New node', placeHolder: 'Pick node kind' }
        );
        if (!kindPick) return { ok: false, cancelled: true };
        kind = kindPick.kind;
      }

      const prefix = camelFrom_(sys) || cap1_(sys);
      const capObj = camelFrom_(objName) || cap1_(objName);

      const srcRootAbs = path.join(launchDirAbs, 'src');
      const depsCandidates = await scanStaticLibDeps_(srcRootAbs);
      const depItems = depsCandidates.map((d) => ({
        label: d.libName,
        description: d.dirName,
        picked: /Core$|Utils$/i.test(d.libName),
        dep: d
      }));
      const depsPick = depItems.length
        ? await vscode.window.showQuickPick(depItems, {
          title: 'coreSw: New node',
          placeHolder: 'Select library deps (optional, writes deps.depend + find_library)',
          canPickMany: true,
          matchOnDescription: true
        })
        : [];
      if (depsPick === undefined) return { ok: false, cancelled: true };
      const deps = Array.isArray(depsPick) ? depsPick.map((x) => x.dep).filter(Boolean) : [];

      if (kind === 'standalone') {
        const suggested = `${prefix}${capObj}Node`;
        const targetName = await vscode.window.showInputBox({
          title: 'coreSw: New node',
          prompt: 'CMake target / C++ class name (ex: PnAlphaNode)',
          value: suggested,
          validateInput: (v) => (validCppIdent_(v) ? undefined : 'invalid C++ identifier')
        });
        if (targetName === undefined) return { ok: false, cancelled: true };
        const t = String(targetName).trim();
        if (!validCppIdent_(t)) throw new Error(`invalid target name: ${t}`);

        await createStandaloneNodeProject_(launchDirAbs, t, nsName, objName, deps);

        if (!Array.isArray(cfg.nodes)) cfg.nodes = [];
        const exeAbsNoExt = path.join(launchDirAbs, 'install', 'bin', t);
        const wdAbs = path.join(launchDirAbs, 'install', 'bin');
        cfg.nodes.push({
          ns: nsName,
          name: objName,
          executable: relFrom_(launchDirAbs, exeAbsNoExt),
          workingDirectory: relFrom_(launchDirAbs, wdAbs),
          options: defaultProcessOptions_(),
          params: {}
        });

        await fs.promises.writeFile(absPath, JSON.stringify(cfg, null, 2) + '\n', 'utf8');
        this._launchersProvider.refresh();
        vscode.window.showInformationMessage(`coreSw: created node ${t} (launch updated).`);
        return { ok: true };
      }

      // component plugin
      const suggestedPlugin = `${prefix}${capObj}Plugin`;
      const pluginName = await vscode.window.showInputBox({
        title: 'coreSw: New component',
        prompt: 'Plugin CMake target (shared lib) name',
        value: suggestedPlugin,
        validateInput: (v) => (validCppIdent_(v) ? undefined : 'invalid C++ identifier')
      });
      if (pluginName === undefined) return { ok: false, cancelled: true };
      const plug = String(pluginName).trim();
      if (!validCppIdent_(plug)) throw new Error(`invalid plugin name: ${plug}`);

      const suggestedComp = `${prefix}${capObj}Component`;
      const componentClassName = await vscode.window.showInputBox({
        title: 'coreSw: New component',
        prompt: 'C++ component class name',
        value: suggestedComp,
        validateInput: (v) => (validCppIdent_(v) ? undefined : 'invalid C++ identifier')
      });
      if (componentClassName === undefined) return { ok: false, cancelled: true };
      const compClass = String(componentClassName).trim();
      if (!validCppIdent_(compClass)) throw new Error(`invalid component class: ${compClass}`);

      const suggestedNs = String(sys).replace(/[^A-Za-z0-9_]/g, '').toLowerCase() || 'ns';
      const cppNamespace = await vscode.window.showInputBox({
        title: 'coreSw: New component',
        prompt: 'C++ namespace',
        value: suggestedNs,
        validateInput: (v) => (validCppIdent_(v) ? undefined : 'invalid C++ identifier')
      });
      if (cppNamespace === undefined) return { ok: false, cancelled: true };
      const cppNs = String(cppNamespace).trim();
      if (!validCppIdent_(cppNs)) throw new Error(`invalid C++ namespace: ${cppNs}`);

      const suggestedType = `${sys}/${capObj}Component`;
      const componentType = await vscode.window.showInputBox({
        title: 'coreSw: New component',
        prompt: 'Component type string (used in launch.json)',
        value: suggestedType,
        validateInput: (v) => (String(v || '').trim() ? undefined : 'required')
      });
      if (componentType === undefined) return { ok: false, cancelled: true };
      const typeStr = String(componentType).trim();

      await createComponentPluginProject_(launchDirAbs, plug, cppNs, compClass, typeStr, deps);

      if (!Array.isArray(cfg.containers)) cfg.containers = [];

      const containerKey = (c) => `${String(c && c.ns ? c.ns : '')}/${String(c && c.name ? c.name : '')}`.replace(/^\/+|\/+$/g, '');
      const preferredKey = String(params.preferredContainerKey || '').trim().replace(/^\/+|\/+$/g, '');

      let container = undefined;
      if (preferredKey) {
        const idx = cfg.containers.findIndex((c) => containerKey(c) === preferredKey);
        if (idx >= 0) container = cfg.containers[idx];
      }

      let pickedContainer = undefined;
      const containerChoices = cfg.containers.map((c, idx) => ({
        label: `${String(c.ns || '')}/${String(c.name || '')}`.replace(/^\/+|\/+$/g, '') || `container#${idx + 1}`,
        description: c.executable ? String(c.executable) : '',
        idx
      }));
      containerChoices.unshift({ label: '+ Create new container', idx: -1 });

      if (!container) {
        pickedContainer = await vscode.window.showQuickPick(containerChoices, {
          title: 'coreSw: New component',
          placeHolder: 'Select a container (SwComponentContainer)'
        });
        if (!pickedContainer) return { ok: false, cancelled: true };
      }

      if (!container && pickedContainer && pickedContainer.idx >= 0) {
        container = cfg.containers[pickedContainer.idx];
      }

      if (!container) {
        const contName = await vscode.window.showInputBox({
          title: 'coreSw: New container',
          prompt: 'Container name (object)',
          value: 'container',
          validateInput: (v) => (String(v || '').trim() ? undefined : 'required')
        });
        if (contName === undefined) return { ok: false, cancelled: true };
        const contNs = await vscode.window.showInputBox({
          title: 'coreSw: New container',
          prompt: 'Container namespace (ns)',
          value: nsName,
          validateInput: (v) => (String(v || '').trim() ? undefined : 'required')
        });
        if (contNs === undefined) return { ok: false, cancelled: true };

        const containerExeAbs = resolveSwComponentContainerExecutable_();
        const containerExeNoExt = containerExeAbs ? stripExtension_(containerExeAbs) : '';
        container = {
          ns: String(contNs).trim(),
          name: String(contName).trim(),
          executable: containerExeNoExt ? relFrom_(launchDirAbs, containerExeNoExt) : '',
          workingDirectory: '.',
          options: defaultProcessOptions_(),
          composition: { threading: 'same_thread', plugins: [], components: [] }
        };
        cfg.containers.push(container);
      }

      if (!container.composition || typeof container.composition !== 'object') {
        container.composition = { threading: 'same_thread', plugins: [], components: [] };
      }
      if (!Array.isArray(container.composition.plugins)) container.composition.plugins = [];
      if (!Array.isArray(container.composition.components)) container.composition.components = [];

      const pluginAbsNoExt = path.join(launchDirAbs, 'install', 'plugins', plug);
      const pluginRel = relFrom_(launchDirAbs, pluginAbsNoExt);
      const normPluginRel = toPosixPath_(pluginRel);
      const existing = container.composition.plugins.map((x) => toPosixPath_(String(x || '')));
      if (!existing.includes(normPluginRel)) container.composition.plugins.push(normPluginRel);

      container.composition.components.push({
        type: typeStr,
        ns: nsName,
        name: objName,
        params: {}
      });

      await fs.promises.writeFile(absPath, JSON.stringify(cfg, null, 2) + '\n', 'utf8');
      this._launchersProvider.refresh();
      vscode.window.showInformationMessage(`coreSw: created component ${compClass} in ${plug} (launch updated).`);
      return { ok: true };
    }

    throw new Error(`unknown action: ${action}`);
  }
}

function dashboardSelectionFromItem_(item) {
  if (!item) return null;
  if (item instanceof TargetItem_) {
    const node = item.node;
    const target = String((node && node.target) || '').trim();
    const domain = String((node && node.domain) || '').trim();
    return { domain, target };
  }
  if (item instanceof DomainItem_) {
    const domain = String(item.domain || '').trim();
    return domain ? { domain } : null;
  }
  return null;
}

async function attachToTarget_(target) {
  const t = String(target || '').trim();
  if (!t) throw new Error('attach: missing target');

  const { domain, object } = splitTargetString_(t);
  if (!domain || !object) throw new Error(`attach: invalid target: ${t}`);

  const info = await swapiNodeInfo_(domain, object);
  if (!info) throw new Error(`attach: swapi node info returned no data for ${t}`);

  const pid = await pickPidFromNode_(info);
  if (!pid) throw new Error(`attach: no PID available for target ${t}`);

  if (!(await pidExists_(pid))) {
    throw new Error(`attach: process PID ${pid} not found (target may have restarted): ${t}`);
  }

  const ok = await attachToPid_(pid, t);
  if (!ok) throw new Error('attach: failed to start debug session');
  return { ok: true, pid: String(pid) };
}

function normalizePid_(pid) {
  const n = Number(pid);
  if (!Number.isFinite(n)) return undefined;
  const i = Math.floor(n);
  return i > 0 ? i : undefined;
}

async function killPidTree_(pid, label) {
  const pidInt = normalizePid_(pid);
  if (!pidInt) throw new Error(`kill: invalid PID: ${pid}`);

  const targetLabel = label ? String(label) : `PID ${pidInt}`;
  const confirm = await vscode.window.showWarningMessage(
    `coreSw: Kill ${targetLabel}?`,
    { modal: true, detail: 'This will force-terminate the process tree.' },
    'Kill'
  );
  if (confirm !== 'Kill') return { ok: false, cancelled: true };

  if (process.platform === 'win32') {
    await new Promise((resolve, reject) => {
      cp.execFile('taskkill', ['/PID', String(pidInt), '/T', '/F'], { encoding: 'utf8' }, (err, stdout, stderr) => {
        if (err) {
          const msg = [
            `taskkill failed for PID ${pidInt}: ${err.message || String(err)}`,
            stderr ? `stderr:\n${stderr}` : '',
            stdout ? `stdout:\n${stdout}` : ''
          ].filter(Boolean).join('\n');
          reject(new Error(msg));
          return;
        }
        resolve();
      });
    });
  } else {
    try {
      process.kill(pidInt, 'SIGKILL');
    } catch (e) {
      throw new Error(`kill failed for PID ${pidInt}: ${e && e.message ? e.message : String(e)}`);
    }
  }

  return { ok: true, pid: pidInt };
}

async function killTargetByTarget_(target) {
  const t = String(target || '').trim();
  if (!t) throw new Error('killTarget: missing target');

  const { domain, object } = splitTargetString_(t);
  if (!domain || !object) throw new Error(`killTarget: invalid target: ${t}`);

  const info = await swapiNodeInfo_(domain, object);
  const pids = normalizePidList_(info && info.pids);
  if (!pids.length) throw new Error(`killTarget: no PID found for target ${t}`);

  const confirm = await vscode.window.showWarningMessage(
    `coreSw: Kill target ${t}?`,
    { modal: true, detail: `PIDs: ${pids.join(', ')}` },
    'Kill'
  );
  if (confirm !== 'Kill') return { ok: false, cancelled: true };

  const results = [];
  for (const pid of pids) {
    try {
      if (process.platform === 'win32') {
        await new Promise((resolve, reject) => {
          cp.execFile('taskkill', ['/PID', String(pid), '/T', '/F'], { encoding: 'utf8' }, (err) => {
            if (err) return reject(err);
            resolve();
          });
        });
      } else {
        process.kill(pid, 'SIGKILL');
      }
      results.push({ pid, ok: true });
    } catch (e) {
      results.push({ pid, ok: false, error: e && e.message ? e.message : String(e) });
    }
  }

  const failed = results.filter((r) => !r.ok);
  if (failed.length) {
    throw new Error(`killTarget: failed to kill ${failed.length}/${results.length} PID(s): ${failed.map((f) => f.pid).join(', ')}`);
  }

  return { ok: true, pids };
}

function parseAppPidList_(appPids) {
  const out = [];
  if (!Array.isArray(appPids)) return out;
  for (const x of appPids) {
    if (typeof x === 'number') {
      const pid = normalizePid_(x);
      if (pid) out.push(pid);
      continue;
    }
    if (isObject_(x) && x.pid !== undefined) {
      const pid = normalizePid_(x.pid);
      if (pid) out.push(pid);
    }
  }
  out.sort((a, b) => a - b);
  return out;
}

async function killDomainByName_(domain) {
  const d = String(domain || '').trim();
  if (!d || d === '<unknown>') throw new Error('killDomain: invalid domain');

  // Union of PIDs from apps + nodes (best-effort).
  let apps = [];
  let nodes = [];
  try {
    const res = await runSwapi_(['app', 'list', '--json']);
    apps = parseJsonStrict_('swapi app list', res.stdout);
  } catch (_) {
    // ignore
  }
  try {
    const res = await runSwapi_(['node', 'list', '--domain', d, '--include-stale', '--json']);
    nodes = parseJsonStrict_('swapi node list', res.stdout);
  } catch (_) {
    // ignore
  }

  const pidSet = new Set();
  if (Array.isArray(apps)) {
    const app = apps.find((a) => isObject_(a) && String(a.domain || '') === d);
    for (const pid of parseAppPidList_(app && app.pids)) pidSet.add(pid);
  }
  if (Array.isArray(nodes)) {
    for (const n of nodes) {
      for (const pid of normalizePidList_(n && n.pids)) pidSet.add(pid);
    }
  }

  const pids = Array.from(pidSet);
  pids.sort((a, b) => a - b);
  if (!pids.length) throw new Error(`killDomain: no PIDs found for domain ${d}`);

  const confirm = await vscode.window.showWarningMessage(
    `coreSw: Kill domain ${d}?`,
    { modal: true, detail: `PIDs: ${pids.join(', ')}` },
    'Kill'
  );
  if (confirm !== 'Kill') return { ok: false, cancelled: true };

  const results = [];
  for (const pid of pids) {
    try {
      if (process.platform === 'win32') {
        await new Promise((resolve, reject) => {
          cp.execFile('taskkill', ['/PID', String(pid), '/T', '/F'], { encoding: 'utf8' }, (err) => {
            if (err) return reject(err);
            resolve();
          });
        });
      } else {
        process.kill(pid, 'SIGKILL');
      }
      results.push({ pid, ok: true });
    } catch (e) {
      results.push({ pid, ok: false, error: e && e.message ? e.message : String(e) });
    }
  }

  const failed = results.filter((r) => !r.ok);
  if (failed.length) {
    throw new Error(`killDomain: failed to kill ${failed.length}/${results.length} PID(s): ${failed.map((f) => f.pid).join(', ')}`);
  }

  return { ok: true, pids };
}

function clampNumber_(v, min, max, fallback) {
  const n = Number(v);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(min, Math.min(max, n));
}

function graphLayoutStorageKey_(domain) {
  const d = String(domain || '').trim();
  return `coreSw.graphLayout.${d}`;
}

function safeFileSegment_(name) {
  let s = String(name || '').trim();
  if (!s) return '';
  s = s.replace(/[<>:"/\\|?*\u0000-\u001F]/g, '_');
  s = s.replace(/\s+/g, '_');
  s = s.replace(/[. ]+$/g, ''); // Windows dislikes trailing dots/spaces
  if (s.length > 120) s = s.slice(0, 120);
  return s;
}

function graphLayoutFilePath_(domain) {
  const ws = workspaceFolderPath_();
  if (!ws) return '';
  const seg = safeFileSegment_(domain) || 'default';
  return path.join(ws, '.coreSw', 'graphLayout', `${seg}.json`);
}

async function readGraphLayoutFile_(domain) {
  const filePath = graphLayoutFilePath_(domain);
  if (!filePath) return { layout: null, path: '' };
  try {
    const txt = await fs.promises.readFile(filePath, 'utf8');
    const obj = JSON.parse(txt);
    return { layout: isObject_(obj) ? obj : null, path: filePath };
  } catch (e) {
    return { layout: null, path: filePath };
  }
}

async function writeGraphLayoutFile_(domain, layout) {
  const filePath = graphLayoutFilePath_(domain);
  if (!filePath) return '';
  try {
    await fs.promises.mkdir(path.dirname(filePath), { recursive: true });
    const body = JSON.stringify(layout, null, 2);
    await fs.promises.writeFile(filePath, body, 'utf8');
    return filePath;
  } catch (e) {
    log_(`[coreSw] graphLayout: failed to write file: ${filePath} (${e && e.message ? e.message : String(e)})`);
    return '';
  }
}

async function deleteGraphLayoutFile_(domain) {
  const filePath = graphLayoutFilePath_(domain);
  if (!filePath) return;
  try {
    await fs.promises.unlink(filePath);
  } catch (e) {
    if (e && e.code === 'ENOENT') return;
    log_(`[coreSw] graphLayout: failed to delete file: ${filePath} (${e && e.message ? e.message : String(e)})`);
  }
}

function sanitizeGraphLayout_(domain, layout) {
  const d = String(domain || '').trim();
  const root = isObject_(layout) ? layout : {};
  const viewIn = isObject_(root.view) ? root.view : {};
  const nodesIn = isObject_(root.nodes) ? root.nodes : {};

  const nodes = {};
  let count = 0;
  for (const k of Object.keys(nodesIn)) {
    if (count >= 5000) break;
    const p = nodesIn[k];
    if (!isObject_(p)) continue;
    const x = Number(p.x);
    const y = Number(p.y);
    if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
    nodes[String(k)] = { x: Math.round(x * 10) / 10, y: Math.round(y * 10) / 10 };
    count++;
  }

  const scale = clampNumber_(viewIn.scale, 0.1, 12, 1);
  const tx = clampNumber_(viewIn.tx, -200000, 200000, 0);
  const ty = clampNumber_(viewIn.ty, -200000, 200000, 0);

  return {
    version: 1,
    domain: d,
    updatedMs: Date.now(),
    nodes,
    view: { scale, tx, ty }
  };
}

async function handleDashboardAction_(action, params) {
  const a = String(action || '').trim();
  const p = isObject_(params) ? params : {};

  if (a === 'ensureBridge') {
    await ensureBridgeRunning_();
    return { ok: true, url: bridgeBaseUrl_() };
  }
  if (a === 'apps') {
    await ensureBridgeRunning_();
    return await bridgeRequestJson_('GET', '/api/apps');
  }
  if (a === 'devices') {
    await ensureBridgeRunning_();
    const domain = String(p.domain || '').trim();
    if (!domain) throw new Error('devices: missing domain');
    return await bridgeRequestJson_('GET', '/api/devices', { query: { domain } });
  }
  if (a === 'connections') {
    await ensureBridgeRunning_();
    const domain = String(p.domain || '').trim();
    if (!domain) throw new Error('connections: missing domain');
    return await bridgeRequestJson_('GET', '/api/connections', { query: { domain } });
  }
  if (a === 'configGet') {
    await ensureBridgeRunning_();
    const target = String(p.target || '').trim();
    if (!target) throw new Error('configGet: missing target');
    return await bridgeRequestJson_('GET', '/api/config', { query: { target } });
  }
  if (a === 'configSetAll') {
    await ensureBridgeRunning_();
    const target = String(p.target || '').trim();
    const config = p.config;
    if (!target) throw new Error('configSetAll: missing target');
    if (!isObject_(config)) throw new Error('configSetAll: missing config (object)');
    return await bridgeRequestJson_('POST', '/api/configAll', { body: { target, config } });
  }
  if (a === 'signals') {
    await ensureBridgeRunning_();
    const target = String(p.target || '').trim();
    if (!target) throw new Error('signals: missing target');
    return await bridgeRequestJson_('GET', '/api/signals', { query: { target } });
  }
  if (a === 'signalEmit') {
    await ensureBridgeRunning_();
    const target = String(p.target || '').trim();
    const name = String(p.name || '').trim();
    const args = Array.isArray(p.args) ? p.args : [];
    if (!target) throw new Error('signalEmit: missing target');
    if (!name) throw new Error('signalEmit: missing name');
    return await bridgeRequestJson_('POST', '/api/signal', { body: { target, name, args } });
  }
  if (a === 'rpcs') {
    await ensureBridgeRunning_();
    const target = String(p.target || '').trim();
    if (!target) throw new Error('rpcs: missing target');
    return await bridgeRequestJson_('GET', '/api/rpcs', { query: { target } });
  }
  if (a === 'rpcCall') {
    await ensureBridgeRunning_();
    const target = String(p.target || '').trim();
    const method = String(p.method || '').trim();
    const args = Array.isArray(p.args) ? p.args : [];
    if (!target) throw new Error('rpcCall: missing target');
    if (!method) throw new Error('rpcCall: missing method');
    return await bridgeRequestJson_('POST', '/api/rpc', { body: { target, method, args } });
  }
  if (a === 'openBridge') {
    const url = bridgeBaseUrl_().replace(/\/+$/, '') + '/';
    vscode.env.openExternal(vscode.Uri.parse(url));
    return { ok: true, url };
  }
  if (a === 'attach') {
    const target = String(p.target || '').trim();
    return await attachToTarget_(target);
  }

  if (a === 'killPid') {
    const pid = p.pid;
    return await killPidTree_(pid);
  }
  if (a === 'killTarget') {
    const target = String(p.target || '').trim();
    return await killTargetByTarget_(target);
  }
  if (a === 'killDomain') {
    const domain = String(p.domain || '').trim();
    return await killDomainByName_(domain);
  }

  if (a === 'graphLayoutGet') {
    const domain = String(p.domain || '').trim();
    if (!domain) throw new Error('graphLayoutGet: missing domain');
    if (!extensionContext_) throw new Error('graphLayoutGet: extension context not ready');
    const fromFile = await readGraphLayoutFile_(domain);
    if (isObject_(fromFile.layout)) {
      return { ok: true, layout: sanitizeGraphLayout_(domain, fromFile.layout), source: 'file', path: fromFile.path };
    }
    const key = graphLayoutStorageKey_(domain);
    const layout = extensionContext_.workspaceState.get(key);
    return { ok: true, layout: isObject_(layout) ? layout : null, source: isObject_(layout) ? 'state' : '', path: fromFile.path };
  }
  if (a === 'graphLayoutSet') {
    const domain = String(p.domain || '').trim();
    if (!domain) throw new Error('graphLayoutSet: missing domain');
    if (!extensionContext_) throw new Error('graphLayoutSet: extension context not ready');
    const layout = sanitizeGraphLayout_(domain, p.layout);
    const key = graphLayoutStorageKey_(domain);
    await extensionContext_.workspaceState.update(key, layout);
    const filePath = await writeGraphLayoutFile_(domain, layout);
    return { ok: true, path: filePath };
  }
  if (a === 'graphLayoutClear') {
    const domain = String(p.domain || '').trim();
    if (!domain) throw new Error('graphLayoutClear: missing domain');
    if (!extensionContext_) throw new Error('graphLayoutClear: extension context not ready');
    const key = graphLayoutStorageKey_(domain);
    await extensionContext_.workspaceState.update(key, undefined);
    await deleteGraphLayoutFile_(domain);
    return { ok: true };
  }

  throw new Error(`unknown action: ${a}`);
}

function openDashboard_(context, targetsProvider, item) {
  const sel = dashboardSelectionFromItem_(item);
  if (sel) dashboardInitSelection_ = sel;

  if (dashboardPanel_) {
    dashboardPanel_.reveal();
    if (dashboardReady_) {
      dashboardPanel_.webview.postMessage({ type: 'settings', dashboardAutoRefreshMs: dashboardAutoRefreshMs_() });
    }
    if (dashboardReady_ && dashboardInitSelection_) {
      dashboardPanel_.webview.postMessage({ type: 'init', selection: dashboardInitSelection_ });
    }
    return;
  }

  const panel = vscode.window.createWebviewPanel(
    'coreSwDashboard',
    'coreSw Dashboard',
    vscode.ViewColumn.Beside,
    {
      enableScripts: true,
      retainContextWhenHidden: true,
      localResourceRoots: [
        vscode.Uri.joinPath(context.extensionUri, 'webview'),
        vscode.Uri.joinPath(context.extensionUri, 'resources')
      ]
    }
  );
  panel.iconPath = vscode.Uri.joinPath(context.extensionUri, 'resources', 'coreSw.svg');

  dashboardPanel_ = panel;
  dashboardReady_ = false;

  panel.onDidDispose(() => {
    dashboardPanel_ = null;
    dashboardReady_ = false;
  });

  panel.webview.html = loadDashboardHtml_(context, panel.webview);

  panel.webview.onDidReceiveMessage(async (msg) => {
    if (!msg || typeof msg !== 'object') return;
    if (msg.type === 'ready') {
      dashboardReady_ = true;
      panel.webview.postMessage({ type: 'settings', dashboardAutoRefreshMs: dashboardAutoRefreshMs_() });
      if (dashboardInitSelection_) {
        panel.webview.postMessage({ type: 'init', selection: dashboardInitSelection_ });
      }
      return;
    }
    if (msg.type !== 'request') return;

    const id = String(msg.id || '');
    const action = String(msg.action || '');
    const params = isObject_(msg.params) ? msg.params : {};
    const shouldRefreshTree = action === 'killPid' || action === 'killTarget' || action === 'killDomain';

    try {
      const data = await handleDashboardAction_(action, params);
      panel.webview.postMessage({ type: 'response', id, ok: true, data });
      if (shouldRefreshTree) targetsProvider.refresh();
    } catch (e) {
      panel.webview.postMessage({
        type: 'response',
        id,
        ok: false,
        error: e && e.message ? e.message : String(e)
      });
    }
  });
}

async function runDiagnostics_() {
  showOutput_();
  log_('[coreSw] --- Diagnostics ---');

  const ws = workspaceFolderPath_();
  if (!ws) {
    const msg = 'coreSw: no workspace folder is open.';
    log_(`[coreSw] ${msg}`);
    vscode.window.showErrorMessage(msg);
    return { ok: false, issues: ['no workspace folder'] };
  }

  const results = [];

  // swapi (required)
  const swapiExe = resolveSwapiExecutable_();
  try {
    const { stdout } = await runSwapi_(['app', 'list', '--json']);
    parseJsonStrict_('swapi app list', stdout);
    results.push({ id: 'swapi', ok: true, detail: swapiExe });
  } catch (e) {
    const err = e && e.message ? e.message : String(e);
    results.push({ id: 'swapi', ok: false, detail: err });
  }

  // SwBridge reachability (optional unless dashboard is used)
  const bridgeUrl = bridgeBaseUrl_();
  try {
    await bridgeRequestJson_('GET', '/api/apps', { timeoutMs: Math.min(800, bridgeRequestTimeoutMs_()) });
    results.push({ id: 'swbridge_http', ok: true, detail: bridgeUrl });
  } catch (e) {
    const err = e && e.message ? e.message : String(e);
    results.push({ id: 'swbridge_http', ok: false, detail: err });
  }

  // SwBridge binary (required only for auto-start)
  const bridgeExe = resolveSwBridgeExecutable_();
  const autoStart = bridgeAutoStart_();
  const bridgeExeOk = !!(bridgeExe && fs.existsSync(bridgeExe));
  results.push({ id: 'swbridge_exe', ok: (!autoStart) || bridgeExeOk, detail: bridgeExe || '(not found)' });

  // SwBuild / SwLaunch (for project world)
  const swBuildExe = resolveSwBuildExecutable_();
  results.push({ id: 'swbuild', ok: !!(swBuildExe && fs.existsSync(swBuildExe)), detail: swBuildExe || '(not found)' });

  const swLaunchExe = resolveSwLaunchExecutable_();
  results.push({ id: 'swlaunch', ok: !!(swLaunchExe && fs.existsSync(swLaunchExe)), detail: swLaunchExe || '(not found)' });

  // SwComponentContainer (needed only for component composition)
  const containerExe = resolveSwComponentContainerExecutable_();
  results.push({ id: 'swcomponentcontainer', ok: !!(containerExe && fs.existsSync(containerExe)), detail: containerExe || '(not found)' });

  for (const r of results) {
    const tag = r.ok ? 'OK' : 'FAIL';
    log_(`[coreSw] ${tag} ${r.id}: ${r.detail}`);
  }

  const failedRequired = results.filter((r) => !r.ok && (r.id === 'swapi' || r.id === 'swbuild' || r.id === 'swlaunch' || r.id === 'swbridge_exe'));
  const failedOptional = results.filter((r) => !r.ok && !failedRequired.includes(r));

  if (!failedRequired.length && !failedOptional.length) {
    vscode.window.showInformationMessage('coreSw: Diagnostics OK.');
    return { ok: true, results };
  }

  const issues = failedRequired.concat(failedOptional).map((r) => r.id);
  const buttons = [];
  if (failedOptional.find((r) => r.id === 'swbridge_http')) buttons.push('Start SwBridge');
  buttons.push('Open Settings');
  buttons.push('Open Output');

  const msg = failedRequired.length
    ? `coreSw: Diagnostics found issues (${issues.join(', ')}).`
    : `coreSw: Diagnostics warnings (${issues.join(', ')}).`;
  const pick = await vscode.window.showWarningMessage(msg, ...buttons);
  if (pick === 'Start SwBridge') {
    await vscode.commands.executeCommand('coreSw.startBridge');
  } else if (pick === 'Open Settings') {
    await vscode.commands.executeCommand('workbench.action.openSettings', '@ext:coresw-local.sw-target-attach coreSw');
  } else if (pick === 'Open Output') {
    showOutput_();
  }

  return { ok: !failedRequired.length, results, issues };
}

function activate(context) {
  extensionContext_ = context;
  const targetsProvider = new CoreSwTargetsProvider_();
  const launchersProvider = new CoreSwLaunchersProvider_();
  const projectViewProvider = new CoreSwProjectViewProvider_(context, launchersProvider);

  let refreshDebounceT = 0;
  requestRefreshAll_ = () => {
    if (refreshDebounceT) clearTimeout(refreshDebounceT);
    refreshDebounceT = setTimeout(() => {
      targetsProvider.refresh();
      launchersProvider.refresh();
    }, 100);
  };

  context.subscriptions.push(
    vscode.window.registerTreeDataProvider('coreSwTargets', targetsProvider)
  );
  context.subscriptions.push(
    vscode.window.registerTreeDataProvider('coreSwLaunchers', launchersProvider)
  );
  context.subscriptions.push(
    vscode.window.registerWebviewViewProvider('coreSwProject', projectViewProvider, { webviewOptions: { retainContextWhenHidden: true } })
  );

  // Auto refresh (targets + launchers) without disrupting open UI state.
  let autoRefreshTimer = 0;
  const resetAutoRefresh_ = () => {
    if (autoRefreshTimer) clearInterval(autoRefreshTimer);
    autoRefreshTimer = 0;

    const ms = targetsAutoRefreshMs_();
    if (ms > 0) {
      autoRefreshTimer = setInterval(() => {
        targetsProvider.refresh();
        launchersProvider.refresh();
      }, ms);
    }

    if (dashboardPanel_ && dashboardReady_) {
      dashboardPanel_.webview.postMessage({ type: 'settings', dashboardAutoRefreshMs: dashboardAutoRefreshMs_() });
    }
  };
  resetAutoRefresh_();
  context.subscriptions.push({ dispose: () => { if (autoRefreshTimer) clearInterval(autoRefreshTimer); } });

  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration('coreSw.autoRefreshMs') || e.affectsConfiguration('coreSw.dashboardAutoRefreshMs')) {
        resetAutoRefresh_();
      }
      if (
        e.affectsConfiguration('coreSw.launcherSearchPatterns') ||
        e.affectsConfiguration('coreSw.launcherSearchExclude') ||
        e.affectsConfiguration('coreSw.launcherMaxResults')
      ) {
        launchersProvider.refresh();
      }
    })
  );

  // Watch for launcher JSON changes.
  let launcherWatchDebounce = 0;
  const onLauncherFsEvent_ = () => {
    if (launcherWatchDebounce) clearTimeout(launcherWatchDebounce);
    launcherWatchDebounce = setTimeout(() => launchersProvider.refresh(), 200);
  };
  try {
    const watcher = vscode.workspace.createFileSystemWatcher('**/*launch*.json');
    watcher.onDidCreate(onLauncherFsEvent_);
    watcher.onDidChange(onLauncherFsEvent_);
    watcher.onDidDelete(onLauncherFsEvent_);
    context.subscriptions.push(watcher);
    context.subscriptions.push({ dispose: () => { if (launcherWatchDebounce) clearTimeout(launcherWatchDebounce); } });
  } catch (_) {
    // ignore
  }

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.refreshTargets', () => {
      targetsProvider.refresh();
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.refreshLaunchers', () => {
      launchersProvider.refresh();
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.diagnostics', async () => {
      try {
        await vscode.window.withProgress(
          { location: vscode.ProgressLocation.Notification, title: 'coreSw: diagnostics...' },
          async () => {
            await runDiagnostics_();
          }
        );
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.openDashboard', (item) => {
      try {
        openDashboard_(context, targetsProvider, item);
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.startBridge', async () => {
      try {
        await vscode.window.withProgress(
          { location: vscode.ProgressLocation.Notification, title: 'coreSw: starting SwBridge...' },
          async () => {
            await ensureBridgeRunning_();
          }
        );
        vscode.window.showInformationMessage(`coreSw: SwBridge ready at ${bridgeBaseUrl_()}`);
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.stopBridge', async () => {
      try {
        if (!bridgeProc_ || !bridgeStartedByUs_) {
          vscode.window.showInformationMessage('coreSw: SwBridge is not started by this extension.');
          return;
        }
        await stopBridgeProcess_();
        vscode.window.showInformationMessage('coreSw: SwBridge stopped.');
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.restartBridge', async () => {
      try {
        await stopBridgeProcess_();
        await vscode.window.withProgress(
          { location: vscode.ProgressLocation.Notification, title: 'coreSw: restarting SwBridge...' },
          async () => {
            await ensureBridgeRunning_();
          }
        );
        vscode.window.showInformationMessage(`coreSw: SwBridge ready at ${bridgeBaseUrl_()}`);
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.copyPid', async (item) => {
      try {
        let node = item && item.node;
        if (!node) {
          vscode.window.showWarningMessage('coreSw: select a target to copy its PID.');
          return;
        }

        node = await refreshNodeInfo_(node);
        const pid = await pickPidFromNode_(node);
        if (!pid) {
          vscode.window.showErrorMessage('coreSw: no PID available for this target.');
          return;
        }

        await vscode.env.clipboard.writeText(String(pid));
        vscode.window.showInformationMessage(`coreSw: copied PID ${pid}`);
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.attachToPid', async (item) => {
      let node = item && item.node;
      if (!node) {
        vscode.window.showWarningMessage('coreSw: select a target to attach.');
        return false;
      }

      node = await refreshNodeInfo_(node);
      const pid = await pickPidFromNode_(node);
      if (!pid) {
        vscode.window.showErrorMessage('coreSw: no PID available for this target.');
        return false;
      }

      if (!(await pidExists_(pid))) {
        const { domain, object } = resolveDomainObjectFromNode_(node);
        const label = String(node.target || '') || (domain && object ? `${domain}/${object}` : '');
        vscode.window.showErrorMessage(`coreSw: process PID ${pid} not found (target may have restarted): ${label}`);
        targetsProvider.refresh();
        return false;
      }

      const { domain, object } = resolveDomainObjectFromNode_(node);
      const label = String(node.target || '') || (domain && object ? `${domain}/${object}` : '');
      return await attachToPid_(pid, label);
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.killPid', async (pidArg) => {
      try {
        let pid = pidArg;
        if (!pid) {
          const typed = await vscode.window.showInputBox({
            title: 'coreSw: Kill PID',
            prompt: 'Enter a PID to force-kill (process tree)',
            validateInput: (v) => (normalizePid_(v) ? undefined : 'Enter a positive integer PID')
          });
          if (!typed) return;
          pid = typed.trim();
        }
        const res = await killPidTree_(pid);
        if (res && res.ok) vscode.window.showInformationMessage(`coreSw: killed PID ${res.pid}`);
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.killTarget', async (item) => {
      try {
        let target = '';
        if (item && item.node) {
          const node = await refreshNodeInfo_(item.node);
          target = String((node && node.target) || '').trim();
        }
        if (!target) {
          const typed = await vscode.window.showInputBox({
            title: 'coreSw: Kill target',
            prompt: 'Enter a target as <sys>/<target> (e.g. pn/pn/alpha)',
            validateInput: (v) => (splitTargetString_(v).domain && splitTargetString_(v).object) ? undefined : 'Expected <sys>/<target>'
          });
          if (!typed) return;
          target = typed.trim();
        }

        const res = await killTargetByTarget_(target);
        if (res && res.ok) {
          vscode.window.showInformationMessage(`coreSw: killed target ${target} (pids=${(res.pids || []).join(',')})`);
          targetsProvider.refresh();
        }
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.killDomain', async (item) => {
      try {
        let domain = '';
        if (item && item.domain) domain = String(item.domain || '').trim();
        if (!domain || domain === '<unknown>') {
          domain = await pickDomain_('');
          if (domain === undefined) return;
          if (!domain) {
            vscode.window.showWarningMessage('coreSw: refusing to kill all domains. Pick a specific domain.');
            return;
          }
        }

        const res = await killDomainByName_(domain);
        if (res && res.ok) {
          vscode.window.showInformationMessage(`coreSw: killed domain ${domain} (pids=${(res.pids || []).join(',')})`);
          targetsProvider.refresh();
        }
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.openLauncherFile', async (item) => {
      try {
        const meta = item && item.launcher;
        const absPath = meta && meta.absPath ? String(meta.absPath) : '';
        if (!absPath) {
          vscode.window.showWarningMessage('coreSw: select a launcher JSON file.');
          return;
        }
        const doc = await vscode.workspace.openTextDocument(vscode.Uri.file(absPath));
        await vscode.window.showTextDocument(doc, { preview: false });
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.launchLauncher', async (item) => {
      try {
        let meta = item && item.launcher;
        if (!meta) {
          await launchersProvider.ensureLoaded();
          const all = launchersProvider.allLaunchers();
          if (!all.length) {
            vscode.window.showWarningMessage('coreSw: no launcher JSON found in this workspace.');
            return;
          }
          const pick = await vscode.window.showQuickPick(
            all.map((m) => ({
              label: String(m.relPath || path.basename(m.absPath || 'launcher')),
              description: `sys=${m.sys} nodes=${m.nodesCount || 0} containers=${m.containersCount || 0}`,
              meta: m
            })),
            { title: 'coreSw: Select a launcher to run', matchOnDescription: true }
          );
          if (!pick) return;
          meta = pick.meta;
        }

        const details = [
          `sys: ${meta.sys}`,
          `nodes: ${meta.nodesCount || 0}`,
          `containers: ${meta.containersCount || 0}`,
          `duration_ms: ${meta.durationMs || 0}`,
          `file: ${meta.relPath || meta.absPath}`
        ].join('\n');

        const confirm = await vscode.window.showWarningMessage(
          `coreSw: Launch ${meta.relPath || path.basename(meta.absPath)}?`,
          { modal: true, detail: details },
          'Launch'
        );
        if (confirm !== 'Launch') return;

        await startSwLaunch_(meta);
        requestRefreshAll_();
        vscode.window.showInformationMessage(`coreSw: SwLaunch started (sys=${meta.sys}).`);
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('coreSw.stopLauncher', async (item) => {
      try {
        let meta = item && item.launcher;
        if (!meta) {
          const running = [];
          for (const entry of launchProcs_.values()) {
            if (!entry || !isProcAlive_(entry.proc) || !entry.meta) continue;
            running.push(entry.meta);
          }
          if (!running.length) {
            vscode.window.showInformationMessage('coreSw: no running SwLaunch process.');
            return;
          }
          const pick = await vscode.window.showQuickPick(
            running.map((m) => ({
              label: String(m.relPath || path.basename(m.absPath || 'launcher')),
              description: `sys=${m.sys}`,
              meta: m
            })),
            { title: 'coreSw: Stop which launcher?' }
          );
          if (!pick) return;
          meta = pick.meta;
        }

        const entry = getLaunchEntry_(meta.absPath);
        const pid = entry && entry.proc ? entry.proc.pid : 0;
        const confirm = await vscode.window.showWarningMessage(
          `coreSw: Stop launcher ${meta.relPath || path.basename(meta.absPath)}?`,
          { modal: true, detail: pid ? `pid=${pid}` : '' },
          'Stop'
        );
        if (confirm !== 'Stop') return;

        await stopSwLaunch_(meta.absPath);
        requestRefreshAll_();
        vscode.window.showInformationMessage('coreSw: launcher stopped.');
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('sw.pickTargetPid', async () => {
      try {
        return await vscode.window.withProgress(
          { location: vscode.ProgressLocation.Notification, title: 'Sw: resolving target PID...' },
          async () => await pickTargetPid_()
        );
      } catch (e) {
        vscode.window.showErrorMessage(e && e.message ? e.message : String(e));
        return undefined;
      }
    })
  );

  context.subscriptions.push({
    dispose: () => {
      // Best-effort cleanup of SwBridge we started.
      if (bridgeProc_ && bridgeStartedByUs_) {
        stopBridgeProcess_().catch(() => {});
      }
      if (outputChannel_) {
        outputChannel_.dispose();
        outputChannel_ = null;
      }
    }
  });
}

function deactivate() {}

module.exports = { activate, deactivate };
