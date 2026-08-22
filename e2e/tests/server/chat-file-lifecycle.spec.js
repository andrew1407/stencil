// Contract-§12 chat transcript over REST: `chat` is a filestore-only file kind
// (llm-contract.md §9 — no project-record columns), so its bytes round-trip like
// any file kind AND, unlike original/result, it is removable on its own via
// DELETE /projects/{id}/files/chat — server/internal/httpapi/files.go.
import { test, expect } from '@playwright/test';
import { issueToken, createProject, bearer, SERVER_URL, stackEnabled } from '../../helpers/serverApi.js';

test.describe('chat transcript file kind (contract §12)', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('chat kind lifecycle: PUT → GET round-trip → DELETE → GET 404', async ({ request }) => {
    const token = await issueToken(request);
    const p = await createProject(request, token, { name: 'chatfile' });
    const fileUrl = `${SERVER_URL}/projects/${p.id}/files/chat`;
    // A small opt-in transcript in the shape clients persist (§12) — the server
    // never parses it, so the pin is byte-exact storage, not schema.
    const transcript = Buffer.from(JSON.stringify({
      version: 1,
      messages: [
        { role: 'user', text: 'rotate it a quarter turn' },
        { role: 'assistant', text: 'Rotated it.' },
      ],
    }));

    // Nothing stored yet.
    expect((await request.get(fileUrl, { headers: bearer(token) })).status()).toBe(404);

    const up = await request.post(`${fileUrl}?ext=json`, {
      headers: { ...bearer(token), 'Content-Type': 'application/octet-stream' },
      data: transcript,
    });
    expect(up.status(), await up.text()).toBe(201);

    // Bytes round-trip exactly.
    const down = await request.get(fileUrl, { headers: bearer(token) });
    expect(down.ok()).toBeTruthy();
    expect(Buffer.from(await down.body()).equals(transcript)).toBeTruthy();

    // Deletable on its own (the user withdraws the opt-in transcript)…
    expect((await request.delete(fileUrl, { headers: bearer(token) })).status()).toBe(204);
    // …and the bytes are really gone.
    expect((await request.get(fileUrl, { headers: bearer(token) })).status()).toBe(404);
    // Deleting an already-absent kind stays 204 (idempotent by contract).
    expect((await request.delete(fileUrl, { headers: bearer(token) })).status()).toBe(204);
  });

  test('original is NOT deletable via the files DELETE route', async ({ request }) => {
    // original/result live on the project record and go with the project; the
    // per-file DELETE route only serves filestore-only kinds (video/variantN/chat).
    const token = await issueToken(request);
    const p = await createProject(request, token, { name: 'chatfile-orig' });
    const res = await request.delete(`${SERVER_URL}/projects/${p.id}/files/original`, { headers: bearer(token) });
    expect(res.status()).toBe(400);
    expect((await res.json()).code).toBe('badRequest');
  });
});
