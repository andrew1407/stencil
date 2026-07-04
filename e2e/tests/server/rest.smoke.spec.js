// Server REST smoke: black-box the running Go binary (docker-compose) over its HTTP
// contract (server/internal/protocol). Covers auth gating, the project lifecycle,
// the LWW version guard, file byte round-trips, and strict-JSON rejection.
import { test, expect } from '@playwright/test';
import { issueToken, createProject, bearer, SERVER_URL, stackEnabled } from '../../helpers/serverApi.js';

test.describe('server REST', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('rejects unauthenticated project access', async ({ request }) => {
    const res = await request.get(`${SERVER_URL}/projects`);
    expect(res.status()).toBe(401);
    expect((await res.json()).code).toBe('unauthorized');
  });

  test('project create → get → conflicting update → delete', async ({ request }) => {
    const token = await issueToken(request);

    const created = await createProject(request, token, { name: 'rest-smoke', imageW: 10, imageH: 20, hasImage: true });
    expect(created.id).toMatch(/^p_/);
    expect(created.version).toBe(0);

    // Appears in the list.
    const list = await request.get(`${SERVER_URL}/projects`, { headers: bearer(token) });
    expect((await list.json()).projects.map((p) => p.id)).toContain(created.id);

    // Fetch the single project.
    const one = await request.get(`${SERVER_URL}/projects/${created.id}`, { headers: bearer(token) });
    expect(one.ok()).toBeTruthy();
    expect((await one.json()).project.id).toBe(created.id);

    // Valid update bumps the version.
    const upd = await request.put(`${SERVER_URL}/projects/${created.id}`, {
      headers: bearer(token), data: { name: 'renamed', version: 0 },
    });
    expect(upd.ok()).toBeTruthy();
    expect((await upd.json()).version).toBe(1);

    // Stale version → 409 conflict (LWW guard).
    const stale = await request.put(`${SERVER_URL}/projects/${created.id}`, {
      headers: bearer(token), data: { name: 'again', version: 0 },
    });
    expect(stale.status()).toBe(409);
    expect((await stale.json()).code).toBe('conflict');

    // Delete → 204, then gone.
    const del = await request.delete(`${SERVER_URL}/projects/${created.id}`, { headers: bearer(token) });
    expect(del.status()).toBe(204);
    const gone = await request.get(`${SERVER_URL}/projects/${created.id}`, { headers: bearer(token) });
    expect(gone.status()).toBe(404);
  });

  test('file upload → download round-trips the bytes', async ({ request }) => {
    const token = await issueToken(request);
    const project = await createProject(request, token, { name: 'files-smoke' });
    const bytes = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 1, 2, 3]);

    const up = await request.post(`${SERVER_URL}/projects/${project.id}/files/original?ext=png&w=2&h=2`, {
      headers: { ...bearer(token), 'Content-Type': 'application/octet-stream' },
      data: bytes,
    });
    expect(up.status()).toBe(201);
    const meta = await up.json();
    expect(meta).toMatchObject({ w: 2, h: 2 });

    const down = await request.get(`${SERVER_URL}/projects/${project.id}/files/original`, { headers: bearer(token) });
    expect(down.ok()).toBeTruthy();
    expect(Buffer.from(await down.body()).equals(bytes)).toBeTruthy();
  });

  test('strict JSON rejects unknown fields', async ({ request }) => {
    const token = await issueToken(request);
    const res = await request.post(`${SERVER_URL}/projects`, {
      headers: bearer(token), data: { name: 'x', bogusField: true },
    });
    expect(res.status()).toBe(400);
  });
});
