import assert from 'node:assert/strict';
import test from 'node:test';
import { isAppScreenshot, isPresentationScreenshot } from './screenshot-dimensions.mjs';

test('app screenshot validation accepts native 1x and 2x captures', () => {
  assert.equal(isAppScreenshot({ width: 1280, height: 800 }), true);
  assert.equal(isAppScreenshot({ width: 2560, height: 1600 }), true);
  assert.equal(isAppScreenshot({ width: 2560, height: 800 }), false);
  assert.equal(isAppScreenshot({ width: 640, height: 400 }), false);
  assert.equal(isAppScreenshot({}), false);
});

test('presentation validation accepts both native display scales', () => {
  assert.equal(isPresentationScreenshot({ width: 780, height: 503 }), true);
  assert.equal(isPresentationScreenshot({ width: 1560, height: 906 }), true);
  assert.equal(isPresentationScreenshot({ width: 1560, height: 1200 }), false);
  assert.equal(isPresentationScreenshot({ width: 390, height: 250 }), false);
  assert.equal(isPresentationScreenshot({}), false);
});
