// Full-stack co-edit of the view: a peer crops, then turns, the picture client A has open. The
// server's original is unchanged (same originalHash), so A re-derives each view from the original
// it holds, as one undo step apiece — the original is never downloaded again, and undo walks back
// through the peer's crop and turn into A's own strokes.
import { test, expect } from '@playwright/test';
import { gotoApp, serverProjectIds, waitForNewServerProjectId } from '../../helpers/boot.js';
import { issueToken, bearer, SERVER_URL, stackEnabled } from '../../helpers/server/api.js';
import { solidPng } from '../../helpers/png.js';

test.describe('co-edit geometry', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('a peer\'s crop and turn land in place: no original re-download, undo history kept', async ({ page, request }) => {
    test.slow();
    const token = await issueToken(request);
    await gotoApp(page);
    await page.evaluate(async ({ url, token }) => { await window.stencil.connect({ url, token }); }, { url: SERVER_URL, token });
    const baseline = new Set(await serverProjectIds(page));
    const dataUrl = `data:image/png;base64,${solidPng(320, 240).toString('base64')}`;
    await page.evaluate(async ({ url, dataUrl }) => {
      await window.stencil.load(dataUrl, { address: url, name: 'geometry.png' });
    }, { url: SERVER_URL, dataUrl });
    const projectId = await waitForNewServerProjectId(page, baseline);
    expect(projectId, 'A\'s linked project should exist on the server').toBeTruthy();
    await page.waitForTimeout(3000);   // the creation's own writes settle

    // Two strokes of A's own, each an undo step, pushed to the server.
    await page.evaluate(async () => {
      for (const y of [40, 80]) {
        window.stencil.setLines([{ points: [{ x: 10, y }, { x: 200, y }], color: '#ff0000' }], { mode: 'combine' });
        await new Promise((r) => setTimeout(r, 60));
      }
    });
    const current = async () =>
      (await (await request.get(`${SERVER_URL}/projects/${projectId}`, { headers: bearer(token) })).json());
    await expect.poll(async () => (await current()).layout?.lines?.length ?? 0,
      { timeout: 15_000, message: 'A\'s strokes reach the server' }).toBe(2);
    await page.waitForTimeout(2500);   // past the echo window of A's own push
    const before = await page.evaluate(() => window.stencil.cropRect);

    let originals = 0;
    page.on('request', (req) => {
      if (req.method() === 'GET' && req.url().startsWith(SERVER_URL)
          && new URL(req.url()).pathname.endsWith(`/projects/${projectId}/files/original`)) originals++;
    });
    const peerPut = async (over) => {
      const cur = await current();
      const put = await request.put(`${SERVER_URL}/projects/${projectId}`, {
        headers: bearer(token), data: { layout: { ...cur.layout, ...over }, version: cur.project.version },
      });
      expect(put.ok(), 'the peer\'s layout write').toBeTruthy();
    };

    // The peer crops the picture…
    await peerPut({ cropRect: { x: 20, y: 10, w: 160, h: 120 } });
    await expect.poll(() => page.evaluate(() => window.stencil.imageSize),
      { timeout: 15_000, message: 'A shows the peer\'s crop' }).toEqual({ width: 160, height: 120 });
    // …then turns it a quarter, the crop now in the turned original's space.
    await page.waitForTimeout(500);
    await peerPut({ rotationQuarters: 1, cropRect: { x: 0, y: 40, w: 120, h: 200 } });
    await expect.poll(() => page.evaluate(() => window.stencil.imageSize),
      { timeout: 15_000, message: 'A shows the peer\'s turn' }).toEqual({ width: 120, height: 200 });
    expect(originals, 'neither view re-downloads the original').toBe(0);

    // Undo walks back through the peer's turn and crop, then into A's own second stroke.
    const walk = await page.evaluate(() => {
      const seen = [];
      for (let i = 0; i < 3; i++) {
        window.stencil.undo();
        seen.push({ size: window.stencil.imageSize, lines: window.stencil.lines.length });
      }
      return seen;
    });
    expect(walk[0], 'the turn is undone').toEqual({ size: { width: 160, height: 120 }, lines: 2 });
    expect(walk[1], 'the crop is undone').toEqual({ size: { width: before.w, height: before.h }, lines: 2 });
    expect(walk[2], 'A\'s own history survived the peer\'s views').toEqual({ size: { width: before.w, height: before.h }, lines: 1 });
    expect(originals, 'undoing a view re-derives it from the original on screen').toBe(0);
  });
});
