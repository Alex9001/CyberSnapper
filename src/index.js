const { exec } = require('child_process');
const { startServer } = require('./server');
const { runCLI, readUrls } = require('./cli');
const { stopRunningInstance } = require('./server/pid');

function openInBrowser(url) {
  const cmd = process.platform === 'win32'
    ? `start "" "${url}"`
    : process.platform === 'darwin'
      ? `open "${url}"`
      : `xdg-open "${url}"`;
  exec(cmd, () => {});
}

async function runServer(shouldOpenBrowser) {
  const server = await startServer(0);
  const url = `http://localhost:${server.address().port}`;

  console.log('  ⌁ CyberSnapper');
  console.log(`  Web UI: ${url}\n`);
  console.log('  Press Ctrl+C or click ⏹ Stop in the UI to exit.');
  console.log('  Idles: auto-stop after 15 minutes of inactivity.\n');

  if (shouldOpenBrowser || !process.stdout.isTTY) {
    openInBrowser(url);
  }
}

async function main() {
  const args = process.argv.slice(2);

  if (args.length === 1 && (args[0] === '--stop' || args[0] === 'stop')) {
    stopRunningInstance();
    return;
  }

  const openFlagIdx = args.indexOf('--open');
  const shouldOpen = openFlagIdx !== -1;
  if (openFlagIdx !== -1) args.splice(openFlagIdx, 1);

  if (args.length > 0) {
    const urls = readUrls(args);
    await runCLI(urls);
  } else {
    await runServer(shouldOpen);
  }
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
