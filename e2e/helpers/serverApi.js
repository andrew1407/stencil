// REST helpers for the Go collaboration server, mirroring server/internal/protocol.
// Used by the fullstack + server-protocol suites. Auth: POST /auth/token issues an
// opaque bearer token (open issuance when the server's ADMIN_TOKEN is unset, which
// is the docker-compose dev default); every other route wants `Authorization: Bearer`.
export const SERVER_URL = process.env.SERVER_URL || 'http://localhost:8090';
export const SERVER_WS = SERVER_URL.replace(/^http/, 'ws') + '/ws';
export const SERVER_TCP_PORT = Number(process.env.SERVER_TCP_PORT) || 8091;

// The stack-dependent projects self-skip unless the backing stack was brought up.
export const stackEnabled = process.env.E2E_STACK === '1';

// Issue a session token. `request` is Playwright's APIRequestContext.
export async function issueToken(request, label = 'e2e') {
  const res = await request.post(`${SERVER_URL}/auth/token`, { data: { label } });
  if (!res.ok()) throw new Error(`issueToken failed: ${res.status()} ${await res.text()}`);
  const body = await res.json();
  return body.token; // { token, expiresAt }
}

export const bearer = (token) => ({ Authorization: `Bearer ${token}` });

// Create a project and return its ProjectRecord. The server requires every project to
// declare an image (a project is created FROM an image), so default hasImage:true —
// these fixtures stand in for real image-backed projects. Callers may override it.
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
