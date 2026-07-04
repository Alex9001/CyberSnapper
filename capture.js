const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const VIEWPORTS = [
  { name: 'desktop', width: 1920, height: 1080 },
  { name: 'tablet',  width: 768,  height: 1024 },
  { name: 'mobile',  width: 375,  height: 812 },
];

const OUT_DIR = 'screenshots';
const DEFAULT_URLS_FILE = path.join('urls', 'urls.txt');

function readUrlsFromArgs(args) {
  if (args.length === 0) {
    if (fs.existsSync(DEFAULT_URLS_FILE)) {
      console.log(`No arguments provided — using default URL file: ${DEFAULT_URLS_FILE}\n`);
      return readUrlsFromFile(DEFAULT_URLS_FILE);
    }
    console.error('Usage: node capture.js <url1 url2 ...>  or  node capture.js <urls.txt>');
    process.exit(1);
  }

  if (args.length === 1 && fs.existsSync(args[0])) {
    return readUrlsFromFile(args[0]);
  }

  return args;
}

function readUrlsFromFile(filePath) {
  const content = fs.readFileSync(filePath, 'utf-8');
  return content.split('\n').map(l => l.trim()).filter(l => l.length > 0);
}

function ensureUrlScheme(url) {
  if (!url.startsWith('http://') && !url.startsWith('https://')) {
    return 'https://' + url;
  }
  return url;
}

function safeFilename(url) {
  const hostname = url.hostname.replace(/[^a-z0-9]/gi, '_').toLowerCase();
  let pathname = url.pathname.replace(/[^a-z0-9]/gi, '_').toLowerCase();
  if (pathname === '_' || pathname === '') pathname = '';
  return hostname + pathname;
}

(async () => {
  const urls = readUrlsFromArgs(process.argv.slice(2));

  if (!fs.existsSync(OUT_DIR)) fs.mkdirSync(OUT_DIR, { recursive: true });

  console.log('Launching browser...\n');
  const browser = await chromium.launch({ headless: true, args: ['--hide-scrollbars'] });
  const context = await browser.newContext();
  const page = await context.newPage();

  for (let i = 0; i < urls.length; i++) {
    const targetUrl = ensureUrlScheme(urls[i]);
    const label = `[${i + 1}/${urls.length}]`;

    console.log(`${'='.repeat(60)}`);
    console.log(`${label} ${targetUrl}`);
    console.log(`${'='.repeat(60)}\n`);

    let urlObj;
    try {
      urlObj = new URL(targetUrl);
    } catch {
      console.error(`  Invalid URL, skipping.\n`);
      continue;
    }

    const prefix = safeFilename(urlObj);

    for (const vp of VIEWPORTS) {
      process.stdout.write(`  ${vp.name} (${vp.width}x${vp.height}) ... `);

      await page.setViewportSize({ width: vp.width, height: vp.height });

      try {
        await page.goto(targetUrl, { waitUntil: 'domcontentloaded', timeout: 60000 });
        await page.addStyleTag({ content: `
          ::-webkit-scrollbar { display: none !important; }
          * { scrollbar-width: none !important; }
        `});
      } catch (err) {
        console.error(`FAILED (${err.message})`);
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

      const filePath = `${OUT_DIR}/${prefix}-${vp.name}.png`;
      await page.screenshot({ path: filePath, fullPage: true, animations: 'disabled' });

      console.log(`saved`);
    }
    console.log();
  }

  await browser.close();
  console.log('Done! All screenshots saved to the "screenshots" folder.\n');
})();
