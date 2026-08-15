const test = require('node:test');
const assert = require('node:assert/strict');
const sharp = require('sharp');
const { normalizePresentation, planPresentation, renderPresentation } = require('../dist/testing.cjs');

const base = {
  enabled: true,
  scene: 'aurora',
  frame: 'auto',
  aspect: 'auto',
  padding: 'balanced',
  shadow: 'soft',
  solidColor: '#0B1220',
};

test('presentation settings reject malformed profile input', () => {
  assert.deepEqual(normalizePresentation({
    enabled: true, scene: 'unknown', frame: 'laptop', aspect: 'wide', padding: 'huge', shadow: 'fog',
    solidColor: 'red',
  }), base);
  assert.equal(normalizePresentation({ ...base, solidColor: '#ab12ef' }).solidColor, '#AB12EF');
  assert.equal(normalizePresentation({ ...base, frame: 'lightTablet' }).frame, 'lightTablet');
  assert.equal(normalizePresentation({ ...base, frame: 'darkTablet' }).frame, 'darkTablet');
});

test('presentation planning never crops, upscales, or exceeds image safety limits', () => {
  for (const aspect of ['auto', '16:9', '4:3', 'square']) {
    const plan = planPresentation(24000, 12000, { ...base, aspect },
      { mobile: false, width: 24000, height: 12000 }, 'viewport');
    assert.ok(plan.screenshotWidth <= 24000);
    assert.ok(plan.screenshotHeight <= 12000);
    assert.ok(plan.canvasWidth <= 16384);
    assert.ok(plan.canvasHeight <= 16384);
    assert.ok(plan.canvasWidth * plan.canvasHeight <= 64_000_000);
    assert.ok(plan.screenshotX >= 0 && plan.screenshotY >= 0);
    assert.ok(plan.screenshotX + plan.screenshotWidth <= plan.canvasWidth);
    assert.ok(plan.screenshotY + plan.screenshotHeight <= plan.canvasHeight);
  }
  const widescreen = planPresentation(640, 900, { ...base, aspect: '16:9' },
    { mobile: false, width: 640, height: 900 }, 'viewport');
  assert.ok(Math.abs(widescreen.canvasWidth / widescreen.canvasHeight - 16 / 9) < 0.002);
});

test('auto framing follows capture intent', () => {
  assert.equal(planPresentation(390, 844, base,
    { mobile: true, width: 390, height: 844 }, 'viewport').resolvedFrame, 'darkPhone');
  assert.equal(planPresentation(768, 1024, base,
    { mobile: true, width: 768, height: 1024 }, 'viewport').resolvedFrame, 'darkTablet');
  assert.equal(planPresentation(1024, 768, base,
    { mobile: true, width: 1024, height: 768 }, 'viewport').resolvedFrame, 'darkTablet');
  assert.equal(planPresentation(1440, 900, base,
    { mobile: false, width: 1440, height: 900 }, 'viewport').resolvedFrame, 'lightBrowser');
  assert.equal(planPresentation(768, 3000, base,
    { mobile: true, width: 768, height: 1024 }, 'fullPage').resolvedFrame, 'roundedCard');
  assert.equal(planPresentation(600, 400, base,
    { mobile: true, width: 768, height: 1024 }, 'element').resolvedFrame, 'roundedCard');
});

test('every built-in scene and frame renders a valid deterministic canvas', async () => {
  const source = await sharp({ create: { width: 120, height: 80, channels: 4,
    background: { r: 20, g: 220, b: 100, alpha: 1 } } }).png().toBuffer();
  const scenes = ['clean', 'aurora', 'sunset', 'midnight', 'graphite', 'customSolid'];
  const frames = [
    'auto', 'none', 'roundedCard', 'lightBrowser', 'darkBrowser',
    'lightTablet', 'darkTablet', 'lightPhone', 'darkPhone',
  ];
  for (const scene of scenes) {
    for (const frame of frames) {
      const result = await renderPresentation(source, { ...base, scene, frame, aspect: '4:3' },
        { mobile: false, width: 120, height: 80 }, 'viewport');
      const metadata = await sharp(result.bytes).metadata();
      assert.equal(metadata.format, 'png');
      assert.equal(metadata.width, result.width);
      assert.equal(metadata.height, result.height);
      assert.ok(Math.abs(result.width / result.height - 4 / 3) < 0.01);
    }
  }
  const left = await renderPresentation(source, base,
    { mobile: false, width: 120, height: 80 }, 'viewport');
  const right = await renderPresentation(source, base,
    { mobile: false, width: 120, height: 80 }, 'viewport');
  assert.deepEqual(left.bytes, right.bytes);
});
