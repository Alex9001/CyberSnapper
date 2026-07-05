const http = require('http');
const fs = require('fs');
const path = require('path');
const { createRouter } = require('./routes');
const { acquirePidFile, removePidFile } = require('./pid');

const IDLE_TIMEOUT_MS = 15 * 60 * 1000;
const WARNING_LEAD_MS = 60 * 1000;

function uiDir() {
  if (process.pkg) {
    const bundled = path.resolve(__dirname, '..', '..', 'ui');
    try {
      if (fs.existsSync(path.join(bundled, 'index.html'))) return bundled;
    } catch {}
    return path.join(path.dirname(process.execPath), 'ui');
  }
  return path.resolve(__dirname, '..', '..', 'ui');
}

function loadUiAsset(name) {
  const filePath = path.join(uiDir(), name);
  try {
    return fs.readFileSync(filePath, 'utf-8');
  } catch {
    console.warn(`Warning: ui/${name} not found at ${filePath}`);
    return '';
  }
}

function loadUiAssets() {
  const html = loadUiAsset('index.html');
  const css = loadUiAsset('styles.css');
  const js = loadUiAsset('app.js');

  const withAssets = html
    .replace('{{STYLES_INLINE}}', css)
    .replace('{{APP_INLINE}}', js);

  return { html: withAssets, css, js };
}

/* ---------- Inactivity watchdog (web-mode only) ---------- */

function startWatchdog(server, { onWarning, onShutdown }) {
  const state = { lastActivity: Date.now(), warned: false };
  let warningTimer = null;

  const bump = () => {
    state.lastActivity = Date.now();
    if (state.warned) {
      state.warned = false;
      if (warningTimer) clearTimeout(warningTimer);
      warningTimer = null;
      onWarning?.(false);
    }
  };

  const tick = () => {
    const idle = Date.now() - state.lastActivity;
    if (idle >= IDLE_TIMEOUT_MS) {
      clearInterval(interval);
      if (warningTimer) clearTimeout(warningTimer);
      onShutdown?.();
      return;
    }
    if (!state.warned && idle >= IDLE_TIMEOUT_MS - WARNING_LEAD_MS) {
      state.warned = true;
      onWarning?.(true);
      warningTimer = setTimeout(() => {
        state.warned = false;
        warningTimer = null;
      }, WARNING_LEAD_MS);
    }
  };

  const interval = setInterval(tick, 5000);

  return {
    state,
    bump,
    stop() {
      clearInterval(interval);
      if (warningTimer) clearTimeout(warningTimer);
    },
  };
}

function wrapResForActivity(res, bump) {
  const origWrite = res.write.bind(res);
  const origEnd = res.end.bind(res);
  res.write = (chunk, ...rest) => {
    bump();
    return origWrite(chunk, ...rest);
  };
  res.end = (...args) => {
    bump();
    return origEnd(...args);
  };
  return res;
}

/* ---------- startServer ---------- */

async function startServer(port = 0) {
  const claim = acquirePidFile();
  if (!claim.ok) {
    console.error(`  CyberSnapper is already running (PID ${claim.ownerPid}).`);
    console.error(`  Use: node capture.js --stop   (or close the browser tab.)\n`);
    process.exit(1);
  }
  const pidFile = claim.pidFile;
  const uiAssets = loadUiAssets();

  const server = http.createServer();
  const watchdog = startWatchdog(server, {
    onWarning: (active) => server.emit('idle-warning', active),
    onShutdown: () => {
      console.log('\n  Auto-stop: idle for 15 minutes. Shutting down...');
      server.close(() => process.exit(0));
    },
  });
  server.watchdog = watchdog;

  const router = createRouter(server, uiAssets);
  server.on('request', (req, res) => {
    // GET /keepalive is a passive poll used by the UI toast — it must NOT
    // reset the inactivity timer, otherwise polling forever prevents the
    // auto-stop it's reporting. All other requests count as user activity.
    const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
    const isPassivePoll = req.method === 'GET' && url.pathname === '/keepalive';
    if (!isPassivePoll) {
      watchdog.bump();
      wrapResForActivity(res, watchdog.bump);
    }
    router(req, res).catch(err => {
      if (!res.headersSent) {
        res.writeHead(500, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: err.message }));
      }
    });
  });

  const cleanup = (cb) => {
    watchdog.stop();
    removePidFile();
    cb?.();
  };
  const shutdownAndExit = () => {
    server.close(() => process.exit(0));
    cleanup();
  };
  process.on('SIGINT', shutdownAndExit);
  process.on('SIGTERM', shutdownAndExit);
  process.on('beforeExit', () => cleanup());

  return new Promise((resolve, reject) => {
    server.on('error', (err) => {
      cleanup();
      reject(err);
    });
    server.listen(port, () => {
      console.log(`  (PID ${process.pid}, pid file: ${pidFile})`);
      resolve(server);
    });
  });
}

module.exports = { startServer, IDLE_TIMEOUT_MS, WARNING_LEAD_MS };
