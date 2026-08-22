// Scriptable stub LLM server for the AI-assistant e2e specs — ALL model traffic in
// this harness terminates here; no real LLM, no external network. One Node http
// server (zero deps, like static-server.js) speaks all three provider wire shapes
// from llm-contract.md §6:
//
//   POST */chat/completions  openai-compat  → { choices: [{ message: { content } }] }
//   POST */api/chat          ollama         → { message: { content } }
//   POST */v1/messages       Anthropic      → { model, stop_reason, content: [{type:"text",text}] }
//
// Per-test scripting: `queue()` texts (typically op-plan JSON per contract §1) are
// served FIFO — one per request — and every received request is recorded on
// `requests` ({ method, path, headers, body }) for wire-shape assertions. When the
// queue is empty a harmless chat-only plan is served so a stray extra call never
// hangs a client.
//
// CORS: the browser app calls openai-compat endpoints cross-origin (the same reason
// real Ollama/LM Studio need CORS enabled — contract §5 note), so the stub answers
// OPTIONS preflights and stamps permissive CORS headers on everything.
import http from 'node:http';

// Fixed port for the collaboration-server proxy spec: LLM_BASE_URL is baked into the
// server's environment at container start (helpers/compose.llm.yml interpolates the
// same variable), so the stub must listen on a KNOWN port. Other specs use an
// ephemeral port (port: 0).
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

// Start the stub. `host` 0.0.0.0 makes it reachable from inside the docker-compose
// network (the Go server dials host.docker.internal); the default loopback is enough
// for the browser app / extension, which run on this host.
export async function startLlmStub({ port = 0, host = '127.0.0.1' } = {}) {
  /** @type {{ method: string, path: string, headers: Record<string,string>, body: any }[]} */
  const requests = [];
  /** @type {string[]} */
  const queue = [];
  // Optional response gate: while `hold()` is armed, every POST reply waits for
  // `release()` — so concurrent chats stay in flight upstream and a caller can
  // observe the server's LLM_MAX_IN_FLIGHT gate (tests/server/llm-rate-limit).
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
      // Reachability probes (browser chatPanel's status dot — llmClient probeProvider).
      // Deliberately NOT recorded on `requests`: specs assert exact chat-call counts,
      // and probes fire opportunistically on panel open / settings change.
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
