const SCROLLBAR_HIDE_CSS = `
  ::-webkit-scrollbar { display: none !important; }
  * { scrollbar-width: none !important; }
`;

async function scrollThrough(page, scrollDelay, finalDelay) {
  await page.evaluate(async ({ sd, fd }) => {
    const wait = ms => new Promise(r => setTimeout(r, ms));
    const step = window.innerHeight;
    let pos = 0;
    while (pos < document.body.scrollHeight) {
      window.scrollBy(0, step);
      pos += step;
      await wait(sd);
    }
    window.scrollTo(0, 0);
    await wait(fd);
  }, { sd: scrollDelay, fd: finalDelay });
}

async function waitForContent(page, { waitForSelector, initialDelay, timeout = 30000 }) {
  if (waitForSelector) {
    try {
      await page.waitForSelector(waitForSelector, { timeout });
      return true;
    } catch {
      return false;
    }
  }
  await page.waitForTimeout(initialDelay);
  return true;
}

module.exports = { SCROLLBAR_HIDE_CSS, scrollThrough, waitForContent };
