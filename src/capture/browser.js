const { execSync } = require('child_process');

async function launchBrowser() {
  try {
    const { chromium } = require('playwright');
    return await chromium.launch({ headless: true, args: ['--hide-scrollbars'] });
  } catch (err) {
    if (!err.message || !err.message.includes('Executable doesn\'t exist')) throw err;

    console.log('\n  Chromium not found. Installing browser engine...\n');
    try {
      execSync('npx playwright install chromium', { stdio: 'inherit' });
    } catch {
      console.log('\n  Auto-install failed. Please run: npx playwright install chromium\n');
      process.exit(1);
    }
    const { chromium } = require('playwright');
    return await chromium.launch({ headless: true, args: ['--hide-scrollbars'] });
  }
}

module.exports = { launchBrowser };
