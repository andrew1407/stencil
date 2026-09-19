// Characterization pins for the /llm/chat spend controls (server/internal/httpapi/llmlimit.go):
// a server-wide in-flight gate (LLM_MAX_IN_FLIGHT, default 8) and a per-session token bucket
// (LLM_RATE_PER_MINUTE, default 30 — a fresh session may burst a minute's worth). Both answer
// 429 with the code `rateLimited`, told apart here by message + Retry-After. Requests omit
// `system`: the server rejects any prompt not carrying Stencil's pinned head, and empty is allowed.
import { test, expect } from '@playwright/test';
import { issueToken, bearer, SERVER_URL, stackEnabled } from '../../helpers/serverApi.js';
import { startLlmStub, LLM_STUB_PORT } from '../../helpers/llm-stub.js';

const MAX_IN_FLIGHT = 8; // server default LLM_MAX_IN_FLIGHT
const RATE_PER_MINUTE = 30; // server default LLM_RATE_PER_MINUTE

const chat = (request, token, text) => request.post(`${SERVER_URL}/llm/chat`, {
  headers: bearer(token),
  data: { messages: [{ role: 'user', text }] },
});

test.describe('LLM proxy spend controls', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  /** @type {Awaited<ReturnType<typeof startLlmStub>>} */
  let stub;

  // Fixed port + all-interfaces bind, as in the fullstack llm-proxy spec: the server container
  // dials host.docker.internal at LLM_STUB_PORT from inside the compose network.
  test.beforeAll(async () => {
    try {
      stub = await startLlmStub({ port: LLM_STUB_PORT, host: '0.0.0.0' });
    } catch (err) {
      if (err && err.code === 'EADDRINUSE') {
        throw new Error(
          `llm-rate-limit: port ${LLM_STUB_PORT} is already in use — a stale stub/dev process is squatting on the `
          + 'LLM stub port. Kill it (lsof -nP -iTCP:'
          + `${LLM_STUB_PORT} -sTCP:LISTEN) or set E2E_LLM_STUB_PORT (the compose override interpolates the same variable).`,
        );
      }
      throw err;
    }
  });
  test.afterAll(async () => { await stub?.close(); });
  test.beforeEach(() => stub.reset());

  // The proxy is enabled only when compose.llm.yml was applied (harness-managed
  // stack). With E2E_SKIP_COMPOSE=1 the server may run without the LLM env.
  async function llmEnabled(request, token) {
    const res = await request.get(`${SERVER_URL}/llm/info`, { headers: bearer(token) });
    expect(res.ok()).toBeTruthy();
    return (await res.json()).enabled;
  }

  test('in-flight cap: the 9th+ concurrent chat is turned away, held ones complete', async ({ request }) => {
    test.slow();
    const token = await issueToken(request, 'llm-inflight');
    test.skip(!(await llmEnabled(request, token)),
      'server is running without the LLM env (helpers/compose.llm.yml not applied — e.g. E2E_SKIP_COMPOSE=1)');

    // Hold the upstream so accepted calls occupy their gate slot until release().
    stub.hold();
    const CONCURRENT = MAX_IN_FLIGHT + 2;
    /** @type {{ status: number, headers: Record<string,string>, body: any }[]} */
    const settled = [];
    const posts = Array.from({ length: CONCURRENT }, (_, i) =>
      chat(request, token, `inflight ${i}`).then(async (res) => {
        const r = { status: res.status(), headers: res.headers(), body: await res.json() };
        settled.push(r);
        return r;
      }));

    // The gate is demonstrably full (8 upstream calls held at the stub) and the
    // overflow was rejected IMMEDIATELY (non-blocking gate) — only then release.
    await expect.poll(() => stub.requests.length, {
      message: `expected ${MAX_IN_FLIGHT} held upstream calls (is LLM_MAX_IN_FLIGHT not at its default?)`,
      timeout: 30_000,
    }).toBe(MAX_IN_FLIGHT);
    await expect.poll(() => settled.filter((r) => r.status === 429).length, {
      message: 'expected the overflow requests to be answered 429 while the gate is full',
      timeout: 30_000,
    }).toBe(CONCURRENT - MAX_IN_FLIGHT);
    stub.release();

    const results = await Promise.all(posts);
    const ok = results.filter((r) => r.status === 200);
    const limited = results.filter((r) => r.status === 429);
    expect(ok).toHaveLength(MAX_IN_FLIGHT);
    expect(limited).toHaveLength(CONCURRENT - MAX_IN_FLIGHT);
    for (const r of limited) {
      expect(r.body).toMatchObject({ code: 'rateLimited' });
      expect(r.body.message).toMatch(/busy|in flight/); // the gate's message, not the bucket's
      expect(r.headers['retry-after']).toBe('5');
    }
    // Held calls completed normally after release — the stub's §1 fallback plan
    // rode back through the Anthropic proxy shape.
    for (const r of ok) {
      expect(r.body.text).toBeTruthy();
      expect(r.body.stopReason).toBe('end_turn');
    }
  });

  test('per-session rate: the burst is honoured, then 429; other sessions unaffected', async ({ request }) => {
    test.slow();
    const token = await issueToken(request, 'llm-rate');
    test.skip(!(await llmEnabled(request, token)),
      'server is running without the LLM env (helpers/compose.llm.yml not applied — e.g. E2E_SKIP_COMPOSE=1)');

    // Sequential, so the in-flight gate cannot fire: a fresh bucket holds RATE_PER_MINUTE tokens and
    // refills at that rate, so the headroom covers refill during the loop.
    const MAX_TURNS = RATE_PER_MINUTE + 15;
    let limited = null;
    let okCount = 0;
    for (let i = 0; i < MAX_TURNS && !limited; i++) {
      const res = await chat(request, token, `turn ${i}`);
      if (res.status() === 429) {
        limited = { headers: res.headers(), body: await res.json() };
      } else {
        expect(res.status(), await res.text()).toBe(200);
        okCount += 1;
      }
    }
    expect(limited, `no 429 within ${MAX_TURNS} sequential turns (is LLM_RATE_PER_MINUTE not at its default?)`).toBeTruthy();
    expect(okCount).toBeGreaterThanOrEqual(RATE_PER_MINUTE); // the full burst went through first
    expect(limited.body).toMatchObject({ code: 'rateLimited' });
    expect(limited.body.message).toMatch(/wait a moment/); // the bucket's message, not the gate's
    expect(limited.headers['retry-after']).toBe('60');

    // The bucket is per-session: a different session still has its full burst.
    const other = await issueToken(request, 'llm-rate-other');
    const res = await chat(request, other, 'fresh session');
    expect(res.status(), await res.text()).toBe(200);
  });
});
