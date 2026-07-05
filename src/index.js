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

async function runServer() {
  const server = await startServer(0);
  const url = `http://localhost:${server.address().port}`;

  console.log('  ⌁ CyberSnapper');
  console.log(`  Web UI: ${url}\n`);
  console.log('  Press Ctrl+C or click ⏹ Stop in the UI to exit.');
  console.log('  Idles: auto-stop after 15 minutes of inactivity.\n');

  if (!process.stdout.isTTY) {
    openInBrowser(url);
  }
}

async function main() {
  const args = process.argv.slice(2);

  if (args.length === 1 && (args[0] === '--stop' || args[0] === 'stop')) {
    stopRunningInstance();
    return;
  }
  if (args.length > 0) {
    const urls = readUrls(args);
    await runCLI(urls);
  } else {
    await runServer();
  }
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
