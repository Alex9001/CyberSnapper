const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const VIEWPORTS = [
  { name: 'desktop', width: 1920, height: 1080 },
  { name: 'tablet',  width: 768,  height: 1024 },
  { name: 'mobile',  width: 375,  height: 812 },
];

const OUT_DIR = 'screenshots';

function ensureScheme(url) {
  if (!url.startsWith('http://') && !url.startsWith('https://')) return 'https://' + url;
  return url;
}

function safeFilename(url) {
  const hostname = url.hostname.replace(/[^a-z0-9]/gi, '_').toLowerCase();
  let pathname = url.pathname.replace(/[^a-z0-9]/gi, '_').toLowerCase();
  if (pathname === '_' || pathname === '') pathname = '';
  return hostname + pathname;
}

async function launchBrowser() {
  try {
    const { chromium } = require('playwright');
    return await chromium.launch({ headless: true, args: ['--hide-scrollbars'] });
  } catch (err) {
    if (err.message && err.message.includes('Executable doesn\'t exist')) {
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
    throw err;
  }
}

async function capture(urls, onProgress) {
  if (!fs.existsSync(OUT_DIR)) fs.mkdirSync(OUT_DIR, { recursive: true });

  const browser = await launchBrowser();
  const context = await browser.newContext();
  const page = await context.newPage();

  for (let i = 0; i < urls.length; i++) {
    const targetUrl = ensureScheme(urls[i]);

    onProgress?.({ type: 'url-start', index: i, total: urls.length, url: targetUrl });

    let urlObj;
    try {
      urlObj = new URL(targetUrl);
    } catch {
      onProgress?.({ type: 'url-error', url: targetUrl, message: 'Invalid URL' });
      continue;
    }

    const prefix = safeFilename(urlObj);

    for (const vp of VIEWPORTS) {
      onProgress?.({ type: 'viewport-start', url: targetUrl, viewport: vp.name });

      await page.setViewportSize({ width: vp.width, height: vp.height });

      try {
        await page.goto(targetUrl, { waitUntil: 'domcontentloaded', timeout: 60000 });
        await page.addStyleTag({ content: `
          ::-webkit-scrollbar { display: none !important; }
          * { scrollbar-width: none !important; }
        `});
      } catch (err) {
        onProgress?.({ type: 'viewport-error', url: targetUrl, viewport: vp.name, message: err.message });
        continue;
      }

      await page.evaluate(async () => {
        const wait = ms => new Promise(r => setTimeout(r, ms));
        const step = window.innerHeight;
        let pos = 0;
        while (pos < document.body.scrollHeight) {
          window.scrollBy(0, step);
          pos += step;
          await wait(1000);
        }
        window.scrollTo(0, 0);
        await wait(1000);
      });

      await page.waitForLoadState('networkidle', { timeout: 15000 }).catch(() => {});

      const filePath = path.join(OUT_DIR, `${prefix}-${vp.name}.png`);
      await page.screenshot({ path: filePath, fullPage: true, animations: 'disabled' });

      onProgress?.({ type: 'viewport-done', url: targetUrl, viewport: vp.name, file: filePath });
    }

    onProgress?.({ type: 'url-done', url: targetUrl });
  }

  await browser.close();
  onProgress?.({ type: 'done' });
}

module.exports = { capture, VIEWPORTS, OUT_DIR };
