// Extension assistant over the direct anthropic wire (llm-providers.md §5, §6.5), against the stub
// LLM server standing in for api.anthropic.com: Options holds a typed key in
// chrome.storage.session (never storage.local), the popup's turn carries it with the §6.5
// headers, and Forget drops it. The key is fake; nothing here reaches Anthropic.
import { test, expect } from '@playwright/test';
import { SITE_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';
import { startLlmStub } from '../../helpers/llm-stub.js';

const FIXTURE_URL = SITE_URL + '__e2e__/page-with-image.html';
const KEY = 'sk-ant-e2e-0123456789abcdef-not-a-real-key';

test.describe('extension assistant: your own Anthropic key, held for the browser session', () => {
  /** @type {Awaited<ReturnType<typeof launchExtension>>} */
  let ext;
  /** @type {Awaited<ReturnType<typeof startLlmStub>>} */
  let stub;

  test.beforeAll(async () => {
    stub = await startLlmStub();
    ext = await launchExtension();
  });
  test.afterAll(async () => {
    await ext?.context.close();
    await stub?.close();
  });

  const stored = async () => (await ext.background()).evaluate(async () => ({
    session: await chrome.storage.session.get(null),
    local: JSON.stringify(await chrome.storage.local.get(null)),
  }));

  test('Options holds the key for the session, a popup turn carries it, Forget drops it', async () => {
    test.slow();
    const options = await ext.context.newPage();
    await options.goto(`chrome-extension://${ext.extId}/src/options/options.html`);
    await options.locator('#llm-provider').selectOption('anthropic', { force: true });
    await expect(options.locator('#llm-session-rows')).toBeVisible();
    await expect(options.locator('#llm-apikey')).toBeHidden();
    await expect(options.locator('#llm-sessionkey-status')).toHaveText('No key for this session.');
    await options.locator('#llm-baseurl').fill(stub.url);
    await options.locator('#llm-model').fill('claude-e2e');
    await options.locator('#llm-sessionkey').fill(KEY);
    await options.locator('#llm-save').click();
    await expect(options.locator('#llm-sessionkey-status')).toHaveText(/^Key kept for this browser session until .+\.$/);
    await expect(options.locator('#llm-sessionkey')).toHaveValue('');

    let now = await stored();
    expect(now.session.stencil_llm_session_key.key).toBe(KEY);
    expect(now.local).toContain('"provider":"anthropic"');
    expect(now.local).not.toContain(KEY);

    const host = await ext.context.newPage();
    await host.goto(FIXTURE_URL);
    const popup = await ext.context.newPage();
    await popup.goto(`chrome-extension://${ext.extId}/src/popup/popup.html`);
    await popup.waitForSelector('.filters', { timeout: 15_000 });
    await popup.evaluate(() => document.getElementById('open-chat').click());
    stub.queue({ version: 1, reply: 'Hello from the stand-in.', actions: [], variants: [] });
    await popup.locator('#chat-input').fill('say hello');
    await popup.locator('#chat-send').click();
    await expect(popup.locator('#sec-assistant .msg.assistant')).toHaveText('Hello from the stand-in.', { timeout: 15_000 });
    const r = stub.requests.at(-1);
    expect(r.path).toBe('/v1/messages');
    expect(r.headers['x-api-key']).toBe(KEY);
    expect(r.headers['anthropic-version']).toBe('2023-06-01');
    expect(r.headers['anthropic-dangerous-direct-browser-access']).toBe('true');
    expect(r.headers.authorization).toBeUndefined();
    expect(r.body.model).toBe('claude-e2e');
    expect(JSON.stringify(r.body)).not.toContain(KEY);

    await options.bringToFront();
    await options.locator('#llm-sessionkey-forget').click();
    await expect(options.locator('#llm-sessionkey-status')).toHaveText('No key for this session.');
    now = await stored();
    expect(now.session.stencil_llm_session_key).toBeUndefined();
    expect(now.local).not.toContain(KEY);

    // Without a key the popup sends nothing and points back to Options.
    const sent = stub.requests.length;
    await popup.bringToFront();
    await popup.locator('#chat-input').fill('hello again');
    await popup.locator('#chat-send').click();
    await expect(popup.locator('#sec-assistant .msg.error').last()).toContainText('no API key for this session', { timeout: 15_000 });
    expect(stub.requests).toHaveLength(sent);
    await popup.close();
    await host.close();
    await options.close();
  });
});
