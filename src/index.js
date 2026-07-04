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

      const shutdown = () => {
        console.log('\n  Shutting down...');
        server.close(() => process.exit(0));
      };
      process.on('SIGINT', shutdown);
      process.on('SIGTERM', shutdown);

      if (process.stdout.isTTY) {
        console.log(`\n  ⌁ CyberSnapper\n`);
        console.log(`  Web UI: ${url}\n`);
        console.log('  Press Ctrl+C or click ⏹ Stop in the UI to exit.\n');
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
