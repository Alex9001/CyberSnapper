const { spawn } = require('child_process');

async function launchBrowser(onStatus) {
  try {
    const { chromium } = require('playwright');
    return await chromium.launch({ headless: true, args: ['--hide-scrollbars'] });
  } catch (err) {
    if (!err.message || !err.message.includes('Executable doesn\'t exist')) throw err;

    if (onStatus) onStatus({ type: 'status', message: 'Installing Chromium browser engine (may take a minute)...' });
    console.log('\n  Chromium not found. Installing browser engine...\n');
    try {
      await new Promise((resolve, reject) => {
        const child = spawn('npx', ['playwright', 'install', 'chromium'], {
          stdio: ['ignore', 'pipe', 'pipe'],
        });

        let buf = '';
        const onData = (chunk) => {
          process.stdout.write(chunk);
          buf += chunk.toString();
          const lines = buf.split('\n');
          buf = lines.pop() || '';
          for (const line of lines) {
            const match = line.match(/(\d+)%/);
            if (match) {
              const pct = parseInt(match[1], 10);
              if (onStatus) {
                onStatus({
                  type: 'status',
                  message: `Installing Chromium browser engine... ${pct}%`,
                  percent: pct,
                });
              }
            }
          }
        };

        child.stdout.on('data', onData);
        child.stderr.on('data', onData);
        child.on('close', (code) => {
          if (code === 0) resolve();
          else reject(new Error(`playwright install exited with code ${code}`));
        });
        child.on('error', reject);
      });
    } catch {
      if (onStatus) onStatus({ type: 'error', message: 'Failed to install Chromium. Run: npx playwright install chromium' });
      console.log('\n  Auto-install failed. Please run: npx playwright install chromium\n');
      process.exit(1);
    }
    if (onStatus) onStatus({ type: 'status', message: 'Chromium installed. Starting capture...' });
    const { chromium } = require('playwright');
    return await chromium.launch({ headless: true, args: ['--hide-scrollbars'] });
  }
}

module.exports = { launchBrowser };
