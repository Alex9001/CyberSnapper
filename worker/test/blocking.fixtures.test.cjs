'use strict';

// Deterministic browser fixtures for content blocking: a synthetic cookie
// banner with reject/dismiss controls, generic cosmetic targets, and a scroll
// lock. Uses only loopback HTTP, so it never touches the network.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');

let playwright;
try {
  playwright = require('playwright');
} catch {
  playwright = null;
}

const { ContentBlocker, sha256Hex } = require('../dist/testing.cjs');

// Engines available in this environment; the suite runs against each one so
// release CI can require full coverage while local runs stay opportunistic.
async function availableEngines() {
  if (!playwright) return [];
  const engines = [];
  for (const name of ['chromium', 'firefox', 'webkit']) {
    try {
      const executablePath = playwright[name].executablePath();
      if (!executablePath) continue;
      const probe = await playwright[name].launch();
      await probe.close();
      engines.push(name);
    } catch { /* engine not installed or missing system dependencies */ }
  }
  return engines;
}

let enginesPromise;
async function getEngines() {
  if (!enginesPromise) enginesPromise = availableEngines();
  return enginesPromise;
}

function enabledSettings() {
  return {
    enabled: true,
    consentStrategy: 'rejectThenDismiss',
    subscriptionIds: [],
    customRulesetIds: [],
    disabledDomains: [],
    versionPolicy: 'latest',
  };
}

function writeSnapshot(root, rulesText, { actions = [] } = {}) {
  const payload = JSON.stringify({
    generatedAt: '2026-01-01T00:00:00Z', subscriptions: {}, rulesText, actions,
  });
  const digest = sha256Hex(payload);
  const snapshotText = JSON.stringify({ format: 1, digest, payload });
  fs.mkdirSync(path.join(root, 'rulesets'), { recursive: true });
  fs.writeFileSync(path.join(root, 'rulesets', `${digest}.json`), snapshotText);
  return { digest, relativePath: `rulesets/${digest}.json` };
}

async function withFixture(pageHtml, rulesText, run, settingsExtra = {}) {
  const engines = await getEngines();
  if (engines.length === 0) return;
  
  const server = http.createServer((request, response) => {
    response.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
    response.end(pageHtml);
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const url = `http://127.0.0.1:${server.address().port}/`;
  
  for (const engineName of engines) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'cs-fixture-'));
    const ruleset = writeSnapshot(root, rulesText, settingsExtra);
    const blocker = await ContentBlocker.load({
      projectRoot: root,
      profile: { contentBlocking: { ...enabledSettings(), ...settingsExtra } },
      ruleset,
    });
    const browser = await playwright[engineName].launch();
    try {
      const context = await browser.newContext({ viewport: { width: 1280, height: 800 } });
      const page = await context.newPage();
      await page.goto(url, { waitUntil: 'domcontentloaded' });
      await run(blocker, page, engineName);
    } finally {
      await browser.close().catch(() => {});
      fs.rmSync(root, { recursive: true, force: true });
    }
  }
  server.close();
}

const bannerPage = `<!doctype html><html><body style="overflow:hidden">
  <div id="cookieConsent" style="position:fixed;bottom:0;left:0;right:0;height:120px">
    <p>We use cookies</p>
    <button id="reject-all" onclick="this.closest('#cookieConsent').remove()">Reject all</button>
    <button id="accept-all">Accept all</button>
  </div>
  <div class="cookie-banner promo-overlay" style="width:600px;height:80px">banner text</div>
  <main style="height:2000px">content</main>
</body></html>`;

const conditional = Boolean(playwright) && process.env.CYBERSNAPPER_SKIP_BROWSER_TESTS !== '1'
  ? test : test.skip;

conditional('cosmetic filters hide generic class-based banners in every pass', async () => {
  await withFixture(bannerPage, ['##.cookie-banner'], async (blocker, page, engineName) => {
    await blocker.applyCosmeticFilters(page);
    assert.equal(await page.evaluate(() =>
      getComputedStyle(document.querySelector('.promo-overlay')).display === 'none'), true);
    // A second pass must not double-count the same frame.
    await blocker.applyCosmeticFilters(page);
    assert.equal(blocker.metrics.cosmeticRulesApplied, 1);
  });
});

conditional('consent pass rejects non-essential cookies and releases the scroll lock', async () => {
  await withFixture(bannerPage, [], async (blocker, page, engineName) => {
    await blocker.runConsentPass(page);
    assert.equal(blocker.metrics.consentActionsAttempted >= 1, true);
    assert.equal(blocker.metrics.consentActionsSucceeded >= 1, true);
    assert.equal(await page.evaluate(() => document.getElementById('cookieConsent') === null), true,
      'the identified banner should be gone after clicking its reject control');
    assert.equal(await page.evaluate(() => getComputedStyle(document.body).overflow), 'auto',
      'scroll locking is restored only when a recognized overlay was removed');
  });
});

conditional('dismiss-only strategy clicks accept instead of reject', async () => {
  const dismissPage = bannerPage.replace(
    'id="accept-all">Accept all',
    'id="accept-all" onclick="this.closest(\'#cookieConsent\').remove()">Accept all');
  await withFixture(dismissPage, [], async (blocker, page, engineName) => {
    await blocker.runConsentPass(page);
    assert.equal(blocker.metrics.consentActionsSucceeded, 1);
    assert.equal(await page.evaluate(() =>
      document.getElementById('accept-all') !== null), false);
  }, { consentStrategy: 'dismiss' });
});

conditional('a legitimate application modal without consent markers stays visible', async () => {
  const modalPage = `<!doctype html><html><body style="overflow:hidden">
    <div id="appModal" role="dialog" aria-label="Upgrade plan"
         style="position:fixed;top:100px;width:500px;height:200px">
      <h2>Upgrade your workspace</h2>
      <button onclick="this.closest('#appModal').remove()">Not now</button>
    </div>
    <main style="height:1500px">content</main>
  </body></html>`;
  await withFixture(modalPage, [], async (blocker, page) => {
    await blocker.runConsentPass(page);
    await blocker.runConsentPass(page);
    assert.equal(blocker.metrics.consentActionsAttempted, 0,
      'no consent action may be attempted against an ordinary application dialog');
    assert.equal(await page.evaluate(() => document.getElementById('appModal') !== null), true);
  });
});

conditional('structured custom actions click their selector on scoped domains', async () => {
  const actionPage = `<!doctype html><html><body style="overflow:hidden">
    <div id="consentDialog" style="position:fixed;width:400px;height:80px">
      Choose your settings
      <button id="other-button">Other</button>
      <button id="save-choice" onclick="this.closest('#consentDialog').remove()">Save choice</button>
    </div>
  </body></html>`;
  await withFixture(actionPage, [], async (blocker, page, engineName) => {
    await blocker.runConsentPass(page);
    assert.equal(blocker.metrics.consentActionsAttempted, 1);
    assert.equal(blocker.metrics.consentActionsSucceeded, 1);
    assert.equal(await page.evaluate(() => document.getElementById('consentDialog') === null), true);
  }, {
    actions: [{ domains: ['127.0.0.1'], selector: '#save-choice', action: 'click', delayMs: 50 }],
  });
});
