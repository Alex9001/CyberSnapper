const { runCLI, readUrls } = require('./src/cli');
const { stopRunningInstance } = require('./src/server/pid');

(async () => {
  const args = process.argv.slice(2);
  if (args.length === 1 && (args[0] === '--stop' || args[0] === 'stop')) {
    stopRunningInstance();
    return;
  }
  const urls = readUrls(args);
  await runCLI(urls);
})().catch(err => {
  console.error(err);
  process.exit(1);
});
