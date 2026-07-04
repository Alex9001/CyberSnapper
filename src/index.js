const { exec } = require('child_process');
const { startServer } = require('./server');
const { runCLI, readUrls } = require('./cli');

const args = process.argv.slice(2);

if (args.length > 0) {
  const urls = readUrls(args);
  runCLI(urls).catch(err => {
    console.error(err);
    process.exit(1);
  });
} else {
  (async () => {
    try {
      const server = await startServer(0);
      const port = server.address().port;
      const url = `http://localhost:${port}`;

      if (process.stdout.isTTY) {
        console.log(`\n  ⌁ CyberSnapper\n`);
        console.log(`  Web UI: ${url}\n`);
        console.log('  Press Ctrl+C to stop.\n');
      } else {
        const cmd = process.platform === 'win32'
          ? `start "" "${url}"`
          : process.platform === 'darwin'
            ? `open "${url}"`
            : `xdg-open "${url}"`;
        exec(cmd, () => {});
      }
    } catch (err) {
      console.error('Failed to start server:', err.message);
      process.exit(1);
    }
  })();
}
