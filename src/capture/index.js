const fs = require('fs');
const path = require('path');
const { generateFilename } = require('../naming');
const { launchBrowser } = require('./browser');
const { HIDE_POPUPS_CSS, attachPopupBlocker } = require('./popupBlocker');
const { SCROLLBAR_HIDE_CSS, scrollThrough, waitForContent } = require('./scrolling');
const { normalizeFormats, writeFormat } = require('./formats');
const sharp = require('sharp');

const OUT_DIR = 'screenshots';

const DEFAULTS = {
  concurrency: 1,
  initialDelaySec: 1.5,
  scrollDelaySec: 1.8,
  finalDelaySec: 1.0,
  webpQuality: 80,
  avifQuality: 50,
  pdf: { format: 'A4', landscape: false, margin: '0' },
  formats: ['png'],
  blockPopups: false,
  stripWhitespace: true,
  hideSelectors: [],
  waitForSelector: '',
  blocklist: [],
};

function ensureScheme(url) {
  if (!url.startsWith('http://') && !url.startsWith('https://')) return 'https://' + url;
  return url;
}

function resolveOpts(opts) {
  return {
    ...DEFAULTS,
    ...opts,
    pdf: { ...DEFAULTS.pdf, ...(opts.pdf || {}) },
    formats: normalizeFormats(opts.formats || DEFAULTS.formats),
    hideSelectors: opts.hideSelectors || [],
    blocklist: opts.blocklist || [],
    webpQuality: opts.webp?.quality || DEFAULTS.webpQuality,
    avifQuality: opts.avif?.quality || DEFAULTS.avifQuality,
    stripWhitespace: opts.stripWhitespace == null ? DEFAULTS.stripWhitespace : !!opts.stripWhitespace,
  };
}

async function captureOneUrl(page, url, viewports, index, total, naming, opts, onProgress) {
  const targetUrl = ensureScheme(url);

  try {
    new URL(targetUrl);
  } catch {
    onProgress?.({ type: 'url-error', index, url: targetUrl, message: 'Invalid URL' });
    return;
  }

  onProgress?.({ type: 'url-start', index, total, url: targetUrl });
  const template = (naming && naming.template) || '{hostname}-{preset}';

  for (const vp of viewports) {
    await captureViewport(page, targetUrl, vp, index, total, template, opts, onProgress);
  }

  onProgress?.({ type: 'url-done', index, url: targetUrl });
}

async function captureViewport(page, targetUrl, vp, index, total, template, opts, onProgress) {
  onProgress?.({ type: 'viewport-start', index, total, url: targetUrl, viewport: vp.name });

  await page.setViewportSize({ width: vp.width, height: vp.height });

  try {
    await page.goto(targetUrl, { waitUntil: 'domcontentloaded', timeout: 60000 });
    await page.addStyleTag({ content: SCROLLBAR_HIDE_CSS });
  } catch (err) {
    onProgress?.({ type: 'viewport-error', index, url: targetUrl, viewport: vp.name, message: err.message });
    return;
  }

  if (opts.blockPopups) {
    await page.addStyleTag({ content: HIDE_POPUPS_CSS });
  }

  for (const selector of opts.hideSelectors) {
    await page.$eval(selector, el => { el.style.display = 'none'; }).catch(() => {});
  }

  const found = await waitForContent(page, {
    waitForSelector: opts.waitForSelector,
    initialDelay: opts.initialDelayMs,
  });
  if (!found && opts.waitForSelector) {
    onProgress?.({
      type: 'warning',
      message: `Selector "${opts.waitForSelector}" not found. Falling back to initialDelay.`,
    });
  }

  await scrollThrough(page, opts.scrollDelayMs, opts.finalDelayMs);
  await page.waitForLoadState('networkidle', { timeout: 15000 }).catch(() => {});

  /* Reset scroll to true origin and settle a frame for rendering */
  await page.evaluate(() => new Promise(r => { requestAnimationFrame(() => { document.documentElement.scrollTop = 0; document.body.scrollTop = 0; requestAnimationFrame(r); }); }));

  const { filename, subdir } = generateFilename(template, targetUrl, vp, index);
  const fileDir = subdir ? path.join(OUT_DIR, subdir) : OUT_DIR;
  if (!fs.existsSync(fileDir)) fs.mkdirSync(fileDir, { recursive: true });
  const baseName = filename.replace(/\.png$/, '');
  const basePath = path.join(fileDir, baseName);

  const rawPng = await page.screenshot({ type: 'png', fullPage: true, animations: 'disabled' });
  const pngBuffer = opts.stripWhitespace ? await stripTopWhitespace(rawPng) : rawPng;

  for (const format of opts.formats) {
    try {
      const outputPath = `${basePath}.${format}`;
      await writeFormat(page, pngBuffer, format, outputPath, opts);
      onProgress?.({ type: 'viewport-done', index, url: targetUrl, viewport: vp.name, file: outputPath });
    } catch (err) {
      onProgress?.({ type: 'warning', message: `Failed to save ${format.toUpperCase()}: ${err.message}` });
    }
  }
}

