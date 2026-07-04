// Full-stack collaboration smoke: the real browser app + the real Go server (brought
// up via docker-compose). Two independent clients connect to the same server; client A
// creates and saves a server-linked project, and client B — a separate browser context —
// observes it appear in the shared server state. Exercises connect → create → save →
// cross-client visibility end to end.
import { test, expect } from '@playwright/test';
import { gotoApp, serverProjectIds, waitForNewServerProjectId } from '../../helpers/boot.js';
import { issueToken, SERVER_URL, stackEnabled } from '../../helpers/serverApi.js';

test.describe('collaboration', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('a project created by one client is visible to another', async ({ browser, request }) => {
    test.slow();
    const token = await issueToken(request);

    const ctxA = await browser.newContext();
    const ctxB = await browser.newContext();
    const pageA = await ctxA.newPage();
    const pageB = await ctxB.newPage();
    await gotoApp(pageA);
    await gotoApp(pageB);

    // Both clients connect to the same collaboration server.
    for (const p of [pageA, pageB]) {
      await p.evaluate(async ({ url, token }) => { await window.stencil.connect({ url, token }); }, { url: SERVER_URL, token });
      const conns = await p.evaluate(() => window.stencil.connections);
      expect(conns.some((u) => u.includes('8090'))).toBeTruthy();
    }

    // Baseline of server-side project ids (both clients see the same shared set).
    const baseline = new Set(await serverProjectIds(pageA));

    // Client A creates a server-linked project, draws on a blank canvas, and saves.
    await pageA.evaluate(async ({ url }) => {
      await window.stencil.newEditor({ address: url });     // creates + links a remote project
      await window.stencil.blank('#ffffff', { size: { width: 320, height: 240 } });
      await window.stencil.save();                          // writes layout + result back to the server
    }, { url: SERVER_URL });

    // The new project's SERVER id — the one B will also see (A's local current.id can differ).
    const newId = await waitForNewServerProjectId(pageA, baseline);
    expect(newId, 'A\'s create should land on the server').toBeTruthy();

    // Client B sees the same new project in the server's shared state (aggregated over REST).
    await expect.poll(
      () => serverProjectIds(pageB),
      { timeout: 15_000, message: 'client B should see A\'s server project' },
    ).toContain(newId);

    await ctxA.close();
    await ctxB.close();
  });
});
