const test = require('node:test');
const assert = require('node:assert/strict');
const { mkdtemp, rm } = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const { once } = require('node:events');
const { assertPublicUrl, captureBaseName, captureName, OutputPathAllocator, safeSegment, startFilteringProxy } = require('../dist/testing.cjs');

test('safeSegment creates portable filenames', () => {
  assert.equal(safeSegment(' A page: with / invalid * things '), 'A-page-with-invalid-things');
  assert.equal(safeSegment('CON'), 'capture');
});

test('captureName safely preserves intentional template folders', () => {
  const job = { id: '12345678-rest', profile: { namingTemplate: '{date}/{hostname}/{index}-{preset}' } };
  const viewport = { id: 'mobile', name: 'Mobile', width: 375, height: 812 };
  const name = captureName(job, 'https://example.com', viewport, 'chromium', 4);
  assert.equal(name.directories.length, 2);
  assert.equal(name.directories[1], 'example.com');
  assert.equal(name.base, '05-Mobile');
});

test('captureBaseName expands the configured template', () => {
  const job = { id: '12345678-rest', profile: { namingTemplate: '{hostname}-{preset}-{engine}-{width}' } };
  const viewport = { id: 'mobile', name: 'Mobile Phone', width: 375, height: 812 };
  assert.equal(captureBaseName(job, 'https://example.com/docs/start', viewport, 'chromium'),
               'example.com-Mobile-Phone-chromium-375');
});

test('network guard rejects local and private targets without navigation', async () => {
  await assert.rejects(assertPublicUrl('http://localhost:3000'), /Private or local/);
  await assert.rejects(assertPublicUrl('http://127.0.0.1'), /Private network/);
  await assert.rejects(assertPublicUrl('file:///etc/passwd'), /Unsupported URL protocol/);
});

test('network guard allows loopback only with explicit project policy', async () => {
  await assert.doesNotReject(assertPublicUrl('http://localhost:3000', { allowLocalhost: true }));
  await assert.doesNotReject(assertPublicUrl('http://127.0.0.1', { allowLocalhost: true }));
  await assert.rejects(assertPublicUrl('http://192.168.1.10', { allowLocalhost: true }), /Private network/);
});

test('output allocator serializes concurrent filename reservations', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'cybersnapper-naming-'));
  try {
    const allocator = new OutputPathAllocator();
    const results = await Promise.all(Array.from({ length: 20 }, () => allocator.choose(directory, 'capture', 'png', 'version')));
    assert.equal(new Set(results.map((result) => result.absolute)).size, 20);
    assert.ok(results.some((result) => result.absolute.endsWith('capture.png')));
    assert.ok(results.some((result) => result.absolute.endsWith('capture-20.png')));
  } finally { await rm(directory, { recursive: true, force: true }); }
});

test('filtering proxy enforces the localhost policy on the actual connection', async (context) => {
  const destination = http.createServer((_request, response) => response.end('reachable'));
  destination.listen(0, '127.0.0.1');
  try { await once(destination, 'listening'); }
  catch (error) {
    if (error?.code === 'EPERM') { context.skip('Local sockets are blocked by the test sandbox'); return; }
    throw error;
  }
  const target = `http://127.0.0.1:${destination.address().port}/test`;
  const requestThrough = (proxyUrl) => new Promise((resolve, reject) => {
    const proxy = new URL(proxyUrl);
    const request = http.get({ host: proxy.hostname, port: proxy.port, path: target }, (response) => {
      let body = ''; response.setEncoding('utf8'); response.on('data', (chunk) => { body += chunk; });
      response.on('end', () => resolve({ status: response.statusCode, body }));
    });
    request.on('error', reject);
  });
  const denied = await startFilteringProxy();
  const allowed = await startFilteringProxy({ allowLocalhost: true });
  try {
    assert.equal((await requestThrough(denied.url)).status, 403);
    assert.deepEqual(await requestThrough(allowed.url), { status: 200, body: 'reachable' });
  } finally {
    await denied.close(); await allowed.close(); destination.close(); await once(destination, 'close');
  }
});
