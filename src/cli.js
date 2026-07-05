const fs = require('fs');
const path = require('path');
const { capture, OUT_DIR } = require('./capture');
const config = require('./config');

function findDefaultUrlsFile() {
  const candidates = [
    path.join(process.cwd(), 'urls', 'urls.txt'),
    path.join(path.dirname(process.execPath), 'urls', 'urls.txt'),
  ];
  for (const f of candidates) {
    if (fs.existsSync(f)) return f;
  }
  return null;
}

function readFile(filePath) {
  return fs.readFileSync(filePath, 'utf-8')
    .split('\n').map(l => l.trim()).filter(l => l.length > 0);
}

function readUrls(args) {
  if (args.length === 0) {
    const defaultFile = findDefaultUrlsFile();
    if (defaultFile) {
      console.log(`No arguments provided — using default URL file: ${defaultFile}\n`);
      return readFile(defaultFile);
    }
    console.error('Usage: node capture.js <url1 url2 ...>  or  node capture.js <urls.txt>');
    process.exit(1);
  }
  if (args.length === 1 && fs.existsSync(args[0])) return readFile(args[0]);
  return args;
}

const BAR = '='.repeat(60);

async function runCLI(urls) {
  if (!Array.isArray(urls) || urls.length === 0) {
    console.error('No valid URLs provided.');
    process.exit(1);
  }

  const cfg = config.load();
  const {
    presets, naming,
    initialDelay, scrollDelay, finalDelay,
    concurrency, formats,
    blockPopups,
    hideSelectors, waitForSelector,
    webp, avif, pdf,
  } = cfg;

  console.log(`Launching browser (${presets.length} presets)...\n`);
  console.log(`  Naming template: ${naming.template}`);
  console.log(`  Delays: initial=${initialDelay}s, scroll=${scrollDelay}s, final=${finalDelay}s`);
  console.log(`  Concurrency: ${concurrency}`);
  console.log(`  Formats: ${formats.join(', ')}`);
  console.log(`  Popup blocking: ${blockPopups ? 'ON' : 'OFF'}\n`);

  await capture(urls, presets, (event) => {
    switch (event.type) {
      case 'url-start':
        console.log(BAR);
        console.log(`[${event.index + 1}/${event.total}] ${event.url}`);
        console.log(`${BAR}\n`);
        break;
      case 'url-error':
        console.error(`  Invalid URL, skipping.\n`);
        break;
      case 'viewport-start':
        process.stdout.write(`  ${event.viewport} ... `);
        break;
      case 'viewport-error':
        console.error(`FAILED (${event.message})`);
        break;
      case 'viewport-done':
        console.log(`saved`);
        break;
      case 'warning':
        console.error(`\n  WARN: ${event.message}`);
        break;
      case 'done':
        console.log(`\nDone! All screenshots saved to the "${OUT_DIR}" folder.\n`);
        break;
    }
  }, naming, {
    initialDelay,
    scrollDelay,
    finalDelay,
    concurrency,
    formats,
    blockPopups,
    hideSelectors,
    waitForSelector,
    webp,
    avif,
    pdf,
  });
}

module.exports = { runCLI, readUrls };
