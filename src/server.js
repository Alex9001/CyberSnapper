const http = require('http');
const fs = require('fs');
const path = require('path');
const { exec } = require('child_process');
const { capture } = require('./capture');
const { generateFilename } = require('./naming');
const config = require('./config');

const SCREENSHOTS_PATH = path.join(process.cwd(), 'screenshots');

function openFolder() {
  if (!fs.existsSync(SCREENSHOTS_PATH)) fs.mkdirSync(SCREENSHOTS_PATH, { recursive: true });
  const cmd = process.platform === 'win32' ? `start "" "${SCREENSHOTS_PATH}"`
    : process.platform === 'darwin' ? `open "${SCREENSHOTS_PATH}"`
    : `xdg-open "${SCREENSHOTS_PATH}"`;
  exec(cmd, () => {});
}

function json(res, code, data) {
  res.writeHead(code, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify(data));
}

const MIME = {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'application/javascript',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
};

async function startServer(port = 0) {
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

    if (url.pathname === '/shutdown') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: true }));
      server.close(() => process.exit(0));
      return;
    }

    if (url.pathname === '/' || url.pathname === '/index.html') {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end(UI_HTML);
      return;
    }

    if (url.pathname === '/config' && req.method === 'GET') {
      json(res, 200, config.load());
      return;
    }

    if (url.pathname === '/config' && req.method === 'PUT') {
      let body = '';
      req.on('data', chunk => body += chunk);
      req.on('end', () => {
        try {
          const data = JSON.parse(body);
          if (!Array.isArray(data.presets)) throw new Error();
          config.save(data);
          json(res, 200, { ok: true });
        } catch {
          json(res, 400, { error: 'Invalid config' });
        }
      });
      return;
    }

    if (url.pathname === '/capture' && req.method === 'POST') {
      let body = '';
      req.on('data', chunk => body += chunk);
      req.on('end', async () => {
        let urls, viewports, naming;
        try {
          const parsed = JSON.parse(body);
          urls = parsed.urls;
          viewports = parsed.presets || config.getPresets();
          naming = parsed.naming || config.getNaming();
          if (!Array.isArray(urls) || urls.length === 0) throw new Error();
        } catch {
          json(res, 400, { error: 'Invalid body — expected { urls: [...], presets: [...] }' });
          return;
        }

        res.writeHead(200, {
          'Content-Type': 'text/event-stream',
          'Cache-Control': 'no-cache',
          'Connection': 'keep-alive',
          'Access-Control-Allow-Origin': '*',
        });

        try {
          await capture(urls, viewports, (event) => {
            res.write(`data: ${JSON.stringify(event)}\n\n`);
          }, naming);
        } catch (err) {
          res.write(`data: ${JSON.stringify({ type: 'error', message: err.message })}\n\n`);
        }
        res.end();
      });
      return;
    }

    if (url.pathname.startsWith('/screenshots/')) {
      const filename = path.basename(url.pathname);
      const filePath = path.join(SCREENSHOTS_PATH, filename);

      if (!filePath.startsWith(SCREENSHOTS_PATH)) {
        res.writeHead(403);
        res.end('Forbidden');
        return;
      }

      fs.readFile(filePath, (err, data) => {
        if (err) {
          res.writeHead(404);
          res.end('Not found');
          return;
        }
        const ext = path.extname(filename).toLowerCase();
        res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
        res.end(data);
      });
      return;
    }

    if (url.pathname === '/preview' && req.method === 'POST') {
      let body = '';
      req.on('data', chunk => body += chunk);
      req.on('end', () => {
        try {
          const { template, url: sampleUrl, preset } = JSON.parse(body);
          const samplePreset = preset || { name: 'Desktop', width: 1920, height: 1080 };
          const result = generateFilename(template || '{hostname}-{preset}', sampleUrl || 'https://example.com', samplePreset, 0);
          json(res, 200, { preview: result.subdir ? result.subdir + '/' + result.filename : result.filename });
        } catch {
          json(res, 400, { error: 'Invalid preview request' });
        }
      });
      return;
    }

    if (url.pathname === '/open-folder') {
      openFolder();
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: true }));
      return;
    }

    res.writeHead(404);
    res.end('Not found');
  });

  return new Promise((resolve) => {
    server.listen(port, () => resolve(server));
  });
}

