const http = require('http');
const fs = require('fs');
const path = require('path');
const { exec } = require('child_process');
const { capture } = require('./capture');

const SCREENSHOTS_PATH = path.join(process.cwd(), 'screenshots');

function openFolder() {
  if (!fs.existsSync(SCREENSHOTS_PATH)) fs.mkdirSync(SCREENSHOTS_PATH, { recursive: true });
  const cmd = process.platform === 'win32' ? `start "" "${SCREENSHOTS_PATH}"`
    : process.platform === 'darwin' ? `open "${SCREENSHOTS_PATH}"`
    : `xdg-open "${SCREENSHOTS_PATH}"`;
  exec(cmd, () => {});
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

    if (url.pathname === '/' || url.pathname === '/index.html') {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end(UI_HTML);
      return;
    }

    if (url.pathname === '/capture' && req.method === 'POST') {
      let body = '';
      req.on('data', chunk => body += chunk);
      req.on('end', async () => {
        let urls;
        try {
          urls = JSON.parse(body).urls;
          if (!Array.isArray(urls) || urls.length === 0) throw new Error();
        } catch {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: 'Invalid JSON body — expected { urls: [...] }' }));
          return;
        }

        res.writeHead(200, {
          'Content-Type': 'text/event-stream',
          'Cache-Control': 'no-cache',
          'Connection': 'keep-alive',
          'Access-Control-Allow-Origin': '*',
        });

        try {
          await capture(urls, (event) => {
            res.write(`data: ${JSON.stringify(event)}\n\n`);
          });
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
  .light { --bg: #f8f9fa; --surface: #fff; --border: #dee2e6; --text: #212529; --text-secondary: #6c757d; --primary: #4361ee; --primary-hover: #3a56d4; --success: #2ec4b6; --error: #e71d36; --accent: #ff9f1c; }
  .dark { --bg: #0f1117; --surface: #1a1b26; --border: #2a2b3d; --text: #e1e2e8; --text-secondary: #9ca0b0; --primary: #7c8aff; --primary-hover: #6a78e8; --success: #2ec4b6; --error: #ff6b6b; --accent: #ff9f1c; }
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: var(--bg); color: var(--text); min-height: 100vh; transition: background .3s, color .3s; }
  .container { max-width: 880px; margin: 0 auto; padding: 24px 20px; }
  header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 32px; padding-bottom: 16px; border-bottom: 1px solid var(--border); }
  header h1 { font-size: 24px; font-weight: 700; display: flex; align-items: center; gap: 10px; }
  header h1 span { color: var(--primary); }
  .theme-toggle { background: none; border: 1px solid var(--border); border-radius: 8px; padding: 6px 12px; cursor: pointer; color: var(--text); font-size: 14px; }
  .theme-toggle:hover { background: var(--border); }
  .card { background: var(--surface); border-radius: 12px; border: 1px solid var(--border); padding: 24px; margin-bottom: 20px; }
  .card h2 { font-size: 15px; font-weight: 600; margin-bottom: 12px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: .5px; }
  textarea { width: 100%; min-height: 140px; padding: 14px; border: 1px solid var(--border); border-radius: 8px; background: var(--bg); color: var(--text); font-family: 'SF Mono', 'Fira Code', monospace; font-size: 13px; line-height: 1.6; resize: vertical; outline: none; }
  textarea:focus { border-color: var(--primary); }
  textarea::placeholder { color: var(--text-secondary); }
  .actions { display: flex; gap: 10px; align-items: center; margin-top: 16px; flex-wrap: wrap; }
  .btn { padding: 10px 24px; border-radius: 8px; border: none; font-size: 14px; font-weight: 600; cursor: pointer; display: inline-flex; align-items: center; gap: 6px; }
  .btn-primary { background: var(--primary); color: #fff; }
  .btn-primary:hover { background: var(--primary-hover); }
  .btn-primary:disabled { opacity: .5; cursor: not-allowed; }
  .btn-outline { background: none; border: 1px solid var(--border); color: var(--text); }
  .btn-outline:hover { background: var(--border); }
  .hint { font-size: 13px; color: var(--text-secondary); margin-left: 4px; }
  #progress { display: none; }
  #progress.active { display: block; }
  .url-result { margin-bottom: 16px; padding-bottom: 16px; border-bottom: 1px solid var(--border); }
  .url-result:last-child { border-bottom: none; margin-bottom: 0; padding-bottom: 0; }
  .url-result .url-line { display: flex; align-items: center; gap: 8px; margin-bottom: 8px; font-size: 14px; font-weight: 500; }
  .url-result .url-line .status-icon { font-size: 16px; }
  .url-result .url-line .url-text { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .url-result .url-line .badge { font-size: 11px; padding: 2px 8px; border-radius: 4px; background: var(--border); margin-left: auto; white-space: nowrap; }
  .badge.done { background: var(--success); color: #fff; }
  .badge.error { background: var(--error); color: #fff; }
  .viewports { display: flex; gap: 8px; flex-wrap: wrap; margin-left: 24px; }
  .vp-item { padding: 4px 12px; border-radius: 6px; font-size: 12px; background: var(--bg); border: 1px solid var(--border); display: flex; align-items: center; gap: 4px; }
  .vp-item.done { border-color: var(--success); color: var(--success); }
  .vp-item.error { border-color: var(--error); color: var(--error); }
  .vp-item.active { border-color: var(--accent); color: var(--accent); animation: pulse 1s infinite; }
  @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: .5; } }
  .summary { margin-top: 16px; padding-top: 16px; border-top: 1px solid var(--border); display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 12px; }
  .summary .count { font-size: 14px; color: var(--text-secondary); }
  .summary .count strong { color: var(--text); }
  #snap-count { display: none; }
  #open-folder-btn { display: none; }
  #open-folder-btn.visible { display: inline-flex; }
  .gallery { display: grid; grid-template-columns: repeat(auto-fill, minmax(180px, 1fr)); gap: 12px; margin-top: 12px; }
  .gallery-item { border-radius: 8px; overflow: hidden; border: 1px solid var(--border); cursor: pointer; transition: transform .2s; position: relative; }
  .gallery-item:hover { transform: scale(1.03); }
  .gallery-item img { width: 100%; display: block; }
  .gallery-item .label { padding: 6px 8px; font-size: 11px; color: var(--text-secondary); background: var(--surface); }
</style>
</head>
<body>
<div class="container">
  <header>
    <h1><span>⌁</span> CyberSnapper</h1>
    <button class="theme-toggle" id="theme-toggle" title="Toggle theme">☀ / ☾</button>
  </header>
  <div class="card">
    <h2>URLs</h2>
    <textarea id="urls-input" placeholder="https://example.com&#10;https://google.com&#10;one-per-line" spellcheck="false"></textarea>
    <div class="actions">
      <button class="btn btn-primary" id="snap-btn">📸 Snap!</button>
      <span class="hint">or run with arguments for CLI mode</span>
    </div>
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
let totalSnaps = 0;

function setTheme(t) { document.documentElement.className = t; localStorage.setItem('theme', t); }
function getTheme() { return localStorage.getItem('theme') || (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'); }
setTheme(getTheme());
themeToggle.addEventListener('click', () => setTheme(document.documentElement.className === 'dark' ? 'light' : 'dark'));

snapBtn.addEventListener('click', startCapture);

async function startCapture() {
  const urls = urlInput.value.trim().split('\\n').map(l => l.trim()).filter(l => l.length > 0);
  if (urls.length === 0) return;
  snapBtn.disabled = true; snapBtn.textContent = '⏳ Capturing...';
  progressEl.classList.add('active'); resultsEl.innerHTML = ''; gallery.innerHTML = '';
  totalSnaps = 0; snapCount.style.display = 'none'; openFolderBtn.classList.remove('visible'); doneCount.textContent = '0';
  try {
    const res = await fetch('/capture', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ urls }) });
    const reader = res.body.getReader(); const dec = new TextDecoder(); let buf = '';
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += dec.decode(value, { stream: true });
      const lines = buf.split('\\n'); buf = lines.pop() || '';
      for (const l of lines) { if (l.startsWith('data: ')) { try { handleEvent(JSON.parse(l.slice(6))); } catch {} } }
    }
  } catch (err) { resultsEl.innerHTML += '<div class="url-result">Connection error: ' + err.message + '</div>'; }
  finally { snapBtn.disabled = false; snapBtn.textContent = '📸 Snap!'; }
}

function handleEvent(e) {
  switch (e.type) {
    case 'url-start': {
      const d = document.createElement('div'); d.className = 'url-result'; d.id = 'url-' + e.index;
      d.innerHTML = '<div class="url-line"><span class="status-icon">⏳</span><span class="url-text">' + esc(e.url) + '</span><span class="badge">' + (e.index+1) + '/' + e.total + '</span></div><div class="viewports" id="vps-' + e.index + '"></div>';
      resultsEl.appendChild(d); break;
    }
    case 'url-error': { const el = resultsEl.lastChild; if (el) { el.querySelector('.status-icon').textContent = '❌'; el.querySelector('.badge').textContent = 'invalid'; } break; }
    case 'url-done': { const el = resultsEl.lastChild; if (el) { el.querySelector('.status-icon').textContent = '✅'; el.querySelector('.badge').className = 'badge done'; el.querySelector('.badge').textContent = 'done'; } break; }
    case 'viewport-start': { const vps = document.getElementById('vps-' + (e.index || 0)); if (vps) { const item = document.createElement('span'); item.className = 'vp-item active'; item.id = 'vp-' + e.viewport; item.textContent = '▸ ' + e.viewport; vps.appendChild(item); } break; }
    case 'viewport-error': { const item = document.getElementById('vp-' + e.viewport); if (item) { item.className = 'vp-item error'; item.textContent = '✗ ' + e.viewport; } break; }
    case 'viewport-done': {
      const item = document.getElementById('vp-' + e.viewport); if (item) { item.className = 'vp-item done'; item.textContent = '✓ ' + e.viewport; }
      totalSnaps++; doneCount.textContent = totalSnaps; snapCount.style.display = 'block'; openFolderBtn.classList.add('visible');
      const fn = (e.file || '').split(/[/\\\\]/).pop();
      const t = document.createElement('div'); t.className = 'gallery-item';
      t.innerHTML = '<img src="/screenshots/' + fn + '" loading="lazy" alt="' + fn + '"><div class="label">' + fn.replace('.png','') + '</div>';
      t.addEventListener('click', () => window.open('/screenshots/' + fn, '_blank'));
      gallery.prepend(t); break;
    }
  }
}

openFolderBtn.addEventListener('click', () => fetch('/open-folder').catch(() => {}));
function esc(t) { const d = document.createElement('div'); d.textContent = t; return d.innerHTML; }
</script>
</body>
</html>`;

module.exports = { startServer };
