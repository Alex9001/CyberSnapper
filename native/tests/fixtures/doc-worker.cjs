'use strict';

const args = process.argv.slice(2);
if (args.includes('--browsers')) {
  const ready = (name) => ({ executablePath: `/documentation-fixture/${name}`,
    installed: true, downloaded: true, ready: true, state: 'ready' });
  process.stdout.write(`${JSON.stringify({
    browsers: { chromium: ready('chromium'), firefox: ready('firefox'), webkit: ready('webkit') },
  })}\n`);
} else if (args.includes('--install') || args.includes('--verify')) {
  const action = args.includes('--verify') ? '--verify' : '--install';
  const engine = args[args.indexOf(action) + 1];
  process.stdout.write(`${JSON.stringify({ protocolVersion: 1,
    type: 'browser_install_result', engine, state: 'ready', downloaded: true,
    installed: true, ready: true, ok: true, message: `${engine} launched successfully.` })}\n`);
} else {
  process.stderr.write('The documentation fixture worker only reports browser readiness.\n');
  process.exitCode = 2;
}
