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
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700;900&family=Share+Tech+Mono&display=swap" rel="stylesheet">
<style>
  :root {
    --black: #111522;
    --darker: #1A2031;
    --dark: #222940;
    --red: #D51001;
    --gold: #FFC734;
    --white: #FFEBD6;
    --gray: #303A58;
    --cyan: #00FFF7;
    --surface: rgba(16,38,62,0.55);
    --border: rgba(213,16,1,0.20);
    --border-hover: rgba(213,16,1,0.35);
    --glow: rgba(213,16,1,0.08);
    --font-display: 'Orbitron', sans-serif;
    --font-mono: 'Share Tech Mono', monospace;
    --scan-color: rgba(213,16,1,0.015);
    --vignette: radial-gradient(ellipse at center, transparent 55%, rgba(0,0,0,0.7) 100%);
    --nv-color: rgba(255,235,214,0.6);
    --hint-color: rgba(255,235,214,0.35);
    --input-placeholder: rgba(255,235,214,0.25);
    --dim-color: rgba(255,235,214,0.4);
    --add-span: rgba(255,235,214,0.3);
    --label-color: rgba(255,235,214,0.5);
    --corner-color: rgba(255,199,52,0.35);
  }
  .light {
    --black: #f0f0f0;
    --darker: #e2e2e2;
    --dark: #d4d4d4;
    --red: #b00000;
    --gold: #b8860b;
    --white: #2d2d2d;
    --gray: #b0b0b0;
    --surface: rgba(200,200,200,0.55);
    --border: rgba(176,0,0,0.12);
    --border-hover: rgba(176,0,0,0.25);
    --glow: rgba(176,0,0,0.04);
    --scan-color: rgba(0,0,0,0.02);
    --vignette: radial-gradient(ellipse at center, transparent 55%, rgba(200,200,200,0.4) 100%);
    --nv-color: rgba(45,45,45,0.5);
    --hint-color: rgba(45,45,45,0.35);
    --input-placeholder: rgba(45,45,45,0.3);
    --dim-color: rgba(45,45,45,0.45);
    --add-span: rgba(45,45,45,0.35);
    --label-color: rgba(45,45,45,0.5);
    --corner-color: rgba(184,134,11,0.4);
  }
  *,*::before,*::after { box-sizing:border-box; margin:0; padding:0; }
  ::-webkit-scrollbar { width:6px; height:6px; }
  ::-webkit-scrollbar-track { background:var(--darker); }
  ::-webkit-scrollbar-thumb { background:var(--gray); border:1px solid var(--red); }
  body {
    background:var(--black);
    color:var(--white);
    font-family:var(--font-mono);
    min-height:100vh;
    display:flex;
    align-items:center;
    justify-content:center;
    padding:20px;
    position:relative;
    overflow-x:hidden;
  }
  body::before {
    content:'';
    position:fixed; inset:0;
    background: repeating-linear-gradient(0deg, transparent, transparent 3px, var(--scan-color) 3px, var(--scan-color) 4px);
    pointer-events:none;
    z-index:9999;
  }
  body::after {
    content:'';
    position:fixed; inset:0;
    background: var(--vignette);
    pointer-events:none;
    z-index:9998;
  }
  .app-window {
    width:100%;
    max-width:820px;
    background:var(--darker);
    border:1px solid var(--border);
    box-shadow: 0 0 60px var(--glow), inset 0 0 60px var(--glow);
    position:relative;
    z-index:1;
  }
  .app-window::before {
    content:'';
    position:absolute; top:-1px; left:0; right:0;
    height:2px;
    background:linear-gradient(90deg, var(--red), var(--gold), transparent);
  }
  .app-window::after {
    content:''; position:absolute; bottom:-1px; left:0; right:0;
    height:1px;
    background:linear-gradient(90deg, transparent, var(--border), transparent);
  }

  /* Corner brackets */
  .corner { position:absolute; width:14px; height:14px; border-color:var(--corner-color); border-style:solid; }
  .corner-tl { top:6px; left:6px; border-width:1px 0 0 1px; }
  .corner-tr { top:6px; right:6px; border-width:1px 1px 0 0; }
  .corner-bl { bottom:6px; left:6px; border-width:0 0 1px 1px; }
  .corner-br { bottom:6px; right:6px; border-width:0 1px 1px 0; }

  /* Header */
  .win-header {
    display:flex; align-items:center; justify-content:space-between;
    padding:14px 20px 12px;
    border-bottom:1px solid var(--border);
    position:relative;
  }
  .win-title {
    font-family:var(--font-display);
    font-size:16px;
    font-weight:700;
    text-transform:uppercase;
    letter-spacing:3px;
    display:flex;
    align-items:center;
    gap:10px;
    color:var(--white);
  }
  .win-title .dot {
    display:inline-block;
    width:6px; height:6px;
    background:var(--red);
    animation:pulse-dot 2s infinite;
  }
  @keyframes pulse-dot { 0%,100% { opacity:1; } 50% { opacity:.4; } }
  .win-controls { display:flex; gap:6px; }
  .win-btn {
    background:none; border:1px solid var(--border);
    color:var(--white); cursor:pointer;
    font-family:var(--font-mono); font-size:11px;
    padding:4px 10px; text-transform:uppercase;
    letter-spacing:1px; transition:all .2s;
    position:relative; overflow:hidden;
  }
  .win-btn:hover { border-color:var(--red); color:var(--red); }
  .win-btn.stop:hover { border-color:var(--red); color:var(--red); }

  /* Panels */
  .panel {
    border-bottom:1px solid var(--border);
    padding:16px 20px;
    position:relative;
  }
  .panel:last-of-type { border-bottom:none; }

  .panel-header {
    display:flex; align-items:center; justify-content:space-between;
    margin-bottom:12px;
    position:relative;
    padding-left:14px;
  }
  .panel-header::before {
    content:'';
    position:absolute; left:0; top:50%; transform:translateY(-50%);
    width:4px; height:4px;
    background:var(--red);
    box-shadow: 0 0 6px var(--red);
  }
  .panel-header h2 {
    font-family:var(--font-display);
    font-size:11px; font-weight:400;
    text-transform:uppercase;
    letter-spacing:2px;
    color:var(--gold);
  }

  textarea {
    width:100%; min-height:100px;
    padding:12px 14px;
    background:var(--black);
    border:1px solid var(--border);
    color:var(--white);
    font-family:var(--font-mono);
    font-size:13px;
    line-height:1.6;
    resize:vertical;
    outline:none;
    transition:border-color .2s;
  }
  textarea:focus { border-color:var(--border-hover); }
  textarea::placeholder { color:var(--input-placeholder); }

  input[type="text"], input[type="number"] {
    padding:6px 10px;
    background:var(--black);
    border:1px solid var(--border);
    color:var(--white);
    font-family:var(--font-mono);
    font-size:12px;
    outline:none;
    transition:border-color .2s;
  }
  input[type="text"]:focus, input[type="number"]:focus { border-color:var(--border-hover); }

  .btn {
    font-family:var(--font-mono);
    font-size:11px;
    text-transform:uppercase;
    letter-spacing:1px;
    cursor:pointer;
    padding:6px 16px;
    border:1px solid var(--border);
    background:transparent;
    color:var(--white);
    transition:all .2s;
    position:relative;
    overflow:hidden;
  }
  .btn:hover { border-color:var(--red); color:var(--red); }
  .btn-primary {
    background:var(--red);
    border-color:var(--red);
    color:var(--white);
    padding:10px 32px;
    font-size:13px;
    font-weight:700;
  }
  .btn-primary:hover { background:transparent; color:var(--red); box-shadow: 0 0 30px rgba(213,16,1,0.15); }
  .btn-primary:disabled { opacity:0.4; cursor:not-allowed; border-color:var(--border); color:var(--white); background:var(--red); }
  .btn-sm { padding:4px 12px; font-size:10px; }

  /* Actions row */
  .actions {
    display:flex; align-items:center; gap:12px;
    padding:16px 20px;
    border-top:1px solid var(--border);
  }
  .actions .hint {
    font-size:11px;
    color:var(--hint-color);
    letter-spacing:1px;
  }

  /* Preset chips */
  .preset-grid { display:flex; flex-wrap:wrap; gap:6px; margin-bottom:10px; }
  .preset-chip {
    display:flex; align-items:center; gap:6px;
    padding:4px 8px 4px 4px;
    border:1px solid var(--border);
    background:rgba(17,21,34,0.6);
    font-size:12px; cursor:pointer; user-select:none;
    transition:border-color .2s, background .2s;
  }
  .preset-chip:hover { border-color:var(--border-hover); }
  .preset-chip.selected { border-color:var(--red); background:rgba(213,16,1,0.06); }
  .preset-chip input { accent-color:var(--red); margin:0; }
  .preset-chip .dim { color:var(--dim-color); font-size:10px; }
  .preset-chip .del { cursor:pointer; opacity:0.3; font-size:13px; line-height:1; padding:0 2px; transition:opacity .2s; }
  .preset-chip .del:hover { opacity:1; color:var(--red); }

  .add-row { display:flex; gap:6px; align-items:center; flex-wrap:wrap; }
  .add-row input { width:80px; }
  .add-row input.name-input { width:100px; }
  .add-row span { color:var(--add-span); font-size:11px; }

  /* Naming */
  .nv {
    display:inline-block;
    padding:1px 6px;
    border:1px solid var(--border);
    cursor:pointer;
    font-size:10px;
    margin:2px 0;
    transition:all .2s;
    color:var(--nv-color);
  }
  .nv:hover { border-color:var(--gold); color:var(--gold); }
  .naming-bar { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin-top:8px; }
  .naming-bar input { flex:1; min-width:180px; }
  .naming-presets { display:flex; gap:6px; flex-wrap:wrap; margin-top:8px; }
  #naming-preview { margin-top:8px; font-size:11px; color:var(--gold); min-height:16px; }

  /* Progress */
  #progress { display:none; }
  #progress.active { display:block; }

  .url-result {
    margin-bottom:12px;
    padding-bottom:12px;
    border-bottom:1px solid var(--border);
  }
  .url-result:last-child { border-bottom:none; margin-bottom:0; padding-bottom:0; }
  .url-line {
    display:flex; align-items:center; gap:8px;
    margin-bottom:6px;
    font-size:12px;
  }
  .url-line .status-icon { font-size:13px; }
  .url-line .url-text { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; flex:1; }
  .url-line .badge {
    font-size:10px; padding:1px 8px;
    border:1px solid var(--border);
    margin-left:auto; white-space:nowrap;
    background:var(--black);
  }
  .badge.done { border-color:var(--red); color:var(--red); }
  .badge.error { border-color:var(--red); color:var(--red); }
  .viewports { display:flex; gap:4px; flex-wrap:wrap; margin-left:20px; }
  .vp-item {
    padding:2px 8px;
    font-size:11px;
    border:1px solid var(--border);
    background:var(--black);
    display:flex; align-items:center; gap:4px;
  }
  .vp-item.done { border-color:var(--red); color:var(--red); }
  .vp-item.error { border-color:var(--red); color:var(--red); opacity:0.6; }
  .vp-item.active { border-color:var(--gold); color:var(--gold); animation:vp-pulse 1s infinite; }
  @keyframes vp-pulse { 0%,100% { opacity:1; } 50% { opacity:.4; } }

  .summary {
    margin-top:12px; padding-top:12px;
    border-top:1px solid var(--border);
    display:flex; justify-content:space-between; align-items:center;
    flex-wrap:wrap; gap:12px;
  }
  .summary .count { font-size:11px; color:var(--label-color); }
  .summary .count strong { color:var(--white); }
  #snap-count { display:none; }
  #open-folder-btn { display:inline-flex; }
  #open-folder-btn { display:none; }
  #open-folder-btn.visible { display:inline-flex; }

  .gallery {
    display:grid;
    grid-template-columns:repeat(auto-fill,minmax(150px,1fr));
    gap:8px; margin-top:12px;
  }
  .gallery-item {
    border:1px solid var(--border);
    cursor:pointer;
    transition:border-color .2s, transform .2s;
    position:relative;
    overflow:hidden;
    background:var(--black);
  }
  .gallery-item:hover { border-color:var(--red); transform:scale(1.02); }
  .gallery-item img { width:100%; display:block; filter:grayscale(30%) contrast(1.05); transition:filter .3s; }
  .gallery-item:hover img { filter:none; }
  .gallery-item .label {
    padding:4px 8px;
    font-size:10px;
    color:var(--label-color);
    background:var(--darker);
    border-top:1px solid var(--border);
  }
  .gallery-item:hover .label { color:var(--white); }
