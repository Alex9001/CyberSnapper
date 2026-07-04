const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const sharp = require('sharp');
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
  const concurrency = opts.concurrency || 1;
  if (concurrency < 1) throw new Error('Concurrency must be at least 1');
  if (!fs.existsSync(OUT_DIR)) fs.mkdirSync(OUT_DIR, { recursive: true });
  const template = (naming && naming.template) || '{hostname}-{preset}';
  const initialDelay = opts.initialDelay || 1500;
  const scrollDelay = opts.scrollDelay || 1800;
  const finalDelay = opts.finalDelay || 1000;
  const blockPopups = opts.blockPopups || false;
  const formats = opts.formats || ['png'];
  const webpQuality = opts.webp?.quality || 80;
  const avifQuality = opts.avif?.quality || 50;
  const pdfOptions = opts.pdf || { format: 'A4', landscape: false, margin: '0' };
  const hideSelectors = opts.hideSelectors || [];
  const waitForSelector = opts.waitForSelector || '';

  const browser = await launchBrowser();
  const context = await browser.newContext();
  const page = await context.newPage();

  // Process URLs in chunks for concurrency control
  const chunks = [];
  for (let i = 0; i < urls.length; i += concurrency) {
    chunks.push(urls.slice(i, i + concurrency));
  }
  
  for (let chunkIndex = 0; chunkIndex < chunks.length; chunkIndex++) {
    const chunk = chunks[chunkIndex];
    await Promise.all(chunk.map(async (url, chunkUrlIndex) => {
      const i = chunkIndex * concurrency + chunkUrlIndex;
      const targetUrl = ensureScheme(url);

      onProgress?.({ type: 'url-start', index: i, total: urls.length, url: targetUrl });

      let urlObj;
      try {
        urlObj = new URL(targetUrl);
      } catch {
        onProgress?.({ type: 'url-error', index: i, url: targetUrl, message: 'Invalid URL' });
        return;
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
        
        // CSS Selector Hiding
        if (opts.hideSelectors && opts.hideSelectors.length > 0) {
          for (const selector of opts.hideSelectors) {
            await page.$eval(selector, el => el.style.display = 'none').catch(() => {});
          }
        }
      } catch (err) {
        onProgress?.({ type: 'viewport-error', index: i, url: targetUrl, viewport: vp.name, message: err.message });
        continue;
      }

      // Wait for selector or fall back to initialDelay
      if (opts.waitForSelector) {
        try {
          await page.waitForSelector(opts.waitForSelector, { timeout: 30000 });
        } catch (err) {
          onProgress?.({ type: 'warning', message: `Selector "${opts.waitForSelector}" not found. Falling back to initialDelay.` });
          await page.waitForTimeout(initialDelay);
        }
      } else {
        await page.waitForTimeout(initialDelay);
      }

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
      const basePath = path.join(fileDir, filename.replace('.png', ''));
      
      // Capture PNG buffer
      const buffer = await page.screenshot({ type: 'png', fullPage: true, animations: 'disabled' });
      
      // Save all requested formats
      for (const format of formats) {
        try {
          const outputPath = `${basePath}.${format}`;
          if (format === 'png') {
            fs.writeFileSync(outputPath, buffer);
          } else if (format === 'webp') {
            await sharp(buffer)
              .webp({ quality: webpQuality })
              .toFile(outputPath);
          } else if (format === 'avif') {
            await sharp(buffer)
              .avif({ quality: avifQuality })
              .toFile(outputPath);
          } else if (format === 'pdf') {
            await page.pdf({
              path: outputPath,
              format: pdfOptions.format,
              landscape: pdfOptions.landscape,
              margin: pdfOptions.margin
            });
          }
          onProgress?.({ type: 'viewport-done', index: i, url: targetUrl, viewport: vp.name, file: outputPath });
        } catch (err) {
          onProgress?.({ type: 'warning', message: `Failed to save ${format.toUpperCase()}: ${err.message}` });
        }
      }
    }

      onProgress?.({ type: 'url-done', url: targetUrl });
    }));
  }

  await browser.close();
  onProgress?.({ type: 'done' });
}

module.exports = { capture, OUT_DIR };
