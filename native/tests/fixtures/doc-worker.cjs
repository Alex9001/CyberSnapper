'use strict';

const args = process.argv.slice(2);
if (args.includes('--browsers')) {
  const ready = (name) => ({ executablePath: `/documentation-fixture/${name}`, installed: true });
  process.stdout.write(`${JSON.stringify({
    browsers: { chromium: ready('chromium'), firefox: ready('firefox'), webkit: ready('webkit') },
  })}\n`);
} else if (args.includes('--install')) {
  process.exitCode = 0;
} else {
  process.stderr.write('The documentation fixture worker only reports browser readiness.\n');
  process.exitCode = 2;
}
