#!/usr/bin/env node
/**
 * Unit tests for src/capture/browser.js
 *
 * Tests error-matching patterns, the runInstall helper, and
 * status callback integration without needing Playwright or a browser.
 *
 * Run: node test/browser.test.js
 */

const { strict: assert } = require('assert');
const { runInstall } = require('../src/capture/browser');

let passed = 0;
let failed = 0;

async function test(name, fn) {
  try {
    await fn();
    console.log(`  ✓ ${name}`);
    passed++;
  } catch (err) {
    console.log(`  ✕ ${name}`);
    console.log(`    ${err.message}`);
    failed++;
  }
}

/* ================================================================
   1. Error message matching patterns
   ================================================================ */

function matchesChromiumMissing(msg) {
  return msg.includes("Executable doesn't exist");
}

function matchesDepsMissing(msg) {
  return msg.includes('missing dependencies') ||
    msg.includes('cannot open shared object') ||
    msg.includes('error while loading shared libraries') ||
    msg.includes('Host system is missing');
}

function testErrorMatching() {
  console.log('\n── Error message matching patterns ──\n');

  const cases = [
    // [message, shouldMatchChromium, shouldMatchDeps]
    ["Executable doesn't exist at /home/user/.cache/ms-playwright/chromium-1234/chrome-linux/chrome", true, false],
    ['browserType.launch: Host system is missing dependencies!', false, true],
    ['Error: cannot open shared object file: libX11-xcb.so.1: No such file or directory', false, true],
    ['error while loading shared libraries: libnss3.so: cannot open shared object file', false, true],
    ['Host system is missing required Linux libraries.', false, true],
    ['Host system is missing dependencies to run browsers.', false, true],
    ['Error: Cannot find module', false, false],
    ['Timeout exceeded 30000ms', false, false],
    ['page.goto: net::ERR_CONNECTION_REFUSED', false, false],
    ['', false, false],
    ['Error: EBUSY: resource busy or locked', false, false],
  ];

  for (const [msg, expChromium, expDeps] of cases) {
    test(`msg="${msg.slice(0, 50)}..." → chromium=${expChromium}, deps=${expDeps}`, () => {
      assert.equal(matchesChromiumMissing(msg), expChromium);
      assert.equal(matchesDepsMissing(msg), expDeps);
    });
  }
}

/* ================================================================
   2. runInstall helper (real implementation from browser.js)
   ================================================================ */

async function testRunInstall() {
  console.log('\n── runInstall helper (real implementation) ──\n');

  test('simple echo succeeds', async () => {
    const events = [];
    await runInstall('echo', ['hello'], 'Test', (ev) => events.push(ev));
    assert.equal(events.length, 0, 'no percent in output → no events');
  });

  test('single percentage line parsed', async () => {
    const events = [];
    await runInstall('bash', ['-c', 'echo "Downloading... 42%"'], 'Test', (ev) => events.push(ev));
    assert.equal(events.length, 1);
    assert.equal(events[0].percent, 42);
    assert.ok(events[0].message.includes('42%'));
  });

  test('multiple progression lines', async () => {
    const events = [];
    await runInstall('bash', ['-c', 'echo "0%"; echo "50%"; echo "100%"'], 'Test', (ev) => events.push(ev));
    assert.equal(events.length, 3);
    assert.equal(events[0].percent, 0);
    assert.equal(events[1].percent, 50);
    assert.equal(events[2].percent, 100);
  });

  test('stderr percentage also captured', async () => {
    const events = [];
    await runInstall('bash', ['-c', 'echo "Downloading... 75%" >&2'], 'Test', (ev) => events.push(ev));
    assert.equal(events.length, 1, 'stderr percentage is captured');
    assert.equal(events[0].percent, 75);
  });

  test('failing command rejects', async () => {
    await assert.rejects(
      () => runInstall('bash', ['-c', 'exit 1'], 'Test'),
      /exited with code 1/
    );
  });

  test('missing command rejects with ENOENT', async () => {
    await assert.rejects(
      () => runInstall('nonexistent-cmd-xyz', [], 'Test'),
      { code: 'ENOENT' }
    );
  });

  test('status events contain correct type and prefix', async () => {
    const events = [];
    await runInstall('bash', ['-c', 'echo "100%"'], 'My Prefix', (ev) => events.push(ev));
    assert.equal(events.length, 1);
    assert.equal(events[0].type, 'status');
    assert.ok(events[0].message.startsWith('My Prefix'));
  });
}

/* ================================================================
   3. Status callback integration (simulated flows)
   ================================================================ */

function testStatusCallbacks() {
  console.log('\n── Status callback integration ──\n');

  test('Chromium install flow emits expected events', () => {
    const events = [];
    const fire = (type, message, percent) => events.push({ type, message, percent });

    fire('status', 'Installing Chromium browser engine (may take a minute)...');
    fire('status', 'Installing Chromium... 0%', 0);
    fire('status', 'Installing Chromium... 47%', 47);
    fire('status', 'Installing Chromium... 100%', 100);
    fire('status', 'Chromium installed. Starting capture...');

    assert.equal(events.length, 5);
    assert.equal(events[0].percent, undefined, 'initial status has no percent');
    assert.equal(events[1].percent, 0);
    assert.equal(events[2].percent, 47);
    assert.equal(events[3].percent, 100);
    assert.equal(events[4].percent, undefined, 'completion status has no percent');
    assert.ok(events[4].message.includes('Starting capture'));
  });

  test('deps install flow emits expected events', () => {
    const events = [];
    const fire = (type, message, percent) => events.push({ type, message, percent });

    fire('status', 'Installing Chromium system dependencies (may take a minute)...');
    fire('status', 'Installing system deps... 33%', 33);
    fire('status', 'Installing system deps... 66%', 66);
    fire('status', 'Installing system deps... 100%', 100);
    fire('status', 'System dependencies installed. Starting capture...');

    assert.equal(events.length, 5);
    assert.ok(events[0].message.includes('system dependencies'));
    assert.equal(events[1].percent, 33);
    assert.equal(events[3].percent, 100);
    assert.ok(events[4].message.includes('Starting capture'));
  });

  test('error events have correct structure', () => {
    const events = [];
    const fire = (type, message) => events.push({ type, message });

    fire('error', 'Failed to install Chromium. Run: npx playwright install chromium');
    fire('error', 'Failed to install system deps. Run: npx playwright install-deps chromium');

    assert.equal(events.length, 2);
    assert.equal(events[0].type, 'error');
    assert.ok(events[0].message.includes('Failed'));
    assert.ok(events[1].message.includes('install-deps'));
  });
}

/* ================================================================
   Main
   ================================================================ */

async function main() {
  console.log('╔══════════════════════════════════════════════╗');
  console.log('║     browser.js Unit Test Suite              ║');
  console.log('╚══════════════════════════════════════════════╝');

  testErrorMatching();
  await testRunInstall();
  testStatusCallbacks();

  console.log(`\n  ${passed} passed, ${failed} failed\n`);

  if (failed > 0) process.exit(1);
}

main().catch(err => {
  console.error('Test suite error:', err);
  process.exit(1);
});
