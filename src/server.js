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
        let urls, viewports, naming, initialDelay, scrollDelay, finalDelay, blockPopups;
        try {
          const parsed = JSON.parse(body);
          urls = parsed.urls;
          viewports = parsed.presets || config.getPresets();
          naming = parsed.naming || config.getNaming();
          initialDelay = parsed.initialDelay || config.getInitialDelay();
          scrollDelay = parsed.scrollDelay || config.getScrollDelay();
          finalDelay = parsed.finalDelay || config.getFinalDelay();
          blockPopups = parsed.blockPopups || config.getBlockPopups();
          if (!Array.isArray(urls) || urls.length === 0) throw new Error();
        } catch {
          json(res, 400, { error: 'Invalid body — expected { urls: [...], presets: [...], initialDelay?: number, scrollDelay?: number, finalDelay?: number, blockPopups?: boolean }' });
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
          }, naming, { initialDelay, scrollDelay, finalDelay, blockPopups });
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
    
    if (url.pathname === '/api/screenshot' && req.method === 'GET') {
      const token = url.searchParams.get('token');
      const configToken = config.getApiToken();
      
      if (token !== configToken) {
        json(res, 403, { error: 'Unauthorized: Invalid API token' });
        return;
      }
      
      const targetUrl = url.searchParams.get('url');
      const format = url.searchParams.get('format') || 'png';
      
      if (!targetUrl) {
        json(res, 400, { error: 'Missing URL parameter' });
        return;
      }
      
      res.writeHead(200, {
        'Content-Type': 'text/event-stream',
        'Cache-Control': 'no-cache',
        'Connection': 'keep-alive',
      });
      
      (async () => {
        try {
          const cfg = config.load();
          await capture([targetUrl], cfg.presets, (event) => {
            res.write(`data: ${JSON.stringify(event)}\n\n`);
          }, cfg.naming, {
            initialDelay: cfg.initialDelay * 1000,
            scrollDelay: cfg.scrollDelay * 1000,
            finalDelay: cfg.finalDelay * 1000,
            concurrency: 1, // API is single URL
            formats: [format],
            webp: cfg.webp,
            avif: cfg.avif,
            pdf: cfg.pdf,
            hideSelectors: cfg.hideSelectors,
            waitForSelector: cfg.waitForSelector,
            blockPopups: cfg.blockPopups
          });
        } catch (err) {
          res.write(`data: ${JSON.stringify({ type: 'error', message: err.message })}\n\n`);
        }
        res.end();
      })();
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
    --black: #bab2a6;
    --darker: #cec6ba;
    --dark: #b8afa2;
    --red: #aa4030;
    --gold: #8a6e30;
    --white: #1a1816;
    --gray: #a2988a;
    --surface: rgba(200,196,188,0.5);
    --border: rgba(170,64,48,0.12);
    --border-hover: rgba(170,64,48,0.22);
    --glow: rgba(170,64,48,0.03);
    --scan-color: rgba(0,0,0,0.006);
    --vignette: radial-gradient(ellipse at center, transparent 55%, rgba(180,172,160,0.2) 100%);
    --nv-color: rgba(26,24,22,0.45);
    --hint-color: rgba(26,24,22,0.3);
    --input-placeholder: rgba(26,24,22,0.25);
    --dim-color: rgba(26,24,22,0.4);
    --add-span: rgba(26,24,22,0.3);
    --label-color: rgba(26,24,22,0.45);
    --corner-color: rgba(138,110,48,0.35);
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
    -webkit-appearance:none;
    appearance:none;
    border-radius:0;
  }
  input[type="text"]:focus, input[type="number"]:focus { border-color:var(--border-hover); }
  input[type="number"]::-webkit-inner-spin-button,
  input[type="number"]::-webkit-outer-spin-button { -webkit-appearance:none; margin:0; display:none; }

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
    letter-spacing:3px;
    display:inline-flex;
    align-items:center;
    gap:10px;
  }
  .btn-primary::before {
    content:'';
    display:inline-block;
    width:6px; height:6px;
    background:currentColor;
    animation:pulse-dot 2s infinite;
  }
  .btn-primary::after {
    content:'';
    position:absolute; inset:0;
    background:repeating-linear-gradient(0deg, transparent, transparent 3px, rgba(255,255,255,0.06) 3px, rgba(255,255,255,0.06) 4px);
    opacity:0;
    transition:opacity .2s;
    pointer-events:none;
  }
  .btn-primary:hover { background:transparent; color:var(--red); box-shadow: 0 0 40px rgba(213,16,1,0.2), inset 0 0 40px rgba(213,16,1,0.05); }
  .btn-primary:hover::after { opacity:1; }
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
  .win-footer {
    display:flex; align-items:center; justify-content:center;
    padding:10px 20px;
    border-top:1px solid var(--border);
  }
  .byline {
    font-family:var(--font-display);
    font-size:8px;
    letter-spacing:3px;
    color:var(--hint-color);
    text-decoration:none;
    transition:color .2s;
  }
  .byline:hover { color:var(--red); }
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
    <button class="win-btn" id="help-btn" title="Help" onclick="document.getElementById('help-modal').style.display='flex'">?</button>
    <button class="win-btn" id="theme-toggle" title="Toggle theme" onclick="setTheme(theme === 'dark' ? 'light' : 'dark')">☀/☾</button>
    <button class="win-btn stop" id="stop-btn" title="Stop server" onclick="if(confirm('Stop the server and exit CyberSnapper?')){fetch('/shutdown').catch(()=>{});}">⏹ Stop</button>
  </div>
  
  <!-- Help Modal -->
  <div id="help-modal" style="display:none; position:fixed; inset:0; background:rgba(0,0,0,0.8); z-index:1000; justify-content:center; align-items:center;">
    <div style="background:var(--darker); border:1px solid var(--border); width:80%; max-width:600px; max-height:80vh; overflow:hidden; position:relative; box-shadow: 0 0 60px var(--glow), inset 0 0 60px var(--glow);">
      <div class="corner corner-tl"></div>
      <div class="corner corner-tr"></div>
      <div class="corner corner-bl"></div>
      <div class="corner corner-br"></div>
      <div style="position:absolute; top:0; left:0; right:0; background:var(--darker); border-bottom:1px solid var(--border); padding:14px 20px; z-index:1;">
        <div style="display:flex; justify-content:space-between; align-items:center;">
          <h2 style="font-family:var(--font-display); font-size:14px; font-weight:700; text-transform:uppercase; letter-spacing:2px; color:var(--gold);">📚 HELP</h2>
          <button id="close-help" style="background:none; border:1px solid var(--border); color:var(--white); font-size:16px; padding:2px 8px; cursor:pointer;" onclick="document.getElementById('help-modal').style.display='none'">✕</button>
        </div>
      </div>
      <div style="padding:16px; margin-top:50px; max-height:calc(80vh - 70px); overflow:auto;">
      <div style="padding:16px;">
        <div style="margin-bottom:16px;">
          <h3 style="font-family:var(--font-display); font-size:12px; font-weight:700; text-transform:uppercase; letter-spacing:1px; margin-bottom:8px;">🖥️ CLI Usage</h3>
          <pre style="background:var(--black); padding:8px; font-family:var(--font-mono); font-size:12px; border:1px solid var(--border);">node capture.js [urls.txt | url1 url2 ...]</pre>
          <p style="font-size:12px; margin-top:8px;">Settings are loaded from <code>config.json</code>.</p>
        </div>
        
        <div style="margin-bottom:16px;">
          <h3 style="font-family:var(--font-display); font-size:12px; font-weight:700; text-transform:uppercase; letter-spacing:1px; margin-bottom:8px;">🔌 REST API</h3>
          <pre style="background:var(--black); padding:8px; font-family:var(--font-mono); font-size:12px; border:1px solid var(--border);">GET /api/screenshot?url=...&token=...</pre>
          <p style="font-size:12px; margin-top:8px;">
            <strong>Parameters:</strong>
            <ul style="margin-top:4px;">
              <li><code>url</code>: Target website (required).</li>
              <li><code>format</code>: Output format (<code>png</code>, <code>webp</code>, <code>avif</code>, <code>pdf</code>).</li>
              <li><code>token</code>: API token from <code>config.json</code> (required).</li>
            </ul>
          </p>
        </div>
        
        <div style="margin-bottom:16px;">
          <h3 style="font-family:var(--font-display); font-size:12px; font-weight:700; text-transform:uppercase; letter-spacing:1px; margin-bottom:8px;">🔧 Settings</h3>
          <p style="font-size:12px;">
            <strong>Capture Settings:</strong>
            <ul style="margin-top:4px;">
              <li><code>Initial Delay</code>: Wait before scrolling (above-the-fold content).</li>
              <li><code>Scroll Delay</code>: Wait between scroll steps (lazy-loaded content).</li>
              <li><code>Concurrency</code>: Number of websites to capture in parallel.</li>
              <li><code>Block Popups</code>: Block popups/modals (checkbox).</li>
            </ul>
          </p>
          <p style="font-size:12px; margin-top:8px;">
            <strong>Output Formats:</strong>
            <ul style="margin-top:4px;">
              <li><code>PNG</code>: Lossless, high quality.</li>
              <li><code>WebP/AVIF</code>: Smaller files, adjustable quality.</li>
              <li><code>PDF</code>: For archiving/legal records (A4/Letter, portrait/landscape).</li>
            </ul>
          </p>
          <p style="font-size:12px; margin-top:8px;">
            <strong>Advanced:</strong>
            <ul style="margin-top:4px;">
              <li><code>Hide Selectors</code>: CSS selectors to hide before capturing (one per line).</li>
              <li><code>Wait For Selector</code>: Wait for this selector before capturing.</li>
            </ul>
          </p>
        </div>
        
        <div>
          <h3 style="font-family:var(--font-display); font-size:12px; font-weight:700; text-transform:uppercase; letter-spacing:1px; margin-bottom:8px;">📝 Variables</h3>
          <table style="width:100%; font-size:12px; border-collapse:collapse;">
            <tr style="border-bottom:1px solid var(--border);">
              <th style="text-align:left; padding:4px;">Variable</th>
              <th style="text-align:left; padding:4px;">Example</th>
            </tr>
            <tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;"><code>{hostname}</code></td><td style="padding:4px;"><code>example_com</code></td></tr>
            <tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;"><code>{preset}</code></td><td style="padding:4px;"><code>desktop</code></td></tr>
            <tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;"><code>{width}</code></td><td style="padding:4px;"><code>1920</code></td></tr>
            <tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;"><code>{height}</code></td><td style="padding:4px;"><code>1080</code></td></tr>
            <tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;"><code>{domain}</code></td><td style="padding:4px;"><code>example.com</code></td></tr>
            <tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;"><code>{date}</code></td><td style="padding:4px;"><code>2026-07-04</code></td></tr>
            <tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;"><code>{time}</code></td><td style="padding:4px;"><code>14-30-00</code></td></tr>
            <tr style="border-bottom:1px solid var(--border);"><td style="padding:4px;"><code>{index}</code></td><td style="padding:4px;"><code>01</code></td></tr>
          </table>
        </div>
      </div>
    </div>
  </div>
  </div>

  <div class="panel">
    <div class="panel-header"><h2>Target URLs</h2></div>
    <textarea id="urls-input" placeholder="example.com&#10;https://google.com&#10;one-per-line  (https:// is optional)" spellcheck="false"></textarea>
  </div>

  <div class="panel">
    <div class="panel-header">
      <h2>Viewport Presets</h2>
      <button class="btn btn-sm" id="reset-presets-btn">Reset</button>
    </div>
    <div id="preset-list" class="preset-grid"></div>
    <div class="add-row">
      <input type="text" class="name-input" id="new-name" placeholder="Name" maxlength="30" autocomplete="off">
      <input type="text" id="new-width" placeholder="Width" inputmode="numeric" pattern="[0-9]*">
      <span>×</span>
      <input type="text" id="new-height" placeholder="Height" inputmode="numeric" pattern="[0-9]*">
      <button class="btn btn-sm" id="add-preset-btn">+ Add</button>
    </div>
  </div>

  <div style="display:flex; gap:20px;">
    <!-- Left Column -->
    <div style="flex:1;">
      <div class="panel">
        <div class="panel-header"><h2>Capture Settings</h2></div>
        <div style="margin-bottom:12px;">
          <div style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
            <label for="initial-delay" style="font-size:12px; color:var(--label-color);" title="Delay before scrolling (seconds)">Initial Delay:</label>
            <input type="text" id="initial-delay" value="1.5" style="width:60px; text-align:center;">
            <span style="font-size:12px; color:var(--label-color);">seconds</span>
          </div>
          <div style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
            <label for="scroll-delay" style="font-size:12px; color:var(--label-color);" title="Delay between scroll steps (seconds)">Scroll Delay:</label>
            <input type="text" id="scroll-delay" value="1.8" style="width:60px; text-align:center;">
            <span style="font-size:12px; color:var(--label-color);">seconds</span>
          </div>
          <div style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
            <label for="concurrency" style="font-size:12px; color:var(--label-color);" title="Number of websites to capture in parallel">Concurrency:</label>
            <input type="text" id="concurrency" value="1" style="width:40px; text-align:center;">
          </div>
          <div style="display:flex; align-items:center; gap:8px;">
            <input type="checkbox" id="block-popups">
            <label for="block-popups" style="font-size:12px; color:var(--label-color);">Block popups/modals</label>
          </div>
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
    </div>
    
    <!-- Right Column -->
    <div style="flex:1;">
      <div class="panel">
        <div class="panel-header"><h2>Output Formats</h2></div>
        <div style="margin-bottom:12px;">
          <div style="display:flex; flex-wrap:wrap; gap:12px; margin-bottom:8px;">
            <label style="display:flex; align-items:center; gap:4px;">
              <input type="checkbox" id="format-png" checked>
              <span style="font-size:12px;">PNG</span>
            </label>
            <label style="display:flex; align-items:center; gap:4px;">
              <input type="checkbox" id="format-webp">
              <span style="font-size:12px;">WebP</span>
            </label>
            <label style="display:flex; align-items:center; gap:4px;">
              <input type="checkbox" id="format-avif">
              <span style="font-size:12px;">AVIF</span>
            </label>
            <label style="display:flex; align-items:center; gap:4px;">
              <input type="checkbox" id="format-pdf">
              <span style="font-size:12px;">PDF</span>
            </label>
          </div>
          <div style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
            <label for="webp-quality" style="font-size:12px; color:var(--label-color);">WebP Quality:</label>
            <input type="text" id="webp-quality" value="80" style="width:40px; text-align:center;">
            <span style="font-size:12px; color:var(--label-color);">(1-100)</span>
          </div>
          <div style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
            <label for="avif-quality" style="font-size:12px; color:var(--label-color);">AVIF Quality:</label>
            <input type="text" id="avif-quality" value="50" style="width:40px; text-align:center;">
            <span style="font-size:12px; color:var(--label-color);">(1-100)</span>
          </div>
          
          <details style="margin-top:12px;">
            <summary style="font-size:12px; color:var(--gold); cursor:pointer;">PDF Settings</summary>
            <div style="margin-top:8px; padding-left:12px;">
              <div style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
                <label for="pdf-format" style="font-size:12px; color:var(--label-color);">Format:</label>
                <select id="pdf-format" style="background:var(--black); border:1px solid var(--border); color:var(--white); font-size:12px; padding:4px;">
                  <option value="A4" selected>A4</option>
                  <option value="Letter">Letter</option>
                </select>
              </div>
              <div style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
                <label for="pdf-landscape" style="font-size:12px; color:var(--label-color);">Landscape:</label>
                <input type="checkbox" id="pdf-landscape">
              </div>
              <div style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
                <label for="pdf-margin" style="font-size:12px; color:var(--label-color);">Margin:</label>
                <input type="text" id="pdf-margin" value="0" style="width:40px; text-align:center;">
                <span style="font-size:12px; color:var(--label-color);">mm</span>
              </div>
            </div>
          </details>
        </div>
      </div>
      
      <div class="panel">
        <div class="panel-header"><h2>Advanced</h2></div>
        <div style="margin-bottom:12px;">
          <div style="margin-bottom:8px;">
            <label for="hide-selectors" style="font-size:12px; color:var(--label-color);" title="CSS selectors to hide before capturing (one per line)">Hide Selectors:</label>
            <textarea id="hide-selectors" style="width:100%; min-height:60px; margin-top:4px; font-family:var(--font-mono); font-size:12px;" placeholder="#intercom-widget\n.sticky-header\n.announcement-bar"></textarea>
          </div>
          <div style="margin-bottom:8px;">
            <label for="wait-for-selector" style="font-size:12px; color:var(--label-color);" title="Wait for this selector to appear before capturing (e.g., .dashboard-loaded-flag)">Wait For Selector:</label>
            <input type="text" id="wait-for-selector" style="width:100%; margin-top:4px;">
          </div>
        </div>
      </div>
    </div>
  </div>

  <div class="actions">
    <button class="btn btn-primary" id="snap-btn">SNAP</button>
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
  <div class="win-footer">
    <a class="byline" href="https://cyberbrand.net" target="_blank" rel="noopener">BY CYBER BRAND</a>
  </div>
