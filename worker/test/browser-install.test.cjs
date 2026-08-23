const test = require('node:test');
const assert = require('node:assert/strict');
const { access } = require('node:fs/promises');
const {
  parseBrowserLaunchError, parseInstallOutputLine, resolvePlaywrightCli, stripAnsi,
} = require('../dist/testing.cjs');

test('Playwright CLI is resolved through the exported manifest', async () => {
  const cli = await resolvePlaywrightCli();
  await assert.doesNotReject(access(cli));
  assert.match(cli, /playwright[/\\]cli\.js$/);
});

test('install output becomes component-scoped structured progress', () => {
  const context = { component: '' };
  const start = parseInstallOutputLine(
    'Downloading WebKit 26.5 (playwright webkit v2311) from https://example.invalid/webkit.zip',
    context,
  );
  assert.equal(start.phase, 'downloading');
  assert.equal(context.component, 'WebKit 26.5');
  const progress = parseInstallOutputLine('|■■■■■■■■                        |  40% of 100.9 MiB', context);
  assert.deepEqual(progress, {
    phase: 'downloading', component: 'WebKit 26.5', percent: 40,
    total: '100.9 MiB', message: '|■■■■■■■■                        |  40% of 100.9 MiB',
  });
  const complete = parseInstallOutputLine('WebKit downloaded to /managed/cache/webkit-2311', context);
  assert.equal(complete.phase, 'extracting');
  assert.equal(complete.percent, 100);
});

test('ANSI output and missing-library diagnostics are sanitized', () => {
  assert.equal(stripAnsi('\u001b[2mDownloading Firefox\u001b[22m'), 'Downloading Firefox');
  const diagnostics = parseBrowserLaunchError(`Host system is missing dependencies to run browsers.
    libicudata.so.74
    libsystemd.so.0
    libicudata.so.74`, 'linux', 'Artix Linux');
  assert.equal(diagnostics.code, 'missing_dependencies');
  assert.deepEqual(diagnostics.missingLibraries, ['libicudata.so.74', 'libsystemd.so.0']);
  assert.equal(diagnostics.distribution, 'Artix Linux');
});
