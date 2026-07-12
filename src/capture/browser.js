const { spawn } = require('child_process');

/* resolve the playwright CLI entry point (works in both Node.js and pkg binary) */
const PLAYWRIGHT_CLI = (() => {
  try {
    return require.resolve('playwright/cli.js');
  } catch {
    return null;
  }
})();

/* Determine the command + args for running playwright's CLI.
   In pkg binaries process.execPath can load scripts from its own snapshot,
   so we prefer it over npx (which may not be installed). */
function playwrightInstallCmd(action) {
  if (PLAYWRIGHT_CLI) {
    return { cmd: process.execPath, args: [PLAYWRIGHT_CLI, action, 'chromium'] };
  }
  /* fallback: hope npx is on PATH */
  return { cmd: 'npx', args: ['playwright', action, 'chromium'] };
}

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
      const { cmd, args } = playwrightInstallCmd('install');
      try {
        await runInstall(cmd, args, 'Installing Chromium', onStatus);
      } catch (installErr) {
        const hint = 'Failed to install Chromium. Run: npx playwright install chromium';
        if (onStatus) onStatus({ type: 'error', message: hint });
        console.log(`\n  ${hint}\n`);
        throw new Error(hint);
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
      const { cmd, args } = playwrightInstallCmd('install-deps');
      try {
        await runInstall(cmd, args, 'Installing system deps', onStatus);
      } catch (installErr) {
        const hint = 'Failed to install system deps. Run: npx playwright install-deps chromium';
        if (onStatus) onStatus({ type: 'error', message: hint });
        console.log(`\n  ${hint}\n`);
        throw new Error(hint);
      }
      if (onStatus) onStatus({ type: 'status', message: 'System dependencies installed. Starting capture...' });
      return launchBrowser(onStatus);
    }

    /* unknown error — rethrow */
    throw err;
  }
}

module.exports = { launchBrowser, runInstall };
