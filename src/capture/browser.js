const { spawn } = require('child_process');

/* ---------- run a piped child process with progress reporting ---------- */
function runInstall(cmd, args, statusPrefix, onStatus) {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { stdio: ['ignore', 'pipe', 'pipe'] });

    let buf = '';
    const onData = (chunk) => {
      process.stdout.write(chunk);
      buf += chunk.toString();
      const lines = buf.split('\n');
      buf = lines.pop() || '';
      for (const line of lines) {
        const trimmed = line.trim();
        if (!trimmed) continue;
        const match = line.match(/(\d+)%/);
        if (match && onStatus) {
          onStatus({
            type: 'status',
            message: `${statusPrefix}... ${match[1]}%`,
            percent: parseInt(match[1], 10),
          });
        }
      }
    };

    child.stdout.on('data', onData);
    child.stderr.on('data', onData);
    child.on('close', (code) => {
      if (code === 0) resolve();
      else reject(new Error(`${args.join(' ')} exited with code ${code}`));
    });
    child.on('error', reject);
  });
}

/* ---------- try to launch, handling install-on-demand ---------- */
async function launchBrowser(onStatus) {
  /* attempt initial launch */
  try {
    const { chromium } = require('playwright');
    return await chromium.launch({ headless: true, args: ['--hide-scrollbars'] });
  } catch (err) {
    const msg = (err && err.message) || '';

    /* --- case 1: Chromium not downloaded --- */
    if (msg.includes('Executable doesn\'t exist')) {
      if (onStatus) onStatus({ type: 'status', message: 'Installing Chromium browser engine (may take a minute)...' });
      console.log('\n  Chromium not found. Installing browser engine...\n');
      try {
        await runInstall('npx', ['playwright', 'install', 'chromium'], 'Installing Chromium', onStatus);
      } catch {
        if (onStatus) onStatus({ type: 'error', message: 'Failed to install Chromium. Run: npx playwright install chromium' });
        console.log('\n  Auto-install failed. Please run: npx playwright install chromium\n');
        process.exit(1);
      }
      if (onStatus) onStatus({ type: 'status', message: 'Chromium installed. Starting capture...' });
      return launchBrowser(onStatus);
    }

    /* --- case 2: system dependencies missing (Linux) --- */
    if (msg.includes('missing dependencies') ||
        msg.includes('cannot open shared object') ||
        msg.includes('error while loading shared libraries') ||
        msg.includes('Host system is missing')) {
      if (onStatus) onStatus({ type: 'status', message: 'Installing Chromium system dependencies (may take a minute)...' });
      console.log('\n  System dependencies missing. Installing via playwright install-deps...\n');
      try {
        await runInstall('npx', ['playwright', 'install-deps', 'chromium'], 'Installing system deps', onStatus);
      } catch {
        if (onStatus) onStatus({ type: 'error', message: 'Failed to install system deps. Run: npx playwright install-deps chromium' });
        console.log('\n  Auto-install failed. Please run: npx playwright install-deps chromium\n');
        process.exit(1);
      }
      if (onStatus) onStatus({ type: 'status', message: 'System dependencies installed. Starting capture...' });
      return launchBrowser(onStatus);
    }

    /* unknown error — rethrow */
    throw err;
  }
}

module.exports = { launchBrowser };
