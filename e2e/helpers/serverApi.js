// REST helpers for the Go collaboration server, mirroring server/internal/protocol.
// Used by the fullstack + server-protocol suites. Auth: POST /auth/token is always
// admin-gated; the harness starts compose with ADMIN_TOKEN (default 'e2e-admin', see
// compose.js) and sends it here. E2E_SKIP_COMPOSE users must export ADMIN_TOKEN
// matching their server. Every other route wants `Authorization: Bearer`.
export const SERVER_URL = process.env.SERVER_URL || 'http://localhost:8090';
export const SERVER_WS = SERVER_URL.replace(/^http/, 'ws') + '/ws';
export const SERVER_TCP_PORT = Number(process.env.SERVER_TCP_PORT) || 8091;

// The stack-dependent projects self-skip unless the backing stack was brought up.
export const stackEnabled = process.env.E2E_STACK === '1';

// Issue a session token. `request` is Playwright's APIRequestContext.
export async function issueToken(request, label = 'e2e') {
  const res = await request.post(`${SERVER_URL}/auth/token`, {
    headers: { 'X-Admin-Token': process.env.ADMIN_TOKEN || 'e2e-admin' },
    data: { label },
  });
  if (!res.ok()) throw new Error(`issueToken failed: ${res.status()} ${await res.text()}`);
  const body = await res.json();
  return body.token; // { token, expiresAt }
}

export const bearer = (token) => ({ Authorization: `Bearer ${token}` });

// The server requires every project to declare an image, so hasImage defaults true; callers
// may override it.
export async function createProject(request, token, body = {}) {
  const data = { hasImage: true, ...body };
  const res = await request.post(`${SERVER_URL}/projects`, { headers: bearer(token), data });
  if (res.status() !== 201) throw new Error(`createProject: ${res.status()} ${await res.text()}`);
  return res.json();
}

export async function listProjects(request, token) {
  const res = await request.get(`${SERVER_URL}/projects`, { headers: bearer(token) });
  if (!res.ok()) throw new Error(`listProjects: ${res.status()}`);
  return (await res.json()).projects;
}
