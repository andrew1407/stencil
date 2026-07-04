// Server file endpoint depth: error paths and the `result` kind the smoke round-trip
// (original only) doesn't cover — server/internal/httpapi/files.go.
import { test, expect } from '@playwright/test';
import { issueToken, createProject, bearer, SERVER_URL, stackEnabled } from '../../helpers/serverApi.js';

test.describe('server file endpoints', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('rejects an unknown file kind', async ({ request }) => {
    const token = await issueToken(request);
    const p = await createProject(request, token, { name: 'badkind' });
    const res = await request.get(`${SERVER_URL}/projects/${p.id}/files/nonsense`, { headers: bearer(token) });
    expect(res.status()).toBe(400);
  });

  test('404s when a file kind has not been written', async ({ request }) => {
    const token = await issueToken(request);
    const p = await createProject(request, token, { name: 'nofile' });
    const res = await request.get(`${SERVER_URL}/projects/${p.id}/files/result`, { headers: bearer(token) });
    expect(res.status()).toBe(404);
  });

  test('rejects an empty upload body', async ({ request }) => {
    const token = await issueToken(request);
    const p = await createProject(request, token, { name: 'empty' });
    const res = await request.post(`${SERVER_URL}/projects/${p.id}/files/original?ext=png&w=1&h=1`, {
      headers: { ...bearer(token), 'Content-Type': 'application/octet-stream' },
      data: Buffer.alloc(0),
    });
    expect(res.status()).toBe(400);
  });

  test('result kind round-trips independently of original', async ({ request }) => {
    const token = await issueToken(request);
    const p = await createProject(request, token, { name: 'result' });
    const bytes = Buffer.from([0x89, 0x50, 0x4e, 0x47, 9, 8, 7, 6]);

    const up = await request.post(`${SERVER_URL}/projects/${p.id}/files/result?ext=png&w=3&h=4`, {
      headers: { ...bearer(token), 'Content-Type': 'application/octet-stream' }, data: bytes,
    });
    expect(up.status()).toBe(201);

    const down = await request.get(`${SERVER_URL}/projects/${p.id}/files/result`, { headers: bearer(token) });
    expect(down.ok()).toBeTruthy();
    expect(Buffer.from(await down.body()).equals(bytes)).toBeTruthy();

    // original still absent → 404 (kinds are independent).
    const orig = await request.get(`${SERVER_URL}/projects/${p.id}/files/original`, { headers: bearer(token) });
    expect(orig.status()).toBe(404);
  });
});
