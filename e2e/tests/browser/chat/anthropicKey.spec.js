// Browser-app assistant over the direct anthropic wire (llm-providers.md §5, §6.5), against the
// stub LLM server standing in for api.anthropic.com: a key typed into the settings rides one turn
// with the §6.5 headers and is held for the tab only — never localStorage, kept by a reload of the
// tab, absent from a new one. The key is fake; nothing here reaches Anthropic.
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../../helpers/boot.js';
import { startLlmStub } from '../../../helpers/llm-stub.js';
import { SYSTEM_PROMPT_HEAD, contentText, openChatPanel, sendChat } from '../../../helpers/chat.js';

const KEY = 'sk-ant-e2e-0123456789abcdef-not-a-real-key';
const ITEM = 'stencil_llm_session_key';

// No gotoApp: its init script clears localStorage on every load, and the reload below must keep it.
const open = async (page) => {
  await page.goto(APP_URL);
  await page.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });
};
const localValues = (page) => page.evaluate(() => Object.keys(localStorage).map((k) => `${k}=${localStorage.getItem(k)}`).join('\n'));
const heldKey = (page) => page.evaluate((item) => JSON.parse(sessionStorage.getItem(item) || 'null')?.key ?? null, ITEM);

test.describe('assistant: your own Anthropic key, held for the tab', () => {
  /** @type {Awaited<ReturnType<typeof startLlmStub>>} */
  let stub;

  test.beforeAll(async () => { stub = await startLlmStub(); });
  test.afterAll(async () => { await stub?.close(); });
  test.beforeEach(() => stub.reset());

  test('a key typed into the settings rides a turn, stays out of localStorage, survives a reload and no new tab', async ({ page, context }) => {
    await open(page);
    await openChatPanel(page);
    await page.locator('#chat-more-btn').click();
    await page.locator('#chat-settings-btn').click();
    await expect(page.locator('#chat-settings-overlay')).toHaveClass(/modal-open/);
    await page.locator('#chat-provider').selectOption('anthropic', { force: true });
    await expect(page.locator('#chat-base-url')).toHaveValue('https://api.anthropic.com');
    await expect(page.locator('#chat-session-key-status')).toHaveText('No key for this session.');
    await expect(page.locator('#chat-api-key-row')).toBeHidden();
    await page.locator('#chat-base-url').fill(stub.url);
    await page.locator('#chat-model').fill('claude-e2e');
    await page.locator('#chat-session-key').fill(KEY);
    await page.locator('#chat-settings-save').click();
    await expect(page.locator('#chat-settings-overlay')).not.toHaveClass(/modal-open/);

    expect(await heldKey(page)).toBe(KEY);
    const saved = await localValues(page);
    expect(saved).toContain('"provider":"anthropic"');
    expect(saved).not.toContain(KEY);

    stub.queue({ version: 1, reply: 'Hello from the stand-in.', actions: [], variants: [] });
    await sendChat(page, 'say hello');
    await expect(page.locator('#chat-transcript .chat-msg-assistant').last()).toHaveText(/Hello from the stand-in\./, { timeout: 15_000 });

    expect(stub.requests).toHaveLength(1);
    const r = stub.requests[0];
    expect(r.path).toBe('/v1/messages');
    expect(r.headers['x-api-key']).toBe(KEY);
    expect(r.headers['anthropic-version']).toBe('2023-06-01');
    expect(r.headers['anthropic-dangerous-direct-browser-access']).toBe('true');
    expect(r.headers.authorization).toBeUndefined();
    expect(r.body.model).toBe('claude-e2e');
    expect(r.body.max_tokens).toBe(32_768);
    expect(r.body.system.startsWith(SYSTEM_PROMPT_HEAD)).toBeTruthy();
    expect(contentText(r.body.messages.at(-1).content)).toContain('say hello');
    expect(JSON.stringify(r.body)).not.toContain(KEY);
    expect(await localValues(page)).not.toContain(KEY);
    expect(await page.evaluate(() => window.stencil.llm.apiKey)).toBe('[redacted]');

    // A reload of the same tab keeps it, and the next turn still carries it.
    await page.reload();
    await page.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });
    expect(await heldKey(page)).toBe(KEY);
    expect(await page.evaluate(() => window.stencil.llm.keyExpiresAt)).toBeGreaterThan(Date.now());
    stub.queue({ version: 1, reply: 'Still here.', actions: [], variants: [] });
    expect((await page.evaluate(() => window.stencil.prompt('again'))).reply).toBe('Still here.');
    expect(stub.requests.at(-1).headers['x-api-key']).toBe(KEY);

    // A new tab shares the saved settings but not the key: it sends nothing and asks for one.
    const other = await context.newPage();
    await open(other);
    expect(await heldKey(other)).toBeNull();
    expect(await other.evaluate(() => window.stencil.llm.provider)).toBe('anthropic');
    const sent = stub.requests.length;
    await openChatPanel(other);
    await sendChat(other, 'hello?');
    const card = other.locator('#chat-transcript .chat-msg-assistant').last();
    await expect(card).toHaveText(/no API key for this session/, { timeout: 15_000 });
    await expect(card.locator('.chat-config-cta')).toBeVisible();
    expect(stub.requests).toHaveLength(sent);
    await other.close();
  });

  test('Forget key drops it at once, and a fresh browser context never had it', async ({ page, browser }) => {
    await open(page);
    await page.evaluate(([url, key]) => window.stencil.llm.setup({ provider: 'anthropic', baseUrl: url, apiKey: key }), [stub.url, KEY]);
    expect(await heldKey(page)).toBe(KEY);
    await openChatPanel(page);
    await page.locator('#chat-more-btn').click();
    await page.locator('#chat-settings-btn').click();
    await expect(page.locator('#chat-session-key-status')).toHaveText(/^Key kept for this tab until .+\.$/);
    await expect(page.locator('#chat-session-key')).toHaveValue('');
    await page.locator('#chat-session-key-forget').click();
    await expect(page.locator('#chat-session-key-status')).toHaveText('No key for this session.');
    expect(await heldKey(page)).toBeNull();

    const fresh = await browser.newContext();
    const p2 = await fresh.newPage();
    await open(p2);
    expect(await heldKey(p2)).toBeNull();
    expect(await p2.evaluate(() => window.stencil.llm.keyExpiresAt)).toBe(0);
    await fresh.close();
  });
});