function toMs(value, defaultSec) {
  if (value == null) return Math.round(defaultSec * 1000);
  return value >= 100 ? Math.round(value) : Math.round(value * 1000);
}

async function stripTopWhitespace(buf) {
  const meta = await sharp(buf).metadata();
  if (!meta.width || !meta.height) return buf;
  const { width, height } = meta;
  const raw = await sharp(buf).ensureAlpha().raw().toBuffer();
  const ch = 4;
  const left = Math.floor(width * 0.25);
  const right = Math.ceil(width * 0.75);
  const regionWidth = right - left;
  const samples = Math.min(30, regionWidth);
  const step = Math.max(1, Math.floor((regionWidth - 1) / samples));
  let firstContent = -1;
  for (let y = 0; y < height; y++) {
    const row = y * width * ch;
    let nonWhite = 0;
    for (let x = left; x < right; x += step) {
      const i = row + x * ch;
      if (raw[i] < 250 || raw[i + 1] < 250 || raw[i + 2] < 250) nonWhite++;
    }
    if (nonWhite >= 3) { firstContent = y; break; }
  }
  if (firstContent < 2) return buf;
  return sharp(buf).extract({ left: 0, top: firstContent, width, height: height - firstContent }).png().toBuffer();
}

async function capture(urls, viewports, onProgress, naming, rawOpts = {}) {
  if (!Array.isArray(urls) || urls.length === 0) {
    throw new Error('No URLs provided to capture');
  }
  if (!Array.isArray(viewports) || viewports.length === 0) {
    throw new Error('No viewports provided');
  }

  const opts = resolveOpts(rawOpts);
  if (opts.concurrency < 1) throw new Error('Concurrency must be at least 1');

  opts.initialDelayMs = toMs(opts.initialDelay, DEFAULTS.initialDelaySec);
  opts.scrollDelayMs = toMs(opts.scrollDelay, DEFAULTS.scrollDelaySec);
  opts.finalDelayMs = toMs(opts.finalDelay, DEFAULTS.finalDelaySec);

  if (!fs.existsSync(OUT_DIR)) fs.mkdirSync(OUT_DIR, { recursive: true });

  const browser = await launchBrowser();

  try {
    const total = urls.length;
    const concurrency = Math.min(opts.concurrency, total);
    const queue = urls.map((url, i) => ({ url, index: i }));

    const worker = async () => {
      while (true) {
        const item = queue.shift();
        if (!item) break;
        const context = await browser.newContext();
        const page = await context.newPage();
        try {
          if (opts.blockPopups) await attachPopupBlocker(page, opts.blocklist);
          await captureOneUrl(page, item.url, viewports, item.index, total, naming, opts, onProgress);
        } finally {
          await context.close();
        }
      }
    };

    const workers = Array.from({ length: concurrency }, () => worker());
    await Promise.all(workers);
  } finally {
    await browser.close();
  }

  onProgress?.({ type: 'done' });
}

module.exports = { capture, OUT_DIR, DEFAULTS, ensureScheme };
