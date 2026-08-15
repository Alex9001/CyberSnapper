const { test, expect } = require('playwright/test');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sourceRoot = path.join(root, 'docs', 'fixtures');
const captures = [
  { name: 'light desktop', file: 'cyberbrand-light-desktop.png', colorScheme: 'light', viewport: { width: 1440, height: 900 } },
  { name: 'dark desktop', file: 'cyberbrand-dark-desktop.png', colorScheme: 'dark', viewport: { width: 1440, height: 900 } },
  { name: 'light tablet', file: 'cyberbrand-light-tablet.png', colorScheme: 'light', viewport: { width: 768, height: 1024 } },
  { name: 'dark tablet', file: 'cyberbrand-dark-tablet.png', colorScheme: 'dark', viewport: { width: 768, height: 1024 } },
  { name: 'light mobile', file: 'cyberbrand-light-mobile.png', colorScheme: 'light', viewport: { width: 390, height: 844 } },
  { name: 'dark mobile', file: 'cyberbrand-dark-mobile.png', colorScheme: 'dark', viewport: { width: 390, height: 844 } },
];

for (const capture of captures) {
  test.describe(capture.name, () => {
    test.use({
      colorScheme: capture.colorScheme,
      reducedMotion: 'reduce',
      serviceWorkers: 'block',
      viewport: capture.viewport,
    });

    test(`capture ${capture.name} source`, async ({ context, page }) => {
      await context.addInitScript(({ colorScheme }) => {
        localStorage.setItem('cb-theme', colorScheme);
        localStorage.setItem('hud-motion-preference', 'paused');
      }, { colorScheme: capture.colorScheme });
      await page.goto('https://cyberbrand.net/', { waitUntil: 'networkidle' });
      await page.evaluate(() => document.fonts.ready);
      await expect(page).toHaveTitle(/CYBER BRAND/i);
      await expect(page.locator('body')).toContainText('Built for speed. Engineered for scale.');
      await expect.poll(() => page.evaluate(() => document.documentElement.dataset.theme)).toBe(capture.colorScheme);
      await page.addStyleTag({
        content: '*,*::before,*::after{animation:none!important;transition:none!important;caret-color:transparent!important}',
      });
      await page.screenshot({
        path: path.join(sourceRoot, capture.file),
        animations: 'disabled',
      });
    });
  });
}
