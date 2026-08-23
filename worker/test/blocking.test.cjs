'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const {
  ContentBlocker, decideRoute, hostMatches, mapResourceType, normalizeRulesText,
  parseTrustedClickAction, sha256Hex, verifySnapshotText,
} = require('../dist/testing.cjs');

function makeSnapshot(payloadExtra = {}) {
  const payload = JSON.stringify({
    generatedAt: '2026-01-01T00:00:00Z',
    subscriptions: { 'easylist-cookie': { version: '1.2.3' } },
    rulesText: ['||ads.example.com^', 'example.org##.banner'],
    actions: [],
    ...payloadExtra,
  });
  return JSON.stringify({ format: 1, digest: sha256Hex(payload), payload });
}

test('normalizeRulesText keeps safe network and cosmetic filters', () => {
  const result = normalizeRulesText([
    '! comment',
    '[Adblock Plus 2.0]',
    '',
    '||ads.example.com^$script,third-party',
    '@@||good.example.com^',
    'example.org##.cookie-banner',
    'example.org#?#.promo:has(.ad)',
    '#@#.optional-hide',
  ].join('\n'));
  assert.equal(result.unsupported, 0);
  assert.equal(result.lines.length, 5);
  assert.ok(result.lines.includes('||ads.example.com^$script,third-party'));
  assert.ok(result.lines.includes('#@#.optional-hide'));
});

test('normalizeRulesText rejects unsafe network options and constructs', () => {
  const unsafe = [
    '||tracker.example.com^$redirect=noop.txt',
    '*$csp=script-src none,domain=example.com',
    'example.com^$permissions=camera()',
    '||x.example.com^$replace=/(bad)/(good)/',
    '||y.example.com^$removeparam=utm_source',
    '||z.example.com^$urltransform=/a/b/',
    'example.com#$#body { display:none }',
    'example.com#%#window.x = 1;',
    'example.com##^script:has-text(boom)',
    'example.com##.thing:style(color:red)',
    'example.com##+js(set-constant, a.b, 1)',
    '!#if cap_html_filtering',
    'example.com##.ok-rule:remove()',
  ];
  const result = normalizeRulesText(unsafe.join('\n'));
  assert.equal(result.lines.length, 0);
  assert.equal(result.unsupported, unsafe.length);
});

test('normalizeRulesText extracts only trusted-click-element scriptlet actions', () => {
  const good = parseTrustedClickAction('consent.example.com',
    '+js(trusted-click-element, button#reject-all, button.confirm, 300)');
  assert.ok(good);
  assert.deepEqual(good.domains, ['consent.example.com']);
  assert.equal(good.action, 'click');
  assert.equal(good.delayMs, 300);
  assert.equal(good.selector, 'button#reject-all, button.confirm');

  // Untrusted or malformed scriptlets must be rejected.
  for (const [domains, body] of [
    ['example.com', '+js(set-constant, document.title, x)'],
    ['example.com', '+js(trusted-replace-fetch-response, x)'],
    ['', '+js(trusted-click-element, button)'],
    ['~skip.example.com,example.com', '+js(trusted-click-element, button)'],
    ['example.com', '+js(trusted-click-element, button; alert(1))'],
    ['example.com', '+js(trusted-click-element, a)'],
    ['bad domain!.com', '+js(trusted-click-element, button)'],
  ]) {
    assert.equal(parseTrustedClickAction(domains, body), null);
  }
});

test('verifySnapshotText validates format and digest integrity', () => {
  const text = makeSnapshot();
  const snapshot = verifySnapshotText(text);
  assert.equal(snapshot.format, 1);
  assert.deepEqual(snapshot.payload.rulesText, ['||ads.example.com^', 'example.org##.banner']);

  const tampered = JSON.parse(makeSnapshot());
  tampered.payload = JSON.stringify({ generatedAt: 'evil', subscriptions: {}, rulesText: [], actions: [] });
  assert.throws(() => verifySnapshotText(JSON.stringify(tampered)), /digest mismatch/i);

  assert.throws(() => verifySnapshotText('{"format":2,"digest":"x","payload":"{}"}'));
  const badAction = makeSnapshot({
    actions: [{ domains: ['e.com'], selector: 'ok', action: 'eval', delayMs: 0 }],
  });
  assert.throws(() => verifySnapshotText(badAction), /structured action/i);
});

