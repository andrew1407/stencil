// Full-stack co-edit under load: client A makes fifty edits on a server-linked project while a
// second client edits it too — a REST writer, then a second browser trading edits with A.
// A pushes its layout at the debounce but renders and uploads the result only when the editing
// goes quiet, and adopts the peer's layout in place: the original is not downloaded again and
// A's undo history survives the peer's edit. A peer that replaces the original with a same-size
// image is told apart by the server's originalHash alone, and A reloads the picture.
import { test, expect } from '@playwright/test';
import { APP_URL, gotoApp, serverProjectIds, waitForNewServerProjectId } from '../../helpers/boot.js';
import { issueToken, bearer, SERVER_URL, stackEnabled } from '../../helpers/server/api.js';
import { solidPng } from '../../helpers/png.js';

const EDITS = 50;

test.describe('co-edit load', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('fifty edits: bounded result uploads, no history reset, no original re-download', async ({ page, request }) => {
    test.slow();
    const token = await issueToken(request);
    await gotoApp(page);
    await page.evaluate(async ({ url, token }) => { await window.stencil.connect({ url, token }); }, { url: SERVER_URL, token });
    const baseline = new Set(await serverProjectIds(page));
    const dataUrl = `data:image/png;base64,${solidPng(320, 240).toString('base64')}`;
    await page.evaluate(async ({ url, dataUrl }) => {
      await window.stencil.load(dataUrl, { address: url, name: 'coedit.png' });
    }, { url: SERVER_URL, dataUrl });
    const projectId = await waitForNewServerProjectId(page, baseline);
    expect(projectId, 'A\'s linked project should exist on the server').toBeTruthy();
    await page.waitForTimeout(3000);   // the creation's own writes settle

    const writes = { layout: 0, result: 0, original: 0 };
    page.on('request', (req) => {
      const url = new URL(req.url());
      if (!req.url().startsWith(SERVER_URL) || !url.pathname.includes(projectId)) return;
      if (req.method() === 'PUT' && url.pathname.endsWith(`/projects/${projectId}`)) writes.layout++;
      if (req.method() === 'POST' && url.pathname.endsWith('/files/result')) writes.result++;
      if (req.method() === 'GET' && url.pathname.endsWith('/files/original')) writes.original++;
    });

    // Fifty edits in quick succession, each its own undo step.
    await page.evaluate(async (n) => {
      for (let i = 0; i < n; i++) {
        window.stencil.setLines([{ points: [{ x: 10, y: 4 + i * 4 }, { x: 300, y: 4 + i * 4 }], color: '#ff0000' }],
          { mode: 'combine' });
        await new Promise((r) => setTimeout(r, 40));
      }
    }, EDITS);

    const serverLines = async () => {
      const r = await request.get(`${SERVER_URL}/projects/${projectId}`, { headers: bearer(token) });
      return (await r.json()).layout?.lines?.length ?? 0;
    };
    await expect.poll(serverLines, { timeout: 15_000, message: 'A\'s fifty lines reach the server' }).toBe(EDITS);
    await expect.poll(() => writes.result, { timeout: 15_000, message: 'the result follows once idle' }).toBeGreaterThan(0);
    await page.waitForTimeout(2500);
    expect(writes.layout, 'the layout rides the debounce, not every edit').toBeLessThan(EDITS);
    expect(writes.result, 'the full-resolution result is not re-encoded per push').toBeLessThanOrEqual(2);

    // The peer adds one line on the server.
    const cur = await (await request.get(`${SERVER_URL}/projects/${projectId}`, { headers: bearer(token) })).json();
    const layout = { ...cur.layout, lines: [...cur.layout.lines, { points: [{ x: 5, y: 230 }, { x: 60, y: 230 }], color: '#00ff00' }] };
    const put = await request.put(`${SERVER_URL}/projects/${projectId}`, {
      headers: bearer(token), data: { layout, version: cur.project.version },
    });
    expect(put.ok()).toBeTruthy();
    await expect.poll(() => page.evaluate(() => window.stencil.lines.length),
      { timeout: 15_000, message: 'A adopts the peer\'s line' }).toBe(EDITS + 1);
    expect(writes.original, 'the peer\'s layout edit never re-downloads the original').toBe(0);

    // A's history survived: undo steps back through the peer's edit and then A's own.
    const afterUndo = await page.evaluate(() => {
      window.stencil.undo();
      const one = window.stencil.lines.length;
      window.stencil.undo();
      return [one, window.stencil.lines.length];
    });
    expect(afterUndo, 'the undo history was kept across the peer event').toEqual([EDITS, EDITS - 1]);
  });

  test('two browsers trade fifty edits: bounded uploads, no history reset', async ({ browser, request }) => {
    test.slow();
    const token = await issueToken(request);
    const [ctxA, ctxB] = [await browser.newContext(), await browser.newContext()];
    const pageA = await gotoApp(await ctxA.newPage());
    const joinB = await gotoApp(await ctxB.newPage());
    for (const p of [pageA, joinB])
      await p.evaluate(async ({ url, token }) => { await window.stencil.connect({ url, token }); }, { url: SERVER_URL, token });
    const baseline = new Set(await serverProjectIds(pageA));
    const dataUrl = `data:image/png;base64,${solidPng(320, 240).toString('base64')}`;
    await pageA.evaluate(async ({ url, dataUrl }) => {
      await window.stencil.load(dataUrl, { address: url, name: 'twobrowsers.png' });
    }, { url: SERVER_URL, dataUrl });
    const projectId = await waitForNewServerProjectId(pageA, baseline);
    expect(projectId, 'A\'s linked project should exist on the server').toBeTruthy();
    await pageA.waitForTimeout(3000);   // the creation's own writes settle

    // B opens the shared project from a link, on a fresh page that keeps B's saved connection.
    await joinB.close();
    const pageB = await ctxB.newPage();
    const link = encodeURIComponent(JSON.stringify({ server: { url: SERVER_URL, id: projectId } }));
    await pageB.goto(`${APP_URL}#stencil=${link}`);
    await pageB.waitForFunction(() => window.stencil?.imageSize?.width === 320, null, { timeout: 15_000 });

    const counts = { A: { result: 0, original: 0 }, B: { result: 0, original: 0 } };
    for (const [who, p] of [['A', pageA], ['B', pageB]]) {
      p.on('request', (req) => {
        const url = new URL(req.url());
        if (!req.url().startsWith(SERVER_URL) || !url.pathname.includes(projectId)) return;
        if (req.method() === 'POST' && url.pathname.endsWith('/files/result')) counts[who].result++;
        if (req.method() === 'GET' && url.pathname.endsWith('/files/original')) counts[who].original++;
      });
    }
    const draw = (p, from, n) => p.evaluate(async ({ from, n }) => {
      for (let i = from; i < from + n; i++) {
        window.stencil.setLines([{ points: [{ x: 10, y: 4 + i * 4 }, { x: 300, y: 4 + i * 4 }], color: '#ff0000' }],
          { mode: 'combine' });
        await new Promise((r) => setTimeout(r, 40));
      }
    }, { from, n });
    const lineCount = (p) => p.evaluate(() => window.stencil.lines.length);
    const half = EDITS / 2;

    await draw(pageA, 0, half);
    await expect.poll(() => lineCount(pageB), { timeout: 20_000, message: 'B adopts A\'s edits' }).toBe(half);
    await draw(pageB, half, half);
    await expect.poll(() => lineCount(pageA), { timeout: 20_000, message: 'A adopts B\'s edits' }).toBe(EDITS);
    await expect.poll(() => counts.A.result + counts.B.result, { timeout: 15_000, message: 'a result follows once idle' })
      .toBeGreaterThan(0);
    await pageA.waitForTimeout(2500);

    expect(counts.A.original, 'a peer layout edit never re-downloads A\'s original').toBe(0);
    expect(counts.B.original, 'B downloads the original only when it opens the project').toBeLessThanOrEqual(1);
    expect(counts.A.result + counts.B.result, 'each burst renders the result once, not per push').toBeLessThanOrEqual(4);
    // A adopts each of B's debounced pushes as one undo step; undo walks back through them into A's own.
    const walkA = await pageA.evaluate((half) => {
      const seen = [window.stencil.lines.length];
      for (let i = 0; i < 6 && window.stencil.lines.length >= half; i++) { window.stencil.undo(); seen.push(window.stencil.lines.length); }
      return seen;
    }, half);
    expect(walkA.at(-1), `A's undo history survived B's edits (${walkA})`).toBe(half - 1);
    const undoneB = await pageB.evaluate(() => { window.stencil.undo(); return window.stencil.lines.length; });
    expect(undoneB, 'B\'s undo history survived A\'s edits').toBe(EDITS - 1);
    await ctxA.close();
    await ctxB.close();
  });

  test('a same-size replaced original reloads the picture', async ({ page, request }) => {
    test.slow();
    const token = await issueToken(request);
    const project = async () =>
      (await (await request.get(`${SERVER_URL}/projects/${projectId}`, { headers: bearer(token) })).json()).project;
    await gotoApp(page);
    await page.evaluate(async ({ url, token }) => { await window.stencil.connect({ url, token }); }, { url: SERVER_URL, token });
    const baseline = new Set(await serverProjectIds(page));
    const blue = `data:image/png;base64,${solidPng(320, 240, [0x20, 0x40, 0xe0]).toString('base64')}`;
    await page.evaluate(async ({ url, dataUrl }) => {
      await window.stencil.load(dataUrl, { address: url, name: 'replaced.png' });
    }, { url: SERVER_URL, dataUrl: blue });
    const projectId = await waitForNewServerProjectId(page, baseline);
    expect(projectId, 'A\'s linked project should exist on the server').toBeTruthy();
    await page.waitForTimeout(3000);   // the creation's own writes settle

    const centre = () => page.evaluate(() => {
      const c = document.getElementById('canvas');
      const [r, , b] = c.getContext('2d').getImageData(c.width >> 1, c.height >> 1, 1, 1).data;
      return r > b ? 'red' : 'blue';
    });
    await expect.poll(centre, { timeout: 10_000, message: 'A shows its own blue picture' }).toBe('blue');
    let originals = 0;
    page.on('request', (req) => {
      if (req.method() === 'GET' && req.url().startsWith(SERVER_URL)
          && new URL(req.url()).pathname.endsWith(`/projects/${projectId}/files/original`)) originals++;
    });
    const before = await project();
    expect(before.originalHash, 'the server names the original by its hash').toMatch(/^[0-9a-f]{64}$/);

    // The peer: the same size, type and stored path, other pixels.
    const up = await request.post(`${SERVER_URL}/projects/${projectId}/files/original?ext=png&w=320&h=240`, {
      headers: { ...bearer(token), 'Content-Type': 'application/octet-stream' },
      data: solidPng(320, 240, [0xe0, 0x30, 0x20]),
    });
    expect(up.status()).toBe(201);
    const after = await project();
    expect([after.imageW, after.imageH, after.originalPath]).toEqual([before.imageW, before.imageH, before.originalPath]);
    expect(after.originalHash, 'only the hash tells the pictures apart').not.toBe(before.originalHash);

    await expect.poll(() => originals, { timeout: 15_000, message: 'A downloads the replaced original' }).toBeGreaterThan(0);
    await expect.poll(centre, { timeout: 15_000, message: 'A shows the peer\'s picture, not its own' }).toBe('red');
  });
});