const UI_HTML = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CyberSnapper</title>
<style>
  :root {
    --bg: #f8f9fa; --surface: #ffffff; --border: #dee2e6;
    --text: #212529; --text-secondary: #6c757d;
    --primary: #4361ee; --primary-hover: #3a56d4;
    --success: #2ec4b6; --error: #e71d36; --accent: #ff9f1c;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #0f1117; --surface: #1a1b26; --border: #2a2b3d;
      --text: #e1e2e8; --text-secondary: #9ca0b0;
      --primary: #7c8aff; --primary-hover: #6a78e8;
      --success: #2ec4b6; --error: #ff6b6b; --accent: #ff9f1c;
    }
  }
  .light { --bg:#f8f9fa;--surface:#fff;--border:#dee2e6;--text:#212529;--text-secondary:#6c757d;--primary:#4361ee;--primary-hover:#3a56d4;--success:#2ec4b6;--error:#e71d36;--accent:#ff9f1c; }
  .dark { --bg:#0f1117;--surface:#1a1b26;--border:#2a2b3d;--text:#e1e2e8;--text-secondary:#9ca0b0;--primary:#7c8aff;--primary-hover:#6a78e8;--success:#2ec4b6;--error:#ff6b6b;--accent:#ff9f1c; }
  *,*::before,*::after { box-sizing:border-box; margin:0; padding:0; }
  body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif; background:var(--bg); color:var(--text); min-height:100vh; transition:background .3s,color .3s; }
  .container { max-width:880px; margin:0 auto; padding:24px 20px; }
  header { display:flex; align-items:center; justify-content:space-between; margin-bottom:32px; padding-bottom:16px; border-bottom:1px solid var(--border); }
  header h1 { font-size:24px; font-weight:700; display:flex; align-items:center; gap:10px; }
  header h1 span { color:var(--primary); }
  .theme-toggle { background:none; border:1px solid var(--border); border-radius:8px; padding:6px 12px; cursor:pointer; color:var(--text); font-size:14px; }
  .theme-toggle:hover { background:var(--border); }
  .card { background:var(--surface); border-radius:12px; border:1px solid var(--border); padding:24px; margin-bottom:20px; }
  .card h2 { font-size:15px; font-weight:600; margin-bottom:12px; color:var(--text-secondary); text-transform:uppercase; letter-spacing:.5px; display:flex; align-items:center; justify-content:space-between; }
  textarea { width:100%; min-height:120px; padding:14px; border:1px solid var(--border); border-radius:8px; background:var(--bg); color:var(--text); font-family:'SF Mono','Fira Code',monospace; font-size:13px; line-height:1.6; resize:vertical; outline:none; }
  textarea:focus { border-color:var(--primary); }
  textarea::placeholder { color:var(--text-secondary); }
  .actions { display:flex; gap:10px; align-items:center; margin-top:16px; flex-wrap:wrap; }
  .btn { padding:10px 24px; border-radius:8px; border:none; font-size:14px; font-weight:600; cursor:pointer; display:inline-flex; align-items:center; gap:6px; transition:opacity .2s; }
  .btn-primary { background:var(--primary); color:#fff; }
  .btn-primary:hover { opacity:.9; }
  .btn-primary:disabled { opacity:.5; cursor:not-allowed; }
  .btn-outline { background:none; border:1px solid var(--border); color:var(--text); }
  .btn-outline:hover { background:var(--border); }
  .btn-sm { padding:6px 14px; font-size:13px; }
  .btn-danger { background:var(--error); color:#fff; }
  .btn-danger:hover { opacity:.9; }
  .hint { font-size:13px; color:var(--text-secondary); }

  .preset-grid { display:flex; flex-wrap:wrap; gap:8px; margin-bottom:12px; }
  .preset-chip { display:flex; align-items:center; gap:6px; padding:4px 8px 4px 4px; border-radius:8px; border:1px solid var(--border); background:var(--bg); font-size:13px; cursor:pointer; user-select:none; transition:border-color .2s,background .2s; }
  .preset-chip:hover { border-color:var(--primary); }
  .preset-chip.selected { border-color:var(--success); background:color-mix(in srgb,var(--success) 10%,var(--bg)); }
  .preset-chip input { accent-color:var(--success); }
  .preset-chip .dim { color:var(--text-secondary); font-size:11px; }
  .preset-chip .del { cursor:pointer; opacity:.4; font-size:14px; line-height:1; }
  .preset-chip .del:hover { opacity:1; color:var(--error); }

  .add-preset-row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin-top:8px; }
  .add-preset-row input { padding:8px 10px; border:1px solid var(--border); border-radius:6px; background:var(--bg); color:var(--text); font-size:13px; outline:none; width:100px; }
  .add-preset-row input:focus { border-color:var(--primary); }
  .add-preset-row input.name-input { width:120px; }

  #progress { display:none; }
  #progress.active { display:block; }
  .url-result { margin-bottom:16px; padding-bottom:16px; border-bottom:1px solid var(--border); }
  .url-result:last-child { border-bottom:none; margin-bottom:0; padding-bottom:0; }
  .url-result .url-line { display:flex; align-items:center; gap:8px; margin-bottom:8px; font-size:14px; font-weight:500; }
  .url-result .url-line .status-icon { font-size:16px; }
  .url-result .url-line .url-text { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  .url-result .url-line .badge { font-size:11px; padding:2px 8px; border-radius:4px; background:var(--border); margin-left:auto; white-space:nowrap; }
  .badge.done { background:var(--success); color:#fff; }
  .badge.error { background:var(--error); color:#fff; }
  .viewports { display:flex; gap:6px; flex-wrap:wrap; margin-left:24px; }
  .vp-item { padding:4px 10px; border-radius:6px; font-size:12px; background:var(--bg); border:1px solid var(--border); display:flex; align-items:center; gap:4px; }
  .vp-item.done { border-color:var(--success); color:var(--success); }
  .vp-item.error { border-color:var(--error); color:var(--error); }
  .vp-item.active { border-color:var(--accent); color:var(--accent); animation:pulse 1s infinite; }
  @keyframes pulse { 0%,100% { opacity:1; } 50% { opacity:.5; } }
  .summary { margin-top:16px; padding-top:16px; border-top:1px solid var(--border); display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px; }
  .summary .count { font-size:14px; color:var(--text-secondary); }
  .summary .count strong { color:var(--text); }
  #snap-count { display:none; }
  #open-folder-btn { display:none; }
  #open-folder-btn.visible { display:inline-flex; }
  .gallery { display:grid; grid-template-columns:repeat(auto-fill,minmax(160px,1fr)); gap:10px; margin-top:12px; }
  .gallery-item { border-radius:8px; overflow:hidden; border:1px solid var(--border); cursor:pointer; transition:transform .2s; position:relative; }
  .gallery-item:hover { transform:scale(1.03); }
  .gallery-item img { width:100%; display:block; }
  .gallery-item .label { padding:5px 8px; font-size:11px; color:var(--text-secondary); background:var(--surface); }
  .nv { display:inline-block; padding:1px 6px; border-radius:4px; background:var(--border); cursor:pointer; font-family:'SF Mono','Fira Code',monospace; font-size:11px; margin:2px 0; transition:background .2s; }
  .nv:hover { background:var(--primary); color:#fff; }
</style>
</head>
<body>
<div class="container">
  <header>
    <h1><span>⌁</span> CyberSnapper</h1>
    <div style="display:flex;gap:8px">
      <button class="theme-toggle" id="theme-toggle" title="Toggle theme">☀ / ☾</button>
      <button class="theme-toggle" id="stop-btn" title="Stop server" style="color:var(--error)">⏹ Stop</button>
    </div>
  </header>

  <div class="card">
    <h2>URLs</h2>
    <textarea id="urls-input" placeholder="https://example.com&#10;https://google.com&#10;one-per-line" spellcheck="false"></textarea>
  </div>

  <div class="card">
    <h2><span>Resolutions</span> <button class="btn btn-outline btn-sm" id="reset-presets-btn">Reset</button></h2>
    <div id="preset-list" class="preset-grid"></div>
    <div class="add-preset-row">
      <input class="name-input" id="new-name" placeholder="Name" maxlength="30">
      <input type="number" id="new-width" placeholder="Width" min="1">
      <span>×</span>
      <input type="number" id="new-height" placeholder="Height" min="1">
      <button class="btn btn-primary btn-sm" id="add-preset-btn">+ Add</button>
    </div>
  </div>

  <div class="card">
    <h2>Naming</h2>
    <div style="margin-bottom:10px;font-size:12px;color:var(--text-secondary)">Variables  —
      <span class="nv" data-v="{hostname}">{hostname}</span>
      <span class="nv" data-v="{preset}">{preset}</span>
      <span class="nv" data-v="{width}">{width}</span>
      <span class="nv" data-v="{height}">{height}</span>
      <span class="nv" data-v="{domain}">{domain}</span>
      <span class="nv" data-v="{date}">{date}</span>
      <span class="nv" data-v="{time}">{time}</span>
      <span class="nv" data-v="{index}">{index}</span>
    </div>
    <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap">
      <input id="naming-template" value="{hostname}-{preset}" style="flex:1;min-width:200px;padding:8px 10px;border:1px solid var(--border);border-radius:6px;background:var(--bg);color:var(--text);font-family:'SF Mono','Fira Code',monospace;font-size:13px;outline:none">
      <button class="btn btn-outline btn-sm" id="preview-btn">Preview</button>
    </div>
    <div style="margin-top:8px;display:flex;gap:10px;align-items:center;flex-wrap:wrap">
      <button class="btn btn-outline btn-sm" data-template="{hostname}-{preset}">Default</button>
      <button class="btn btn-outline btn-sm" data-template="{hostname}-{width}x{height}">By size</button>
      <button class="btn btn-outline btn-sm" data-template="{date}/{hostname}-{preset}">By date</button>
      <button class="btn btn-outline btn-sm" data-template="{index}-{hostname}-{preset}">Indexed</button>
      <button class="btn btn-outline btn-sm" data-template="{domain}/{preset}/{hostname}">By domain</button>
    </div>
    <div id="naming-preview" style="margin-top:8px;font-size:13px;color:var(--text-secondary)"></div>
  </div>

  <div class="actions" style="margin-bottom:20px">
    <button class="btn btn-primary" id="snap-btn">📸 Snap!</button>
    <span class="hint">or run with arguments for CLI mode</span>
  </div>

  <div class="card" id="progress">
    <h2>Capturing</h2>
    <div id="results"></div>
    <div class="summary">
      <div class="count" id="snap-count">Captured <strong id="done-count">0</strong> screenshots</div>
      <div><button class="btn btn-outline" id="open-folder-btn">📁 Open folder</button></div>
    </div>
    <div id="gallery" class="gallery"></div>
  </div>
</div>
<script>
const themeToggle = document.getElementById('theme-toggle');
const urlInput = document.getElementById('urls-input');
const snapBtn = document.getElementById('snap-btn');
const progressEl = document.getElementById('progress');
const resultsEl = document.getElementById('results');
const doneCount = document.getElementById('done-count');
const snapCount = document.getElementById('snap-count');
const openFolderBtn = document.getElementById('open-folder-btn');
const gallery = document.getElementById('gallery');
const presetList = document.getElementById('preset-list');
const newName = document.getElementById('new-name');
const newWidth = document.getElementById('new-width');
const newHeight = document.getElementById('new-height');
const addBtn = document.getElementById('add-preset-btn');
const resetBtn = document.getElementById('reset-presets-btn');
const namingInput = document.getElementById('naming-template');
const previewDiv = document.getElementById('naming-preview');
const previewBtn = document.getElementById('preview-btn');
let presets = [];
let totalSnaps = 0;
let urlIndex = 0;

function setTheme(t) { document.documentElement.className = t; localStorage.setItem('theme', t); }
function getTheme() { return localStorage.getItem('theme') || (window.matchMedia('(prefers-color-scheme:dark)').matches ? 'dark' : 'light'); }
setTheme(getTheme());
themeToggle.addEventListener('click', () => setTheme(document.documentElement.className === 'dark' ? 'light' : 'dark'));

async function loadPresets() {
  try {
    const res = await fetch('/config');
    const data = await res.json();
    presets = data.presets || [];
    renderPresets();
  } catch {}
}

function renderPresets() {
  presetList.innerHTML = presets.map((p, i) =>
    '<label class="preset-chip selected">' +
    '<input type="checkbox" checked data-i="' + i + '">' +
    esc(p.name) + ' <span class="dim">' + p.width + '×' + p.height + '</span>' +
    '<span class="del" data-i="' + i + '">✕</span></label>'
  ).join('');
  presetList.querySelectorAll('input[type=checkbox]').forEach(cb =>
    cb.addEventListener('change', () => cb.closest('.preset-chip').classList.toggle('selected', cb.checked))
  );
  presetList.querySelectorAll('.del').forEach(el =>
    el.addEventListener('click', async e => {
      e.preventDefault();
      const i = +e.target.dataset.i;
      presets.splice(i, 1);
      await savePresets();
      renderPresets();
    })
  );
}

async function savePresets() {
  await fetch('/config', { method:'PUT', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ presets }) });
}

addBtn.addEventListener('click', async () => {
  const name = newName.value.trim();
  const w = +newWidth.value;
  const h = +newHeight.value;
  if (!name || !w || !h) return;
  if (presets.some(p => p.name.toLowerCase() === name.toLowerCase())) return;
  presets.push({ name, width:w, height:h });
  await savePresets();
  newName.value = ''; newWidth.value = ''; newHeight.value = '';
  renderPresets();
});

resetBtn.addEventListener('click', async () => {
  await fetch('/config', {
    method:'PUT', headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ presets:[], naming: { template: namingInput.value.trim() || '{hostname}-{preset}' } })
  });
  await loadPresets();
});

// Enter key to add
[newName, newWidth, newHeight].forEach(el => el.addEventListener('keydown', e => { if (e.key === 'Enter') addBtn.click(); }));

snapBtn.addEventListener('click', startCapture);

async function startCapture() {
  const urls = urlInput.value.trim().split('\\n').map(l => l.trim()).filter(l => l.length > 0);
  if (urls.length === 0) return;
  const selected = presets.filter((_, i) => {
    const cb = presetList.querySelector('input[data-i="' + i + '"]');
    return cb ? cb.checked : true;
  });
  if (selected.length === 0) return;

  snapBtn.disabled = true; snapBtn.textContent = '⏳ Capturing...';
  progressEl.classList.add('active'); resultsEl.innerHTML = ''; gallery.innerHTML = '';
  totalSnaps = 0; urlIndex = 0;
  snapCount.style.display = 'none'; openFolderBtn.classList.remove('visible'); doneCount.textContent = '0';
  try {
    const res = await fetch('/capture', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ urls, presets: selected }) });
    const reader = res.body.getReader(); const dec = new TextDecoder(); let buf = '';
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += dec.decode(value, { stream: true });
      const lines = buf.split('\\n'); buf = lines.pop() || '';
      for (const l of lines) { if (l.startsWith('data: ')) { try { handleEvent(JSON.parse(l.slice(6))); } catch {} } }
    }
  } catch (err) { resultsEl.innerHTML += '<div class="url-result">Connection error: ' + esc(err.message) + '</div>'; }
  finally { snapBtn.disabled = false; snapBtn.textContent = '📸 Snap!'; }
}