test('mapResourceType never reports main documents to community filters', () => {
  assert.equal(mapResourceType('document'), 'sub_frame');
  assert.equal(mapResourceType('xhr'), 'xmlhttprequest');
  assert.equal(mapResourceType('script'), 'script');
  assert.equal(mapResourceType('unknown'), 'other');
});

test('hostMatches supports suffix scoping with optional wildcards', () => {
  assert.equal(hostMatches('www.example.com', 'example.com'), true);
  assert.equal(hostMatches('example.com', 'example.com'), true);
  assert.equal(hostMatches('notexample.com', 'example.com'), false);
  assert.equal(hostMatches('a.b.example.com', '*.example.com'), true);
});

test('ContentBlocker blocks matching subresources but not unrelated requests', async () => {
  const reference = snapshotReference(makeSnapshot(), ['||blocked.example.net^']);
  const blocker = await ContentBlocker.load({
    projectRoot: reference.projectRoot,
    profile: { contentBlocking: enabledSettings(['easylist-cookie']) },
    ruleset: reference,
  });
  assert.equal(blocker.enabled, true);
  assert.equal(blocker.hasEngine, true);
  assert.equal(blocker.blocksSubresource('https://blocked.example.net/tracker.js', 'script'), true);
  assert.equal(blocker.blocksSubresource('https://fine.example/page.css', 'stylesheet'), false);
  assert.equal(blocker.metrics.blockedSubresources, 0, 'counting happens in routing, not matching');
});

test('disabledDomains disable all content blocking for that site', async () => {
  const settings = enabledSettings(['easylist-cookie']);
  settings.disabledDomains = ['exempt.example'];
  const reference = snapshotReference(makeSnapshot(), ['##.cookie-banner']);
  const blocker = await ContentBlocker.load({
    projectRoot: reference.projectRoot,
    profile: { contentBlocking: settings },
    ruleset: reference,
  });
  assert.equal(blocker.blocksSubresource('https://exempt.example/ad.js', 'script'), false);
  assert.equal(blocker.cosmeticStyles('https://exempt.example/', domWithCookieClass), null);
  assert.equal(
    blocker.cosmeticStyles('https://other.example/', domWithCookieClass),
    '.cookie-banner { display: none !important; }',
  );
});

test('inert blockers do nothing (blockPopups=false stays off)', async () => {
  const blocker = ContentBlocker.inert(enabledSettings([]));
  assert.equal(blocker.enabled, false);
  assert.equal(blocker.blocksSubresource('https://anything.example/x.js', 'script'), false);
  assert.equal(blocker.cosmeticStyles('https://anything.example/', domWithCookieClass), null);
});

test('missing or broken snapshots warn and fall back to built-in consent handling', async () => {
  const missing = await ContentBlocker.load({
    projectRoot: os.tmpdir(),
    profile: { contentBlocking: enabledSettings([]) },
    ruleset: {},
  });
  assert.equal(missing.hasEngine, false);
  assert.equal(missing.metrics.staleCache, true);
  assert.ok(missing.metrics.warnings.join(' ').includes('built-in consent handling'));

  const badDigest = await ContentBlocker.load({
    projectRoot: writeProjectWithSnapshot('{"format":1,"digest":"00","payload":"{}"}'),
    profile: { contentBlocking: enabledSettings([]) },
    ruleset: { digest: sha256Hex('mismatch'), relativePath: 'rulesets/x.json' },
  });
  assert.equal(badDigest.metrics.staleCache, true);
  assert.ok(badDigest.metrics.warnings.some((w) => /could not be loaded|digest/i.test(w)));
});