</style>
</head>
<body>
<div class="app-window">
  <div class="corner corner-tl"></div>
  <div class="corner corner-tr"></div>
  <div class="corner corner-bl"></div>
  <div class="corner corner-br"></div>

  <div class="win-header">
    <div class="win-title"><span class="dot"></span> ⌁ CyberSnapper</div>
    <div class="win-controls">
      <button class="win-btn" id="theme-toggle" title="Toggle theme">☀/☾</button>
      <button class="win-btn stop" id="stop-btn" title="Stop server">⏹ Stop</button>
    </div>
  </div>

  <div class="panel">
    <div class="panel-header"><h2>Target URLs</h2></div>
    <textarea id="urls-input" placeholder="https://example.com&#10;https://google.com&#10;one-per-line" spellcheck="false"></textarea>
  </div>

  <div class="panel">
    <div class="panel-header">
      <h2>Viewport Presets</h2>
      <button class="btn btn-sm" id="reset-presets-btn">Reset</button>
    </div>
    <div id="preset-list" class="preset-grid"></div>
    <div class="add-row">
      <input class="name-input" id="new-name" placeholder="Name" maxlength="30">
      <input type="number" id="new-width" placeholder="Width" min="1">
      <span>×</span>
      <input type="number" id="new-height" placeholder="Height" min="1">
      <button class="btn btn-sm" id="add-preset-btn">+ Add</button>
    </div>
  </div>

  <div class="panel">
    <div class="panel-header"><h2>Output Naming</h2></div>
    <div style="margin-bottom:8px;font-size:10px;color:rgba(255,235,214,0.35);letter-spacing:1px">Variables —
      <span class="nv" data-v="{hostname}">{hostname}</span>
      <span class="nv" data-v="{preset}">{preset}</span>
      <span class="nv" data-v="{width}">{width}</span>
      <span class="nv" data-v="{height}">{height}</span>
      <span class="nv" data-v="{domain}">{domain}</span>
      <span class="nv" data-v="{date}">{date}</span>
      <span class="nv" data-v="{time}">{time}</span>
      <span class="nv" data-v="{index}">{index}</span>
    </div>
    <div class="naming-bar">
      <input type="text" id="naming-template" value="{hostname}-{preset}">
      <button class="btn btn-sm" id="preview-btn">Preview</button>
    </div>
    <div class="naming-presets">
      <button class="btn btn-sm" data-template="{hostname}-{preset}">Default</button>
      <button class="btn btn-sm" data-template="{hostname}-{width}x{height}">By Size</button>
      <button class="btn btn-sm" data-template="{date}/{hostname}-{preset}">By Date</button>
      <button class="btn btn-sm" data-template="{index}-{hostname}-{preset}">Indexed</button>
      <button class="btn btn-sm" data-template="{domain}/{preset}/{hostname}">By Domain</button>
    </div>
    <div id="naming-preview"></div>
  </div>

  <div class="actions">
    <button class="btn btn-primary" id="snap-btn">📸 Snap!</button>
    <span class="hint">CLI: node capture.js urls.txt</span>
  </div>

  <div class="panel" id="progress">
    <div class="panel-header"><h2>Capture Log</h2></div>
    <div id="results"></div>
    <div class="summary">
      <div class="count" id="snap-count">Captured <strong id="done-count">0</strong> screenshots</div>
      <div><button class="btn btn-sm" id="open-folder-btn">📁 Open</button></div>
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