function handleEvent(e) {
  switch (e.type) {
    case 'url-start': {
      urlIndex = e.index;
      const d = document.createElement('div'); d.className = 'url-result'; d.id = 'url-' + e.index;
      d.innerHTML = '<div class="url-line"><span class="status-icon">⏳</span><span class="url-text">' + esc(e.url) + '</span><span class="badge">' + (e.index+1) + '/' + e.total + '</span></div><div class="viewports" id="vps-' + e.index + '"></div>';
      resultsEl.appendChild(d); break;
    }
    case 'url-error': { const el = resultsEl.lastChild; if (el) { el.querySelector('.status-icon').textContent = '❌'; el.querySelector('.badge').textContent = 'invalid'; } break; }
    case 'url-done': { const el = document.getElementById('url-' + urlIndex); if (el) { el.querySelector('.status-icon').textContent = '✅'; el.querySelector('.badge').className = 'badge done'; el.querySelector('.badge').textContent = 'done'; } break; }
    case 'viewport-start': { const idx = e.index != null ? e.index : urlIndex; const vps = document.getElementById('vps-' + idx); if (vps) { const item = document.createElement('span'); item.className = 'vp-item active'; item.id = 'vp-' + idx + '-' + slug(e.viewport); item.textContent = '▸ ' + e.viewport; vps.appendChild(item); } break; }
    case 'viewport-error': { const idx = e.index != null ? e.index : urlIndex; const item = document.getElementById('vp-' + idx + '-' + slug(e.viewport)); if (item) { item.className = 'vp-item error'; item.textContent = '✗ ' + e.viewport; } break; }
    case 'viewport-done': {
      const idx = e.index != null ? e.index : urlIndex;
      const item = document.getElementById('vp-' + idx + '-' + slug(e.viewport)); if (item) { item.className = 'vp-item done'; item.textContent = '✓ ' + e.viewport; }
      totalSnaps++; doneCount.textContent = totalSnaps; snapCount.style.display = 'block'; openFolderBtn.classList.add('visible');
      const fn = (e.file || '').split(/[/\\\\]/).pop();
      const t = document.createElement('div'); t.className = 'gallery-item';
      t.innerHTML = '<img src="/screenshots/' + fn + '" loading="lazy" alt="' + esc(fn) + '"><div class="label">' + esc(fn.replace('.png','')) + '</div>';
      t.addEventListener('click', () => window.open('/screenshots/' + fn, '_blank'));
      gallery.prepend(t); break;
    }
  }
}