</div>
<script>
const themeToggle = document.getElementById('theme-toggle');
const urlInput = document.getElementById('urls-input');
const snapBtn = document.getElementById('snap-btn');
const helpBtn = document.getElementById('help-btn');
const helpModal = document.getElementById('help-modal');
const closeHelp = document.getElementById('close-help');
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
const initialDelayInput = document.getElementById('initial-delay');
const scrollDelayInput = document.getElementById('scroll-delay');
const concurrencyInput = document.getElementById('concurrency');
const blockPopups = document.getElementById('block-popups');
const formatPng = document.getElementById('format-png');
const formatWebp = document.getElementById('format-webp');
const formatAvif = document.getElementById('format-avif');
const formatPdf = document.getElementById('format-pdf');
const webpQualityInput = document.getElementById('webp-quality');
const avifQualityInput = document.getElementById('avif-quality');
const pdfFormatSelect = document.getElementById('pdf-format');
const pdfLandscape = document.getElementById('pdf-landscape');
const pdfMarginInput = document.getElementById('pdf-margin');
const hideSelectorsTextarea = document.getElementById('hide-selectors');
const waitForSelectorInput = document.getElementById('wait-for-selector');
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
  console.log('loadPresets called'); // Debug
  try {
    const res = await fetch('/config');
    const data = await res.json();
    presets = data.presets || [];
    initialDelayInput.value = data.initialDelay || 1.5;
    scrollDelayInput.value = data.scrollDelay || 1.8;
    concurrencyInput.value = data.concurrency || 1;
    blockPopups.checked = data.blockPopups || false;
    
    // Formats
    formatPng.checked = data.formats.includes('png');
    formatWebp.checked = data.formats.includes('webp');
    formatAvif.checked = data.formats.includes('avif');
    formatPdf.checked = data.formats.includes('pdf');
    
    // Quality
    webpQualityInput.value = data.webp?.quality || 80;
    avifQualityInput.value = data.avif?.quality || 50;
    
    // PDF
    pdfFormatSelect.value = data.pdf?.format || 'A4';
    pdfLandscape.checked = data.pdf?.landscape || false;
    pdfMarginInput.value = data.pdf?.margin || '0';
    
    // Advanced
    hideSelectorsTextarea.value = data.hideSelectors?.join('\n') || '';
    waitForSelectorInput.value = data.waitForSelector || '';
    
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
  const w = parseInt(newWidth.value, 10);
  const h = parseInt(newHeight.value, 10);
  if (!name || isNaN(w) || w <= 0 || isNaN(h) || h <= 0) return;
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

[newName, newWidth, newHeight].forEach(el => {
  el.addEventListener('keydown', e => {
    if (e.key === 'Enter') addBtn.click();
    // Prevent non-numeric input for width/height
    if (el.id !== 'new-name' && !/[0-9]/.test(e.key) && e.key !== 'Backspace' && e.key !== 'Delete' && e.key !== 'ArrowLeft' && e.key !== 'ArrowRight' && e.key !== 'Tab') {
      e.preventDefault();
    }
  });
});

// Helper to validate number inputs
function validateNumberInput(input, min, max, defaultValue) {
  const value = parseFloat(input.value);
  if (isNaN(value) || value < min || value > max) {
    input.value = defaultValue;
    return defaultValue;
  }
  return value;
}

initialDelayInput.addEventListener('change', () => {
  validateNumberInput(initialDelayInput, 0.1, 60, 1.5);
  savePresets();
});

scrollDelayInput.addEventListener('change', () => {
  validateNumberInput(scrollDelayInput, 0.1, 60, 1.8);
  savePresets();
});

concurrencyInput.addEventListener('change', () => {
  const value = parseInt(concurrencyInput.value, 10);
  if (isNaN(value) || value < 1 || value > 10) {
    concurrencyInput.value = 1;
  }
  savePresets();
});

[formatPng, formatWebp, formatAvif, formatPdf].forEach(checkbox => {
  checkbox.addEventListener('change', () => {
    // Ensure at least one format is selected
    if (!formatPng.checked && !formatWebp.checked && !formatAvif.checked && !formatPdf.checked) {
      formatPng.checked = true;
    }
    savePresets();
  });
});

[webpQualityInput, avifQualityInput].forEach(input => {
  input.addEventListener('change', () => {
    const value = parseInt(input.value, 10);
    if (isNaN(value) || value < 1 || value > 100) {
      input.value = input.id === 'webp-quality' ? 80 : 50;
    }
    savePresets();
  });
});

[pdfFormatSelect, pdfLandscape, pdfMarginInput].forEach(input => {
  input.addEventListener('change', savePresets);
});

hideSelectorsTextarea.addEventListener('change', savePresets);
waitForSelectorInput.addEventListener('change', savePresets);

blockPopups.addEventListener('change', savePresets);

snapBtn.addEventListener('click', startCapture);

async function startCapture() {
  const urls = urlInput.value.trim().split('\\n').map(l => l.trim()).filter(l => l.length > 0);
  if (urls.length === 0) return;
  const selected = presets.filter((_, i) => {
    const cb = presetList.querySelector('input[data-i="' + i + '"]');
    return cb ? cb.checked : true;
  });
  if (selected.length === 0) return;

  snapBtn.disabled = true; snapBtn.textContent = '⌁ CAPTURING';
  progressEl.classList.add('active'); resultsEl.innerHTML = ''; gallery.innerHTML = '';
  totalSnaps = 0; urlIndex = 0;
  snapCount.style.display = 'none'; openFolderBtn.classList.remove('visible'); doneCount.textContent = '0';
  try {
    const res = await fetch('/capture', {
      method: 'POST', 
      headers: { 'Content-Type': 'application/json' },
      const formats = [];
    if (formatPng.checked) formats.push('png');
    if (formatWebp.checked) formats.push('webp');
    if (formatAvif.checked) formats.push('avif');
    if (formatPdf.checked) formats.push('pdf');
    
    const hideSelectors = hideSelectorsTextarea.value
      .split('\n')
      .map(s => s.trim())
      .filter(s => s.length > 0);
    
    body: JSON.stringify({
      urls,
      presets: selected,
      initialDelay: validateNumberInput(initialDelayInput, 0.1, 60, 1.5),
      scrollDelay: validateNumberInput(scrollDelayInput, 0.1, 60, 1.8),
      concurrency: parseInt(concurrencyInput.value, 10) || 1,
      formats,
      webp: { quality: parseInt(webpQualityInput.value, 10) || 80 },
      avif: { quality: parseInt(avifQualityInput.value, 10) || 50 },
      pdf: {
        format: pdfFormatSelect.value,
        landscape: pdfLandscape.checked,
        margin: pdfMarginInput.value
      },
      hideSelectors,
      waitForSelector: waitForSelectorInput.value.trim(),
      blockPopups: blockPopups.checked
    })
    });
    const reader = res.body.getReader(); const dec = new TextDecoder(); let buf = '';
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += dec.decode(value, { stream: true });
      const lines = buf.split('\\n'); buf = lines.pop() || '';
      for (const l of lines) { if (l.startsWith('data: ')) { try { handleEvent(JSON.parse(l.slice(6))); } catch {} } }
    }
  } catch (err) { resultsEl.innerHTML += '<div class="url-result">Connection error: ' + esc(err.message) + '</div>'; }
  finally { snapBtn.disabled = false; snapBtn.textContent = 'SNAP'; }
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
    if (data.initialDelay != null) {
      initialDelayInput.value = data.initialDelay;
    }
    if (data.scrollDelay != null) {
      scrollDelayInput.value = data.scrollDelay;
    }
    if (data.concurrency != null) {
      concurrencyInput.value = data.concurrency;
    }
    if (data.blockPopups != null) {
      blockPopups.checked = data.blockPopups;
    }
    if (data.formats) {
      formatPng.checked = data.formats.includes('png');
      formatWebp.checked = data.formats.includes('webp');
      formatAvif.checked = data.formats.includes('avif');
      formatPdf.checked = data.formats.includes('pdf');
    }
    if (data.webp) {
      webpQualityInput.value = data.webp.quality;
    }
    if (data.avif) {
      avifQualityInput.value = data.avif.quality;
    }
    if (data.pdf) {
      pdfFormatSelect.value = data.pdf.format;
      pdfLandscape.checked = data.pdf.landscape;
      pdfMarginInput.value = data.pdf.margin;
    }
    if (data.hideSelectors) {
      hideSelectorsTextarea.value = data.hideSelectors.join('\n');
    }
    if (data.waitForSelector != null) {
      waitForSelectorInput.value = data.waitForSelector;
    }
  } catch {}
};

