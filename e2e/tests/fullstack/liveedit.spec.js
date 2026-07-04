// Full-stack LIVE update: the collaboration mechanism the smoke visibility test doesn't
// reach. Client A opens a server-linked project; a *peer* edits that project on the
// server; A receives the project-event over its live feed and auto-reloads
// (DrawingApp.onServerProjectEvent → reloadRemoteActive), so A's canvas reflects the
// peer's change without any user action. The "peer" here is a REST writer, which drives
// the exact same server event path a second browser would.
import { test, expect } from '@playwright/test';
import { gotoApp, PNG_DATA_URL, serverProjectIds, waitForNewServerProjectId } from '../../helpers/boot.js';
import { issueToken, bearer, SERVER_URL, stackEnabled } from '../../helpers/serverApi.js';

test.describe('live update propagation', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('A auto-reloads a peer\'s server-side edit', async ({ page, request }) => {
    test.slow();
    const token = await issueToken(request);
    await gotoApp(page);
    await page.evaluate(async ({ url, token }) => { await window.stencil.connect({ url, token }); }, { url: SERVER_URL, token });

    // Baseline of server ids, then A opens a server-LINKED project (uploads the original
    // image so the live reload has bytes to re-fetch). It starts with zero lines.
    const baseline = new Set(await serverProjectIds(page));
    const size = await page.evaluate(async ({ url, dataUrl }) => {
      await window.stencil.load(dataUrl, { address: url, name: 'live.png' });
      return window.stencil.imageSize;
    }, { url: SERVER_URL, dataUrl: PNG_DATA_URL });
    expect(await page.evaluate(() => window.stencil.lines.length)).toBe(0);

    // The project's server id (A's local current.id can differ from the server id).
    const projectId = await waitForNewServerProjectId(page, baseline);
    expect(projectId, 'A\'s linked project should exist on the server').toBeTruthy();

    // A peer edits the project on the server: replace the layout with TWO lines, guarded
    // by the current version. This publishes a project-event 'updated' on A's live feed.
    await page.waitForTimeout(300); // clear the brief self-echo window
    const cur = await (await request.get(`${SERVER_URL}/projects/${projectId}`, { headers: bearer(token) })).json();
    const peerLayout = {
      imageWidth: size.width, imageHeight: size.height,
      lines: [
        { points: [{ x: 1, y: 1 }, { x: 5, y: 5 }], color: '#ff0000' },
        { points: [{ x: 2, y: 2 }, { x: 6, y: 6 }], color: '#00ff00' },
      ],
    };
    const put = await request.put(`${SERVER_URL}/projects/${projectId}`, {
      headers: bearer(token), data: { layout: peerLayout, version: cur.project.version },
    });
    expect(put.ok()).toBeTruthy();

    // A pulls the peer's change live — its canvas now shows the peer's two lines,
    // with no local action taken.
    await expect.poll(
      async () => page.evaluate(() => window.stencil.lines.length),
      { timeout: 15_000, message: 'A should auto-reload the peer\'s 2-line layout' },
    ).toBe(2);
  });
});