openFolderBtn.addEventListener('click', () => fetch('/open-folder').catch(() => {}));
document.getElementById('stop-btn').addEventListener('click', () => {
  if (confirm('Stop the server and exit CyberSnapper?')) {
    fetch('/shutdown').catch(() => {});
  }
});
function esc(t) { const d = document.createElement('div'); d.textContent = t; return d.innerHTML; }
function slug(s) { return s.replace(/[^a-z0-9]/gi,'_').toLowerCase(); }

// — naming —
document.querySelectorAll('.nv').forEach(el => el.addEventListener('click', () => {
  const v = el.dataset.v;
  const start = namingInput.selectionStart;
  const end = namingInput.selectionEnd;
  const val = namingInput.value;
  namingInput.value = val.slice(0, start) + v + val.slice(end);
  namingInput.selectionStart = namingInput.selectionEnd = start + v.length;
  namingInput.focus();
  updatePreview();
}));

document.querySelectorAll('[data-template]').forEach(el => el.addEventListener('click', () => {
  namingInput.value = el.dataset.template;
  updatePreview();
}));

async function updatePreview() {
  const template = namingInput.value.trim() || '{hostname}-{preset}';
  const sampleUrl = (urlInput.value.trim().split('\\n').filter(l => l.trim())[0]) || 'https://example.com';
  const samplePreset = presets.length > 0 ? presets[0] : { name: 'Desktop', width: 1920, height: 1080 };
  try {
    const res = await fetch('/preview', {
      method:'POST', headers:{'Content-Type':'application/json'},
      body:JSON.stringify({ template, url: sampleUrl, preset: samplePreset })
    });
    const data = await res.json();
    previewDiv.textContent = 'Preview: screenshots/' + (data.preview || '');
  } catch { previewDiv.textContent = ''; }
}

namingInput.addEventListener('input', updatePreview);

// save naming when config is saved, load naming from config
const origLoadPresets = loadPresets;
loadPresets = async function() {
  await origLoadPresets();
  try {
    const res = await fetch('/config');
    const data = await res.json();
    if (data.naming && data.naming.template) {
      namingInput.value = data.naming.template;
      updatePreview();
    }
  } catch {}
};

// also save naming when config is saved
const origSavePresets = savePresets;
savePresets = async function() {
  const template = namingInput.value.trim() || '{hostname}-{preset}';
  await fetch('/config', {
    method:'PUT', headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ presets, naming: { template } })
  });
};

loadPresets();
</script>
</body>
</html>`;

module.exports = { startServer };
