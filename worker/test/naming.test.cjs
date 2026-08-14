const test = require('node:test');
const assert = require('node:assert/strict');
const { assertPublicUrl, captureBaseName, captureName, safeSegment } = require('../dist/testing.cjs');

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
