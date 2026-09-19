// Fragment-privacy invariant: the handed-off image (the #stencil= URL fragment,
// DrawingApp.applyExternalLaunch) is local content and must NEVER leave the machine. Browsers
// do not send a fragment, and the app must not forward the payload either — so every network
// request the page makes is captured and asserted free of the fragment sentinel, URL and body.
import { test, expect } from '@playwright/test';
import { gotoApp, PNG_DATA_URL } from '../../helpers/boot.js';

test('the #stencil= fragment payload never appears in any outgoing request', async ({ page }) => {
  // A distinctive sentinel carried inside the handoff payload (its image name). If any
  // request URL or body contains it, local content escaped the page.
  const SENTINEL = 'stencil-privacy-sentinel-DO-NOT-LEAK-7f3a9c';
  const payload = encodeURIComponent(JSON.stringify({ dataUrl: PNG_DATA_URL, name: `${SENTINEL}.png` }));

  // Record every request BEFORE navigating so the deep-link load is fully observed.
  const seen = [];
  page.on('request', (req) => {
    seen.push({ url: req.url(), method: req.method(), body: req.postData() || '' });
  });

  await gotoApp(page, { hash: `#stencil=${payload}` });

  // Ready once applyExternalLaunch has loaded the image and stripped the fragment. Avoid
  // networkidle — the app may hold a live connection, which would never idle.
  await page.waitForFunction(
    () => !!(window.stencil && window.stencil.imageSize) && location.hash === '',
    null,
    { timeout: 15_000 },
  );

  // Give any (mis)behaving forwarder a beat to fire.
  await page.waitForTimeout(750);

  // No captured request may carry the sentinel OR the raw encoded fragment, in its URL or body.
  const leaks = seen.filter((r) =>
    r.url.includes(SENTINEL) || r.url.includes(payload) ||
    r.body.includes(SENTINEL) || r.body.includes(payload));
  expect(leaks, `local handoff payload leaked into requests: ${JSON.stringify(leaks)}`).toEqual([]);

  // Belt-and-braces: the fragment must also be stripped from the address bar after consumption.
  expect(new URL(page.url()).hash).toBe('');
});
