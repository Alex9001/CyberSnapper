const { runCLI, readUrls } = require('./src/cli');

(async () => {
  const urls = readUrls(process.argv.slice(2));
  await runCLI(urls);
})().catch(err => {
  console.error(err);
  process.exit(1);
});
