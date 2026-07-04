const fs = require('fs');
const path = require('path');
const { capture, OUT_DIR } = require('./capture');

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

function readFile(filePath) {
  return fs.readFileSync(filePath, 'utf-8')
    .split('\n').map(l => l.trim()).filter(l => l.length > 0);
}

async function runCLI(urls) {
  if (urls.length === 0) {
    console.error('No valid URLs provided.');
    process.exit(1);
  }

  console.log('Launching browser...\n');

  await capture(urls, (event) => {
    switch (event.type) {
      case 'url-start':
        console.log(`${'='.repeat(60)}`);
        console.log(`[${event.index + 1}/${event.total}] ${event.url}`);
        console.log(`${'='.repeat(60)}\n`);
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
      case 'done':
        console.log(`\nDone! All screenshots saved to the "${OUT_DIR}" folder.\n`);
        break;
    }
  });
}

module.exports = { runCLI, readUrls };