let theme = 'dark';
function setTheme(t) {
  theme = t; localStorage.setItem('cybersnapper-theme', t);
  document.documentElement.classList.toggle('light', t === 'light');
}
function getTheme() { return localStorage.getItem('cybersnapper-theme') || 'dark'; }
setTheme(getTheme());
themeToggle.addEventListener('click', () => setTheme(theme === 'dark' ? 'light' : 'dark'));

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
    case 'url-error': { const el = resultsEl.lastChild; if (el) { el.querySelector('.status-icon').textContent = '✕'; el.querySelector('.badge').textContent = 'invalid'; } break; }
    case 'url-done': { const el = document.getElementById('url-' + urlIndex); if (el) { el.querySelector('.status-icon').textContent = '✓'; el.querySelector('.badge').className = 'badge done'; el.querySelector('.badge').textContent = 'done'; } break; }
    case 'viewport-start': { const idx = e.index != null ? e.index : urlIndex; const vps = document.getElementById('vps-' + idx); if (vps) { const item = document.createElement('span'); item.className = 'vp-item active'; item.id = 'vp-' + idx + '-' + slug(e.viewport); item.textContent = '▸ ' + e.viewport; vps.appendChild(item); } break; }
    case 'viewport-error': { const idx = e.index != null ? e.index : urlIndex; const item = document.getElementById('vp-' + idx + '-' + slug(e.viewport)); if (item) { item.className = 'vp-item error'; item.textContent = '✕ ' + e.viewport; } break; }
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
    previewDiv.textContent = '▸ screenshots/' + (data.preview || '');
  } catch { previewDiv.textContent = ''; }
}

namingInput.addEventListener('input', updatePreview);

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
