// Server access model, black-boxing the running binary: a project is a SHARED workspace — any
// valid token may list, read, edit and delete any project; unauthenticated callers are rejected
// at the door (401). Mirrors server/internal/httpapi/projects.go.
import { test, expect } from '@playwright/test';
import { issueToken, createProject, listProjects, bearer, SERVER_URL, stackEnabled } from '../../helpers/server/api.js';

test.describe('server shared-workspace access', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('any valid token can read, edit, and list a project created by another token', async ({ request }) => {
    const tokenA = await issueToken(request, 'client-A');
    const tokenB = await issueToken(request, 'client-B');
    expect(tokenB).not.toBe(tokenA);

    const p = await createProject(request, tokenA, { name: 'shared', imageW: 4, imageH: 4, hasImage: true });

    // Seed a file as A, then read it back as B — bytes are shared.
    const bytes = Buffer.from([0x89, 0x50, 0x4e, 0x47, 1, 2, 3, 4]);
    const seed = await request.post(`${SERVER_URL}/projects/${p.id}/files/original?ext=png&w=4&h=4`, {
      headers: { ...bearer(tokenA), 'Content-Type': 'application/octet-stream' }, data: bytes,
    });
    expect(seed.status()).toBe(201);

    // B reads the project + its bytes.
    const getB = await request.get(`${SERVER_URL}/projects/${p.id}`, { headers: bearer(tokenB) });
    expect(getB.ok()).toBeTruthy();
    const current = await getB.json();
    expect(current.project.id).toBe(p.id);
    const fileGetB = await request.get(`${SERVER_URL}/projects/${p.id}/files/original`, { headers: bearer(tokenB) });
    expect(fileGetB.ok()).toBeTruthy();
    expect(Buffer.from(await fileGetB.body()).equals(bytes)).toBeTruthy();

    // B edits it (LWW version guard still applies, but ownership does not). The file
    // upload above bumped the version, so edit against the CURRENT version, not 0.
    const putB = await request.put(`${SERVER_URL}/projects/${p.id}`, {
      headers: bearer(tokenB), data: { name: 'edited-by-B', version: current.project.version },
    });
    expect(putB.ok()).toBeTruthy();
    expect((await putB.json()).version).toBe(current.project.version + 1);

    // B lists it.
    expect((await listProjects(request, tokenB)).map((r) => r.id)).toContain(p.id);

    // Any token may delete it too, and the row is gone for everyone.
    expect((await request.delete(`${SERVER_URL}/projects/${p.id}`, { headers: bearer(tokenB) })).status()).toBe(204);
    expect((await request.get(`${SERVER_URL}/projects/${p.id}`, { headers: bearer(tokenA) })).status()).toBe(404);
  });

  test('file GET/PUT with no token is 401 unauthorized', async ({ request }) => {
    const token = await issueToken(request);
    const p = await createProject(request, token, { name: 'noauth' });

    const noAuthGet = await request.get(`${SERVER_URL}/projects/${p.id}/files/original`);
    expect(noAuthGet.status()).toBe(401);
    expect((await noAuthGet.json()).code).toBe('unauthorized');

    const noAuthPut = await request.post(`${SERVER_URL}/projects/${p.id}/files/original?ext=png&w=1&h=1`, {
      headers: { 'Content-Type': 'application/octet-stream' }, data: Buffer.from([1, 2, 3, 4]),
    });
    expect(noAuthPut.status()).toBe(401);
    expect((await noAuthPut.json()).code).toBe('unauthorized');

    await request.delete(`${SERVER_URL}/projects/${p.id}`, { headers: bearer(token) });
  });
});
