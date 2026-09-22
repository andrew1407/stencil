// Scriptable stub LLM server for the AI-assistant e2e specs — all model traffic in this
// harness terminates here. One zero-dep Node http server speaks the three llm-contract §6
// wire shapes (openai-compat /chat/completions, ollama /api/chat, Anthropic /v1/messages).
// `queue()` texts are served FIFO and every request is recorded on `requests`; OPTIONS
// preflights and permissive CORS are answered because the browser app calls cross-origin.
import http from 'node:http';

// Fixed port for the collaboration-server proxy spec: LLM_BASE_URL is baked into the server's
// environment at container start, so the stub needs a KNOWN port (other specs pass port: 0).
export const LLM_STUB_PORT = Number(process.env.E2E_LLM_STUB_PORT) || 8189;

// Served when the queue is empty — a valid §1 chat-only plan, so an unscripted call
// degrades loudly (visible in the transcript) instead of hanging or erroring.
const FALLBACK_TEXT = JSON.stringify({
  version: 1, reply: '(llm stub) no scripted reply was queued', actions: [], variants: [],
});

const CORS_HEADERS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'authorization, content-type, x-api-key, anthropic-version',
};

// `host` 0.0.0.0 makes the stub reachable from inside the docker-compose network (the Go
// server dials host.docker.internal); loopback is enough for the browser app.
export async function startLlmStub({ port = 0, host = '127.0.0.1' } = {}) {
  /** @type {{ method: string, path: string, headers: Record<string,string>, body: any }[]} */
  const requests = [];
  /** @type {string[]} */
  const queue = [];
  // While `hold()` is armed every POST reply waits for `release()`, so calls stay in flight upstream.
  /** @type {{ promise: Promise<void>, resolve: () => void } | null} */
  let gate = null;

  const readBody = (req) => new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });

  const server = http.createServer(async (req, res) => {
    if (req.method === 'OPTIONS') {
      res.writeHead(204, CORS_HEADERS).end();
      return;
    }
    const path = (req.url || '/').split('?')[0];
    const json = (status, obj) => {
      res.writeHead(status, { 'Content-Type': 'application/json', ...CORS_HEADERS });
      res.end(JSON.stringify(obj));
    };
    if (req.method === 'GET') {
      // Reachability probes are deliberately NOT recorded on `requests`: specs assert exact chat-call
      // counts, and probes fire opportunistically on panel open / settings change.
      if (path.endsWith('/models')) json(200, { object: 'list', data: [{ id: 'e2e-model' }] });
      else if (path.endsWith('/api/version')) json(200, { version: 'e2e-stub' });
      else json(404, { error: 'not found' });
      return;
    }
    if (req.method !== 'POST') { json(404, { error: 'not found' }); return; }

    let body = null;
    try { body = JSON.parse(await readBody(req)); } catch { /* recorded as null */ }
    requests.push({ method: req.method, path, headers: { ...req.headers }, body });
    const text = queue.length ? queue.shift() : FALLBACK_TEXT;
    if (gate) await gate.promise; // held responses answer only after release()

    if (path.endsWith('/chat/completions')) {          // §6.2 openai-compat
      json(200, { choices: [{ index: 0, message: { role: 'assistant', content: text }, finish_reason: 'stop' }] });
    } else if (path.endsWith('/api/chat')) {           // §6.1 ollama native chat
      json(200, { message: { role: 'assistant', content: text }, done: true });
    } else if (path.endsWith('/v1/messages')) {        // §6.3 upstream Anthropic Messages shape
      json(200, {
        id: 'msg_e2e', type: 'message', role: 'assistant',
        model: (body && body.model) || 'claude-e2e',
        stop_reason: 'end_turn',
        content: [{ type: 'text', text }],
      });
    } else {
      json(404, { error: `llm stub: unknown endpoint ${path}` });
    }
  });

  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolve);
  });
  const bound = /** @type {import('node:net').AddressInfo} */ (server.address()).port;

  return {
    // Reachable from THIS host regardless of the bind address.
    url: `http://127.0.0.1:${bound}`,
    port: bound,
    requests,
    /** Queue the next raw model reply (objects are JSON-stringified). */
    queue: (text) => queue.push(typeof text === 'string' ? text : JSON.stringify(text)),
    /** Hold every subsequent POST response open until release(). */
    hold: () => {
      let resolve;
      const promise = new Promise((r) => { resolve = r; });
      gate = { promise, resolve };
    },
    /** Answer all held responses; new POSTs reply immediately again. */
    release: () => { if (gate) { gate.resolve(); gate = null; } },
    /** Drop recorded requests + any unserved queued replies (per-test isolation). */
    reset: () => { requests.length = 0; queue.length = 0; if (gate) { gate.resolve(); gate = null; } },
    close: () => new Promise((resolve) => server.close(resolve)),
  };
}
