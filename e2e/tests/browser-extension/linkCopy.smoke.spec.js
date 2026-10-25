// Extension e2e — the "Copy Stencil link" clipboard route in real Chromium: the service worker
// opens src/offscreen/clipboard.html (the `offscreen` permission, reason CLIPBOARD) and asks it to
// copy, and a page reads the very text back. The link itself is unit-pinned in the extension suite.
import { test, expect } from '@playwright/test';
import { SITE_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';

test.describe('extension: copy Stencil link', () => {
  test('the offscreen page puts the link on the system clipboard', async () => {
    const { context, background } = await launchExtension();
    try {
      await context.grantPermissions(['clipboard-read', 'clipboard-write'], { origin: new URL(SITE_URL).origin });
      const sw = await background();
      const link = 'https://andrew1407.github.io/stencil/?source=https%3A%2F%2Fexample.com%2Fp.png&incognito=true';
      const answer = await sw.evaluate(async (text) => {
        const url = chrome.runtime.getURL('src/offscreen/clipboard.html');
        if (!(await chrome.runtime.getContexts({ contextTypes: ['OFFSCREEN_DOCUMENT'], documentUrls: [url] })).length) {
          await chrome.offscreen.createDocument({ url: 'src/offscreen/clipboard.html', reasons: ['CLIPBOARD'], justification: 'e2e' });
        }
        return chrome.runtime.sendMessage({ type: 'stencil-offscreen-copy', text });
      }, link);
      expect(answer).toEqual({ ok: true });
      const page = await context.newPage();
      await page.goto(`${SITE_URL}__e2e__/page-with-image.html`);
      await page.bringToFront();
      await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe(link);
    } finally {
      await context.close();
    }
  });
});
