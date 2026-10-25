// A launch link naming a project on a saved server opens it, but the project fields it carries
// (expiry, description, keywords, colour) stay in this editor: none is written to the server, where
// it would change the project for every collaborator (an `expire=day` would have the sweep delete it).
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/boot.js';
import { issueToken, bearer, createProject, SERVER_URL, stackEnabled } from '../../helpers/server/api.js';
import { solidPng } from '../../helpers/png.js';

test.describe('a link over a server project', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('opens it and pushes none of its project fields to the server', async ({ page, request }) => {
    const token = await issueToken(request);
    const created = await createProject(request, token, {
      name: 'shared', description: 'kept', keywords: ['kept'],
      originalContent: `data:image/png;base64,${solidPng(64, 48).toString('base64')}`,
    });
    const id = created.id ?? created.project?.id;
    const read = async () => (await (await request.get(`${SERVER_URL}/projects/${id}`, { headers: bearer(token) })).json()).project;
    const before = await read();

    await page.addInitScript((entries) => {
      if (sessionStorage.getItem('seeded')) return;
      sessionStorage.setItem('seeded', '1');
      localStorage.clear();
      for (const [k, v] of Object.entries(entries)) localStorage.setItem(k, v);
    }, { drawingApp_servers: JSON.stringify([{ url: SERVER_URL, token }]), drawingApp_autoConnectServers: '1' });
    const link = `#server=${encodeURIComponent(SERVER_URL)}&id=${id}&expire=day&description=replaced`
      + '&keywords=replaced&color=%23ff0000';
    await page.goto(APP_URL + link);
    await page.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });
    await expect.poll(() => page.evaluate(() => window.stencil.imageSize?.width ?? 0),
      { timeout: 20_000, message: 'the linked project opens' }).toBe(64);
    await page.waitForTimeout(3000);   // past every push debounce

    const after = await read();
    expect(after.expiresAt ?? 0).toBe(before.expiresAt ?? 0);
    expect(after.description).toBe('kept');
    expect(after.keywords).toEqual(['kept']);
    expect(after.color ?? '').toBe(before.color ?? '');
  });
});