function enabledSettings(subscriptionIds) {
  return {
    enabled: true,
    consentStrategy: 'rejectThenDismiss',
    subscriptionIds,
    customRulesetIds: [],
    disabledDomains: [],
    versionPolicy: 'latest',
  };
}

function snapshotReference(snapshotText, extraRules) {
  const document = JSON.parse(snapshotText);
  const payload = JSON.stringify({
    ...JSON.parse(document.payload),
    rulesText: [...JSON.parse(document.payload).rulesText, ...(extraRules ?? [])],
  });
  const finalText = JSON.stringify({ format: 1, digest: sha256Hex(payload), payload });
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'cybersnapper-rules-'));
  fs.mkdirSync(path.join(root, 'rulesets'), { recursive: true });
  fs.writeFileSync(path.join(root, 'rulesets', `${sha256Hex(payload)}.json`), finalText);
  return { projectRoot: root, digest: sha256Hex(payload), relativePath: `rulesets/${sha256Hex(payload)}.json` };
}

const domWithCookieClass = { classes: ['cookie-banner'], ids: [], hrefs: [] };

function writeProjectWithSnapshot(text) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'cybersnapper-bad-'));
  fs.mkdirSync(path.join(root, 'rulesets'), { recursive: true });
  fs.writeFileSync(path.join(root, 'rulesets', `${sha256Hex('mismatch')}.json`), text);
  return root;
}

function fakeRequest(url, { resourceType = 'script', navigation = false, mainFrame = true } = {}) {
  return {
    url: () => url,
    resourceType: () => resourceType,
    isNavigationRequest: () => navigation,
    frame: () => (mainFrame ? { parentFrame: () => null } : { parentFrame: () => ({}) }),
  };
}

async function blockerWithRules(rules, settingsExtra = {}) {
  const reference = snapshotReference(makeSnapshot(), rules);
  const blocker = await ContentBlocker.load({
    projectRoot: reference.projectRoot,
    profile: { contentBlocking: { ...enabledSettings(['easylist-cookie']), ...settingsExtra } },
    ruleset: reference,
  });
  assert.equal(blocker.hasEngine, true);
  return blocker;
}

test('routing order: community exceptions can never bypass the network-security policy', async () => {
  // The engine contains ONLY an allow exception for the loopback host. If
  // filters were consulted before the security policy, this request would
  // continue; the policy decision must win instead.
  const blocker = await blockerWithRules(['@@||127.0.0.1^']);
  const decision = await decideRoute(
    fakeRequest('http://127.0.0.1:39070/tracker.js'),
    [], {}, blocker);
  assert.deepEqual(decision, { action: 'abort', reason: 'policy' });
  assert.equal(blocker.metrics.blockedSubresources, 0);
});

test('routing order: the main document request is never community-blocked', async () => {
  const blocker = await blockerWithRules(['||ads.example.net^']);
  const permissive = async () => {};
  const decision = await decideRoute(
    fakeRequest('https://ads.example.net/', { resourceType: 'document', navigation: true }),
    [], {}, blocker, permissive);
  assert.deepEqual(decision, { action: 'continue' });
});

test('routing order: matched subresources abort after the policy passes', async () => {
  const blocker = await blockerWithRules(['||ads.example.net^']);
  const permissive = async () => {};
  const first = await decideRoute(
    fakeRequest('https://ads.example.net/pixel.js', { mainFrame: false }),
    [], {}, blocker, permissive);
  assert.equal(first.action, 'abort');
  assert.equal(first.reason, 'filter');
  assert.equal(blocker.metrics.blockedSubresources, 1);
});

test('routing order: user blocklist fragments abort before anything else', async () => {
  const blocker = await blockerWithRules([]);
  const never = async () => { throw new Error('policy check must not run for blocklisted URLs'); };
  const decision = await decideRoute(
    fakeRequest('https://127.0.0.1/analytics.js?blocked-fragment=1'),
    ['blocked-fragment'], {}, blocker, never);
  assert.deepEqual(decision, { action: 'abort', reason: 'blocklist' });
});
