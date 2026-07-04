const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const { generateFilename } = require('./naming');

const OUT_DIR = 'screenshots';

function ensureScheme(url) {
  if (!url.startsWith('http://') && !url.startsWith('https://')) return 'https://' + url;
  return url;
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

async function capture(urls, viewports, onProgress, naming, opts = {}) {
  if (!fs.existsSync(OUT_DIR)) fs.mkdirSync(OUT_DIR, { recursive: true });
  const template = (naming && naming.template) || '{hostname}-{preset}';
  const initialDelay = opts.initialDelay || 2000;
  const scrollDelay = opts.scrollDelay || 1000;
  const finalDelay = opts.finalDelay || 1000;
  const blockPopups = opts.blockPopups || false;

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

    for (const vp of viewports) {
      onProgress?.({ type: 'viewport-start', index: i, url: targetUrl, viewport: vp.name });

      await page.setViewportSize({ width: vp.width, height: vp.height });

      try {
        await page.goto(targetUrl, { waitUntil: 'domcontentloaded', timeout: 60000 });
        await page.addStyleTag({ content: `
          ::-webkit-scrollbar { display: none !important; }
          * { scrollbar-width: none !important; }
        `});
        
        // Popup blocking
        if (blockPopups) {
          await page.route('**/*', route => {
            const url = route.request().url();
            const blocked = [
              'doubleclick.net', 'googlesyndication.com', 'googleadservices.com',
              'facebook.com/tr', 'connect.facebook.net', 'intercom.io',
              'cookiebot.com', 'onetrust.com', 'hotjar.com',
              'optanon.com', 'trustarc.com', 'cookielaw.org',
              'hubspot.com', 'pardot.com', 'marketo.com',
              'adservice.google.com', 'pagead2.googlesyndication.com',
              'cdn.onesignal.com', 'pushcrew.com', 'pushwoosh.com',
              'crazyegg.com', 'mouseflow.com', 'fullstory.com',
              'tawk.to', 'livechatinc.com', 'olark.com',
              'popup', 'affiliate'
            ];
            if (blocked.some(d => url.includes(d))) {
              route.abort();
            } else {
              route.continue();
            }
          });
          
          await page.addStyleTag({ content: `
            [class*="modal"], [class*="popup"], [class*="overlay"],
            [class*="cookie"], [id*="cookie"], [class*="banner"],
            [class*="newsletter"], [class*="subscribe"],
            [class*="gdpr"], [id*="gdpr"],
            [class*="consent"], [id*="consent"],
            [class*="notification"], [class*="announcement"],
            [class*="interstitial"], [class*="layer"],
            [class*="lightbox"], [class*="fancybox"],
            iframe[src*="intercom"], iframe[src*="tawk"],
            #intercom, .intercom, #hubspot-messages,
            .fc-consent-root, .cookie-consent, .cc-window,
            .mailmunch-forms, .pum-overlay, .mfp-wrap,
            [aria-modal="true"], [role="dialog"]:not([role="dialog"] form) {
              display: none !important;
              visibility: hidden !important;
              opacity: 0 !important;
              pointer-events: none !important;
              height: 0 !important;
              width: 0 !important;
              overflow: hidden !important;
              clip: rect(0,0,0,0) !important;
              position: absolute !important;
            }
          `});
        }
      } catch (err) {
        onProgress?.({ type: 'viewport-error', index: i, url: targetUrl, viewport: vp.name, message: err.message });
        continue;
      }

      // Initial delay before scrolling
      await page.waitForTimeout(initialDelay);

      await page.evaluate(async (scrollDelay, finalDelay) => {
        const wait = ms => new Promise(r => setTimeout(r, ms));
        const step = window.innerHeight;
        let pos = 0;
        while (pos < document.body.scrollHeight) {
          window.scrollBy(0, step);
          pos += step;
          await wait(scrollDelay);
        }
        window.scrollTo(0, 0);
        await wait(finalDelay);
      }, scrollDelay, finalDelay);

      await page.waitForLoadState('networkidle', { timeout: 15000 }).catch(() => {});

      const { filename, subdir } = generateFilename(template, targetUrl, vp, i);
      const fileDir = subdir ? path.join(OUT_DIR, subdir) : OUT_DIR;
      if (!fs.existsSync(fileDir)) fs.mkdirSync(fileDir, { recursive: true });
      const filePath = path.join(fileDir, filename);
      await page.screenshot({ path: filePath, fullPage: true, animations: 'disabled' });

      onProgress?.({ type: 'viewport-done', index: i, url: targetUrl, viewport: vp.name, file: filePath });
    }

    onProgress?.({ type: 'url-done', url: targetUrl });
  }

  await browser.close();
  onProgress?.({ type: 'done' });
}

module.exports = { capture, OUT_DIR };
