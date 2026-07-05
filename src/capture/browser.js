const { exec } = require('child_process');

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
        const child = exec('npx playwright install chromium', (err) => {
          if (err) reject(err);
          else resolve();
        });
        child.stdout.pipe(process.stdout);
        child.stderr.pipe(process.stderr);
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
