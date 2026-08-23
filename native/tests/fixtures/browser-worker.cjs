const args = process.argv.slice(2);

if (args.includes('--browsers')) {
  const browser = (engine) => ({
    executablePath: `/fixture/${engine}`,
    installed: false,
    downloaded: false,
  });
  process.stdout.write(`${JSON.stringify({ browsers: {
    chromium: browser('chromium'),
    firefox: browser('firefox'),
    webkit: browser('webkit'),
  } })}\n`);
  process.exit(0);
}

const installIndex = args.indexOf('--install');
const verifyIndex = args.indexOf('--verify');
const engine = args[installIndex >= 0 ? installIndex + 1 : verifyIndex + 1];
const operation = verifyIndex >= 0 ? 'verifying' : 'downloading';
let done = false;

const finish = (state, code) => {
  if (done) return;
  done = true;
  process.stdout.write(`${JSON.stringify({
    protocolVersion: 1,
    type: 'browser_install_result',
    engine,
    state,
    downloaded: state === 'ready',
    installed: state === 'ready',
    ready: state === 'ready',
    ok: state === 'ready',
    message: state === 'ready' ? `${engine} launched successfully.` : `${engine} installation was cancelled.`,
  })}\n`);
  setTimeout(() => process.exit(code), 0);
};

process.stdout.write(`${JSON.stringify({
  protocolVersion: 1,
  type: 'browser_install_progress',
  engine,
  phase: operation,
  percent: operation === 'downloading' ? 25 : undefined,
  elapsedMs: 10,
  message: operation === 'verifying' ? `Checking ${engine}…` : `Downloading ${engine}…`,
})}\n`);

process.stdin.setEncoding('utf8');
process.stdin.on('data', (input) => {
  if (/"command"\s*:\s*"cancel"/.test(input)) finish('cancelled', 2);
});
setTimeout(() => finish('ready', 0), 500);
