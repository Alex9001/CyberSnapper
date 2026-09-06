const test = require('node:test');
const assert = require('node:assert/strict');
const { mkdtemp, rm } = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const { once } = require('node:events');
const { chromium } = require('playwright');
const sharp = require('sharp');
const { runCaptureJob } = require('../dist/testing.cjs');

// This fixture starts a browser for each of four capture jobs. Windows arm64
// also probes system Chrome/Edge before falling back to emulated Chromium.
const fixtureTimeout = process.platform === 'win32' && process.arch === 'arm64' ? 300000 : 60000;
test('captures browser light/dark preferences, filenames, progress, skips and failures', { timeout: fixtureTimeout }, async (t) => {
  try { const browser = await chromium.launch(); await browser.close(); }
  catch (error) {
    if ((process.env.CYBERSNAPPER_REQUIRED_BROWSERS || '').includes('chromium')) throw error;
    t.skip('Chromium is not installed or cannot launch'); return;
  }
  const root = await mkdtemp(path.join(os.tmpdir(), 'cybersnapper-themes-'));
  const server = http.createServer((request, response) => {
    if (request.url === '/fail') { response.writeHead(500); response.end('failure'); return; }
    response.setHeader('Content-Type', 'text/html');
    if (request.url === '/static') {
      response.end('<html><style>html{scrollbar-gutter:stable}::-webkit-scrollbar{width:10px}body{margin:0;background:rgb(30,60,90);height:160px}</style><body></body></html>');
      return;
    }
    response.end(`<html><style>html,body{margin:0;background:rgb(255,255,255)}
      @media(prefers-color-scheme:dark){html,body{background:rgb(0,0,0)}}</style>
      <script>document.title=matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light'</script></html>`);
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const url = `http://127.0.0.1:${server.address().port}/sample`;
  const job = { id: 'theme-fixture', projectId: 'fixture', projectRoot: root, urls: [url], allowLocalhost: true,
    profile: { engines: ['chromium'], formats: ['png'], colorScheme: 'both', captureMode: 'viewport',
      viewports: [{ id: 'desktop', name: 'Desktop', width: 100, height: 100, deviceScaleFactor: 1, enabled: true, mobile: false }],
      initialDelay: 0, scrollDelay: 0, finalDelay: 0, concurrency: 2, navigationTimeoutSeconds: 10,
      selectorTimeoutSeconds: 5, maxPageHeight: 10000, maxScrollSeconds: 5, stripWhitespace: false,
      blocklist: [], hideSelectors: [], comparisonIgnoreSelectors: [], waitForSelector: '',
      collisionPolicy: 'overwrite', namingTemplate: '', comparisonEnabled: true,
      pixelThreshold: 0.1, mismatchThreshold: 0.001, presentation: { enabled: true, frame: 'none' },
      contentBlocking: { enabled: false } } };
  const run = async (onEvent) => {
    const events = [];
    const runtime = { cancelled: false, browsers: new Set() };
    await runCaptureJob(job, runtime, event => { events.push(event); onEvent?.(event, runtime); });
    assert.equal(runtime.browsers.size, 0);
    return events;
  };
  try {
    t.diagnostic('Capturing paired originals and portfolio copies');
    const events = await run();
    assert.equal(events[0].totalArtifacts, 4);
    assert.equal(events[0].totalTargets, 2);
    assert.equal(events.at(-1).type, 'job_succeeded');
    const artifacts = events.filter(e => e.type === 'artifact_completed').map(e => e.artifact);
    assert.equal(artifacts.length, 4);
    assert.equal(new Set(artifacts.map(a => a.relativePath)).size, 4);
    for (const scheme of ['light', 'dark']) {
      const artifact = artifacts.find(a => a.colorScheme === scheme && a.variant === 'original');
      assert.equal(path.basename(artifact.relativePath), `127.0.0.1-sample-Desktop-${scheme}.png`);
      const pixel = await sharp(path.join(root, artifact.relativePath)).extract({ left: 0, top: 0, width: 1, height: 1 }).removeAlpha().raw().toBuffer();
      assert.deepEqual([...pixel], scheme === 'dark' ? [0, 0, 0] : [255, 255, 255]);
    }
    const comparisons = events.filter(e => e.type === 'comparison_completed').map(e => e.comparison);
    assert.equal(new Set(comparisons.map(c => c.comparisonKey)).size, 2);
    assert.equal(comparisons.find(c => c.colorScheme === 'dark').comparisonKey, `${url}|chromium|desktop|viewport|png|dark`);
    assert.deepEqual(events.filter(e => e.type === 'job_progress').map(e => e.completed), [1, 2, 3, 4]);
    const stages = events.filter(e => e.type === 'target_progress');
    assert.deepEqual([...new Set(stages.map(e => e.position))].sort(), [1, 2]);
    for (const stage of ['Loading page', 'Taking screenshot', 'Writing PNG original', 'Rendering PNG portfolio copy']) {
      assert.ok(stages.some(e => e.stage === stage), stage);
    }
    // Same output paths and skip policy must preserve both themes and finish the plan.
    job.profile.collisionPolicy = 'skip';
    t.diagnostic('Checking skipped paired outputs');
    const skipped = await run();
    assert.equal(skipped.filter(e => e.artifact?.status === 'skipped').length, 4);
    assert.equal(skipped.at(-1).completed, 4);
    job.urls = [url.replace('/sample', '/fail')];
    t.diagnostic('Checking paired failure counts');
    const failed = await run();
    assert.equal(failed.at(-1).type, 'job_failed');
    assert.equal(failed.at(-1).failed, 4);
    assert.deepEqual(failed.filter(e => e.type === 'job_progress').map(e => e.failed), [1, 2, 3, 4]);
    // Cancellation does not claim completion for the remaining theme or files.
    job.urls = [url]; job.profile.concurrency = 1;
    t.diagnostic('Checking cancellation before remaining theme');
    const cancelled = await run((event, runtime) => { if (event.type === 'artifact_completed') runtime.cancelled = true; });
    assert.equal(cancelled.at(-1).type, 'job_cancelled');
    assert.ok(cancelled.at(-1).completed < 4);

    t.diagnostic('Omitting identical themes and removing reserved scrollbar gutters');
    job.urls = [url.replace('/sample', '/static')];
    job.profile.captureMode = 'fullPage';
    job.profile.collisionPolicy = 'overwrite';
    const identical = await run();
    const originals = identical.filter(e => e.artifact?.variant === 'original' && e.type === 'artifact_completed');
    assert.equal(originals.length, 1);
    assert.equal(originals[0].artifact.colorScheme, 'light');
    assert.equal(identical.filter(e => e.type === 'target_theme_redundant').length, 1);
    assert.equal(identical.filter(e => e.type === 'job_progress').at(-1).totalArtifacts, 2);
    assert.equal(identical.at(-1).completed, 2);
    assert.equal(identical.at(-1).omittedArtifacts, 2);
    const saved = path.join(root, originals[0].artifact.relativePath);
    const metadata = await sharp(saved).metadata();
    assert.equal(metadata.width, 100);
    assert.equal(metadata.height, 160);
    const rightEdge = await sharp(saved).extract({ left: 99, top: 0, width: 1, height: 160 }).removeAlpha().raw().toBuffer();
    for (let offset = 0; offset < rightEdge.length; offset += 3) {
      assert.deepEqual([...rightEdge.subarray(offset, offset + 3)], [30, 60, 90]);
    }

    t.diagnostic('Keeping dark-only captures and existing dark comparison baselines');
    job.profile.colorScheme = 'dark';
    const darkOnly = await run();
    assert.equal(darkOnly.filter(e => e.type === 'artifact_completed').length, 2);
    assert.equal(darkOnly.filter(e => e.type === 'target_theme_redundant').length, 0);
    const darkArtifact = darkOnly.find(e => e.artifact?.variant === 'original').artifact;
    const darkKey = `${job.urls[0]}|chromium|desktop|fullPage|png|dark`;
    job.baselines = { [darkKey]: { comparisonKey: darkKey, artifactId: darkArtifact.id, artifact: darkArtifact } };
    job.profile.colorScheme = 'both';
    const monitored = await run();
    assert.equal(monitored.filter(e => e.type === 'artifact_completed').length, 4);
    assert.equal(monitored.filter(e => e.type === 'target_theme_redundant').length, 0);

    t.diagnostic('Keeping concurrent target pairs isolated');
    job.baselines = {};
    job.profile.captureMode = 'viewport';
    job.profile.concurrency = 2;
    job.urls = [url.replace('/sample', '/static'), url];
    const mixed = await run();
    assert.equal(mixed.filter(e => e.type === 'artifact_completed').length, 6);
    const omitted = mixed.filter(e => e.type === 'target_theme_redundant');
    assert.equal(omitted.length, 1);
    assert.equal(omitted[0].url, job.urls[0]);
    assert.equal(mixed.filter(e => e.type === 'job_progress').at(-1).totalArtifacts, 6);
    assert.equal(mixed.at(-1).type, 'job_succeeded');
  } finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    await rm(root, { recursive: true, force: true });
  }
});
