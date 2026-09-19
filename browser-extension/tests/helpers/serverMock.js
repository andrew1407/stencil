// Injected-fetch stand-ins for a Stencil collaboration server, shared by the connections*
// suites, plus the chrome.storage mock those wrappers persist into.
import { installChromeStub } from './chromeStub.js';

export const installStorageMock = () => {
  const stub = installChromeStub();
  return { peek: stub.peek, reset: stub.reset };
};

// A mock server: routes the REST subset the extension uses.
export const mockFetch = (opts = {}) => {
  const projects = opts.projects || [];
  const json = (status, body) => ({ ok: status >= 200 && status < 300, status, json: async () => body });
  return async (url, init = {}) => {
    const u = new URL(url);
    const method = init.method || 'GET';
    if (u.pathname === '/auth/token' && method === 'POST') return json(200, { token: 'tk', expiresAt: 0 });
    if (u.pathname === '/projects' && method === 'GET') return json(200, { projects });
    if (u.pathname === '/projects' && method === 'POST') return json(201, { id: 'p_new_a', name: JSON.parse(init.body).name });
    return json(404, { code: 'notFound', message: 'no route' });
  };
};

// A fetch that only accepts one session token — enough to prove which token connect used.
export const tokenGatedFetch = (good) => async (url, init = {}) => {
  const auth = ((init.headers || {}).Authorization || '').replace('Bearer ', '');
  const json = (status, body) => ({ ok: status >= 200 && status < 300, status, json: async () => body });
  if (new URL(url).pathname === '/auth/token') return json(401, { message: 'admin required' });
  if (auth !== good) return json(401, { message: 'bad token' });
  return json(200, { projects: [] });
};