const origSavePresets = savePresets;
savePresets = async function() {
  const template = namingInput.value.trim() || '{hostname}-{preset}';
  const formats = [];
  if (formatPng.checked) formats.push('png');
  if (formatWebp.checked) formats.push('webp');
  if (formatAvif.checked) formats.push('avif');
  if (formatPdf.checked) formats.push('pdf');
  
  const hideSelectors = hideSelectorsTextarea.value
    .split('\n')
    .map(s => s.trim())
    .filter(s => s.length > 0);
  
  await fetch('/config', {
    method:'PUT', headers:{'Content-Type':'application/json'},
    body:JSON.stringify({
      presets,
      naming: { template },
      initialDelay: validateNumberInput(initialDelayInput, 0.1, 60, 1.5),
      scrollDelay: validateNumberInput(scrollDelayInput, 0.1, 60, 1.8),
      concurrency: parseInt(concurrencyInput.value, 10) || 1,
      formats,
      webp: { quality: parseInt(webpQualityInput.value, 10) || 80 },
      avif: { quality: parseInt(avifQualityInput.value, 10) || 50 },
      pdf: {
        format: pdfFormatSelect.value,
        landscape: pdfLandscape.checked,
        margin: pdfMarginInput.value
      },
      hideSelectors,
      waitForSelector: waitForSelectorInput.value.trim(),
      blockPopups: blockPopups.checked
    })
  });
};

loadPresets().then(() => {
  // Help Modal
  closeHelp.addEventListener('click', () => {
    helpModal.style.display = 'none';
  });

  helpBtn.addEventListener('click', () => {
    console.log('Help button clicked'); // Debug
    helpModal.style.display = 'flex';
  });

  // Close modal when clicking outside
  helpModal.addEventListener('click', (e) => {
    if (e.target === helpModal) {
      helpModal.style.display = 'none';
    }
  });
});
</script>
</body>
</html>`;

module.exports = { startServer };
