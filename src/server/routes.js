const fs = require('fs');
const path = require('path');
const { capture } = require('../capture');
const { generateFilename } = require('../naming');
const config = require('../config');

const IDLE_TIMEOUT_MS = 15 * 60 * 1000;
const WARNING_LEAD_MS = 60 * 1000;

const SCREENSHOTS_PATH = path.join(process.cwd(), 'screenshots');

const MIME = {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'application/javascript',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.webp': 'image/webp',
  '.avif': 'image/avif',
  '.pdf': 'application/pdf',
};

function json(res, code, data) {
  res.writeHead(code, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify(data));
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => resolve(body));
    req.on('error', reject);
  });
}

function streamHeaders(res) {
  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    'Connection': 'keep-alive',
    'Access-Control-Allow-Origin': '*',
  });
}

function sendSSE(res, event) {
  res.write(`data: ${JSON.stringify(event)}\n\n`);
}

function openFolder() {
  if (!fs.existsSync(SCREENSHOTS_PATH)) fs.mkdirSync(SCREENSHOTS_PATH, { recursive: true });
  const cmd = process.platform === 'win32'
    ? `start "" "${SCREENSHOTS_PATH}"`
    : process.platform === 'darwin'
      ? `open "${SCREENSHOTS_PATH}"`
      : `xdg-open "${SCREENSHOTS_PATH}"`;
  require('child_process').exec(cmd, () => {});
}

function shutdown(server, res) {
  json(res, 200, { ok: true });
  server.close(() => process.exit(0));
}

async function handleConfigGet(res) {
  json(res, 200, config.load());
}

async function handleConfigPut(req, res) {
  try {
    const data = JSON.parse(await readBody(req));
    if (!Array.isArray(data.presets)) throw new Error('presets must be an array');
    const cfg = config.save(data);
    json(res, 200, { ok: true, config: cfg });
  } catch (err) {
    json(res, 400, { error: 'Invalid config: ' + err.message });
  }
}

async function handleCapture(req, res) {
  let parsed;
  try {
    parsed = JSON.parse(await readBody(req));
    if (!Array.isArray(parsed.urls) || parsed.urls.length === 0) throw new Error('urls required');
  } catch (err) {
    json(res, 400, { error: 'Invalid body — expected { urls: [...] }' });
    return;
  }

  const cfg = config.load();
  const viewports = parsed.presets && parsed.presets.length ? parsed.presets : cfg.presets;
  const naming = parsed.naming || cfg.naming;

  streamHeaders(res);

  try {
    await capture(parsed.urls, viewports, ev => sendSSE(res, ev), naming, {
      initialDelay: parsed.initialDelay ?? cfg.initialDelay,
      scrollDelay: parsed.scrollDelay ?? cfg.scrollDelay,
      finalDelay: parsed.finalDelay ?? cfg.finalDelay,
      concurrency: parsed.concurrency ?? cfg.concurrency,
      formats: parsed.formats || cfg.formats,
      blockPopups: !!parsed.blockPopups,
      blocklist: parsed.blocklist || cfg.blocklist,
      hideSelectors: parsed.hideSelectors || cfg.hideSelectors,
      waitForSelector: parsed.waitForSelector || cfg.waitForSelector,
      webp: parsed.webp || cfg.webp,
      avif: parsed.avif || cfg.avif,
      pdf: parsed.pdf || cfg.pdf,
    });
  } catch (err) {
    sendSSE(res, { type: 'error', message: err.message });
  }
  res.end();
}

async function handlePreview(req, res) {
  try {
    const { template, url: sampleUrl, preset } = JSON.parse(await readBody(req));
    const safeUrl = sampleUrl && !sampleUrl.startsWith('http://') && !sampleUrl.startsWith('https://')
      ? 'https://' + sampleUrl : sampleUrl;
    const samplePreset = preset || { name: 'Desktop', width: 1920, height: 1080 };
    const result = generateFilename(
      template || '{hostname}-{preset}',
      safeUrl || 'https://example.com',
      samplePreset,
      0
    );
    json(res, 200, { preview: result.subdir ? result.subdir + '/' + result.filename : result.filename });
  } catch {
    json(res, 400, { error: 'Invalid preview request' });
  }
}

function handleScreenshotFile(url, res) {
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
}

async function handleApiScreenshot(url, res) {
  const token = url.searchParams.get('token');
  const cfg = config.load();

  if (token !== cfg.apiToken) {
    json(res, 403, { error: 'Unauthorized: Invalid API token' });
    return;
  }

  const targetUrl = url.searchParams.get('url');
  if (!targetUrl) {
    json(res, 400, { error: 'Missing URL parameter' });
    return;
  }

  const format = url.searchParams.get('format') || 'png';

  streamHeaders(res);

  (async () => {
    try {
      await capture([targetUrl], cfg.presets, ev => sendSSE(res, ev), cfg.naming, {
        initialDelay: cfg.initialDelay,
        scrollDelay: cfg.scrollDelay,
        finalDelay: cfg.finalDelay,
        concurrency: 1,
        formats: [format],
        blockPopups: cfg.blockPopups,
        blocklist: cfg.blocklist,
        hideSelectors: cfg.hideSelectors,
        waitForSelector: cfg.waitForSelector,
        webp: cfg.webp,
        avif: cfg.avif,
        pdf: cfg.pdf,
      });
    } catch (err) {
      sendSSE(res, { type: 'error', message: err.message });
    }
    res.end();
  })();
}

function createRouter(server, uiAssets) {
  return async (req, res) => {
    const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
    const method = req.method;

    try {
      if (url.pathname === '/shutdown') return shutdown(server, res);

      if ((url.pathname === '/' || url.pathname === '/index.html') && method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end(uiAssets.html);
        return;
      }

      if (url.pathname === '/styles.css' && method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'text/css' });
        res.end(uiAssets.css);
        return;
      }

      if (url.pathname === '/app.js' && method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/javascript' });
        res.end(uiAssets.js);
        return;
      }

      if (url.pathname === '/config' && method === 'GET') return await handleConfigGet(res);
      if (url.pathname === '/config' && method === 'PUT') return await handleConfigPut(req, res);
      if (url.pathname === '/capture' && method === 'POST') return await handleCapture(req, res);
      if (url.pathname === '/preview' && method === 'POST') return await handlePreview(req, res);
      if (url.pathname === '/open-folder' && method === 'GET') {
        openFolder();
        json(res, 200, { ok: true });
        return;
      }
      if (url.pathname === '/keepalive' && method === 'GET') {
        const wd = server.watchdog;
        const idleMs = Date.now() - wd.state.lastActivity;
        const remaining = Math.max(0, IDLE_TIMEOUT_MS - idleMs);
        json(res, 200, {
          ok: true,
          idleMs,
          remainingMs: remaining,
          totalMs: IDLE_TIMEOUT_MS,
          warningMs: WARNING_LEAD_MS,
          warning: remaining <= WARNING_LEAD_MS,
        });
        return;
      }
      if (url.pathname === '/keepalive' && method === 'POST') {
        // Explicit "keep alive" click — bumps activity via the request listener.
        json(res, 200, { ok: true });
        return;
      }
      if (url.pathname === '/api/screenshot' && method === 'GET') return await handleApiScreenshot(url, res);
      if (url.pathname.startsWith('/screenshots/') && method === 'GET') return handleScreenshotFile(url, res);

      res.writeHead(404);
      res.end('Not found');
    } catch (err) {
      json(res, 500, { error: err.message });
    }
  };
}

module.exports = { createRouter, MIME };
