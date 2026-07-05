// Fragment-privacy invariant: the handed-off image (delivered via the #stencil=
// URL *fragment*, DrawingApp.applyExternalLaunch) is local content and must NEVER
// leave the machine. A URL fragment is not sent to a server by browsers, but the
// app also must not itself forward the payload anywhere — so we capture EVERY
// network request the page makes and assert none carries the fragment marker in its
// URL or its POST body. This is the headline "nothing local goes outward" guard.
import { test, expect } from '@playwright/test';
import { gotoApp, PNG_DATA_URL } from '../../helpers/boot.js';

test('the #stencil= fragment payload never appears in any outgoing request', async ({ page }) => {
  // A distinctive marker carried inside the handoff payload (its image name). If any
  // request URL or body contains it, local content escaped the page.
  const MARKER = 'stencil-privacy-marker-DO-NOT-LEAK-7f3a9c';
  const payload = encodeURIComponent(JSON.stringify({ dataUrl: PNG_DATA_URL, name: `${MARKER}.png` }));

  // Record every request BEFORE navigating so the deep-link load is fully observed.
  const seen = [];
  page.on('request', (req) => {
    seen.push({ url: req.url(), method: req.method(), body: req.postData() || '' });
  });

  await gotoApp(page, { hash: `#stencil=${payload}` });

  // Readiness: applyExternalLaunch has both loaded the image (imageSize set) and
  // stripped the fragment from the URL. Avoid networkidle — the app may hold a live
  // connection, which would never idle.
  await page.waitForFunction(
    () => !!(window.stencil && window.stencil.imageSize) && location.hash === '',
    null,
    { timeout: 15_000 },
  );

  // Give any (mis)behaving forwarder a beat to fire.
  await page.waitForTimeout(750);

  // No captured request may carry the marker OR the raw encoded fragment, in its URL or body.
  const leaks = seen.filter((r) =>
    r.url.includes(MARKER) || r.url.includes(payload) ||
    r.body.includes(MARKER) || r.body.includes(payload));
  expect(leaks, `local handoff payload leaked into requests: ${JSON.stringify(leaks)}`).toEqual([]);

  // Belt-and-braces: the fragment must also be stripped from the address bar after consumption.
  expect(new URL(page.url()).hash).toBe('');
});
