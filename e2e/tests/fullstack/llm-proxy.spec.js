// Full-stack LLM proxy e2e: the real browser app chats through the real Go server's Anthropic
// proxy (POST /llm/chat behind bearer auth), which forwards to the harness's stub LLM server —
// never the real Anthropic API. The server's LLM env is injected by the compose override
// helpers/compose.llm.yml on every harness-managed `up`; with E2E_SKIP_COMPOSE=1 the override
// is not applied and this spec self-skips off GET /llm/info.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';
import { issueToken, bearer, SERVER_URL, stackEnabled } from '../../helpers/serverApi.js';
import { startLlmStub, LLM_STUB_PORT } from '../../helpers/llm-stub.js';

test.describe('LLM proxy (stencil-server provider)', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  /** @type {Awaited<ReturnType<typeof startLlmStub>>} */
  let stub;

  // The server container dials host.docker.internal:<LLM_STUB_PORT>, so the stub must
  // bind all interfaces (docker's host-gateway traffic doesn't arrive on loopback).
  test.beforeAll(async () => {
    try {
      stub = await startLlmStub({ port: LLM_STUB_PORT, host: '0.0.0.0' });
    } catch (err) {
      if (err && err.code === 'EADDRINUSE') {
        throw new Error(
          `llm-proxy: port ${LLM_STUB_PORT} is already in use — a stale stub/dev process is squatting on the `
          + 'LLM stub port. Kill it (lsof -nP -iTCP:'
          + `${LLM_STUB_PORT} -sTCP:LISTEN) or set E2E_LLM_STUB_PORT (the compose override interpolates the same variable).`,
        );
      }
      throw err;
    }
  });
  test.afterAll(async () => { await stub?.close(); });

  test('chat via the server reaches the Anthropic upstream shape', async ({ page, request }) => {
    test.slow();
    const token = await issueToken(request);

    // GET /llm/info (bearer-authed): enabled + the configured model. Asserted over
    // REST directly — cheaper and less brittle than driving the settings modal UI.
    const infoRes = await request.get(`${SERVER_URL}/llm/info`, { headers: bearer(token) });
    expect(infoRes.ok()).toBeTruthy();
    const info = await infoRes.json();
    test.skip(!info.enabled, 'server is running without the LLM env (helpers/compose.llm.yml not applied — e.g. E2E_SKIP_COMPOSE=1)');
    expect(info.model).toBe('claude-e2e');

    // A disabled proxy is a separate contract point (§6.3) — not reachable here, since
    // this stack has the key; the server unit suite covers the 503 llmDisabled path.

    await gotoApp(page);

    // Point the chat provider at the SAME connection URL so the panel reuses its bearer token
    // (contract §5 stencil-server).
    const serverHost = new URL(SERVER_URL).host; // stencil.connect normalizes to an origin, so the host survives
    const connUrl = await page.evaluate(async ({ url, token, host }) => {
      await window.stencil.connect({ url, token });
      return window.stencil.connections.find((u) => u.includes(host));
    }, { url: SERVER_URL, token, host: serverHost });
    expect(connUrl).toBeTruthy();
    await page.evaluate((serverUrl) => {
      localStorage.setItem('drawingApp_llmSettings', JSON.stringify({
        provider: 'stencil-server', baseUrl: '', model: '', apiKey: '', serverUrl,
      }));
    }, connUrl);

    // Chat-only §1 plan from the "model".
    stub.queue({ version: 1, reply: 'Hello from the proxy.', actions: [], variants: [] });

    await page.locator('#chat-btn').click();
    await expect(page.locator('#chat-panel')).toHaveClass(/chat-open/);
    // Enter-to-send: the floating "Get Stencil" install button can overlap the docked
    // panel's send button and swallow clicks (same workaround as tests/browser/chat).
    await page.locator('#chat-input').fill('Say hello through the server');
    await page.locator('#chat-input').press('Enter');

    // The reply renders in the transcript (round-trip browser → server → stub → back).
    await expect(page.locator('#chat-transcript .chat-msg-assistant')).toHaveText('Hello from the proxy.', { timeout: 20_000 });

    // The stub saw the §6.3 upstream call: POST /v1/messages with the Anthropic
    // headers, the configured model, and the canonical system prompt.
    expect(stub.requests).toHaveLength(1);
    const r = stub.requests[0];
    expect(r.path).toBe('/v1/messages');
    expect(r.headers['x-api-key']).toBe('e2e-test');
    expect(r.headers['anthropic-version']).toBe('2023-06-01');
    expect(r.body.model).toBe('claude-e2e');
    expect(r.body.max_tokens).toBeGreaterThan(0);
    expect(String(r.body.system).startsWith('You are the AI assistant inside Stencil')).toBeTruthy();
    const user = r.body.messages.at(-1);
    expect(user.role).toBe('user');
    expect(JSON.stringify(user.content)).toContain('Say hello through the server');
  });
});
