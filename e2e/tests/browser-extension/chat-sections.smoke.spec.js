// Extension AI assistant e2e, the popup's section chrome: provider "none" removes the embedded
// Assistant section and its ✦ button, and a drag springs open only the section it hovers. Runs
// against a stub LLM server (helpers/llm-stub.js) with settings in chrome.storage.local.
import { setTimeout as sleep } from 'node:timers/promises';
import { test, expect } from '@playwright/test';
import { APP_URL, SITE_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';
import { startLlmStub } from '../../helpers/llm-stub.js';
import { llmSettings as stubLlmSettings } from '../../helpers/chat.js';

const FIXTURE_URL = SITE_URL + '__e2e__/page-with-image.html';

test.describe('extension AI assistant (embedded section)', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;
  let extId = '';
  /** @type {Awaited<ReturnType<typeof launchExtension>>['background']} */
  let background;
  /** @type {Awaited<ReturnType<typeof startLlmStub>>} */
  let stub;

  test.beforeAll(async () => {
    stub = await startLlmStub();
    ({ context, background, extId } = await launchExtension());
    const sw = await background();
    // The §5 `llmSettings` shape, read when the assistant section boots and on storage.onChanged.
    await sw.evaluate(({ editorUrl, llm }) => new Promise((resolve) =>
      chrome.storage.sync.set({ editorUrl }, () =>
        chrome.storage.local.set({ llmSettings: llm }, resolve))),
    {
      editorUrl: APP_URL,
      llm: { ...stubLlmSettings(stub.url + '/v1'), serverToken: '' },
    });
    await sleep(500);
  });

  test.afterAll(async () => {
    await context?.close();
    await stub?.close();
  });

  // Provider 'none' means the assistant is off (contract §5) and its surfaces must not appear at
  // all; flipping the provider back restores them live off the chrome.storage mirror.
  test('provider "none" removes the Assistant section and its ✦ button entirely', async () => {
    test.slow();
    const sw = await background();
    const setProvider = (provider) => sw.evaluate(async (p) => {
      const { llmSettings } = await chrome.storage.local.get('llmSettings');
      await chrome.storage.local.set({ llmSettings: { ...llmSettings, provider: p } });
    }, provider);

    const popup = await context.newPage();
    await popup.goto(`chrome-extension://${extId}/src/popup/popup.html`);
    await popup.waitForSelector('.filters', { timeout: 15_000 });
    const section = popup.locator('#sec-assistant');
    const sparkle = popup.locator('#open-chat');
    await expect(section).toBeVisible();
    await expect(sparkle).toBeVisible();

    // Switched off while the surface is OPEN → both disappear.
    await setProvider('none');
    await expect(section).toBeHidden({ timeout: 10_000 });
    await expect(sparkle).toBeHidden();

    // A surface opened while it is off never shows them either.
    const fresh = await context.newPage();
    await fresh.goto(`chrome-extension://${extId}/src/popup/popup.html`);
    await fresh.waitForSelector('.filters', { timeout: 15_000 });
    await expect(fresh.locator('#sec-assistant')).toBeHidden({ timeout: 10_000 });
    await expect(fresh.locator('#open-chat')).toBeHidden();
    // With the section absent a drag simply doesn't consider it — and the list still
    // springs (the section spring reads it as "not a collapsed target", not an error).
    await fresh.evaluate(() => {
      const dt = new DataTransfer();
      dt.setData('text/uri-list', 'https://example.com/cat.png');
      document.getElementById('list').dispatchEvent(
        new DragEvent('dragover', { dataTransfer: dt, bubbles: true, cancelable: true }));
    });
    await expect(fresh.locator('header .logo')).toHaveClass(/drag-armed/);
    await expect(fresh.locator('#sec-assistant')).toBeHidden();
    await fresh.close();

    // Picking a provider again brings both back, still without a reload.
    await setProvider('openai-compat');
    await expect(section).toBeVisible({ timeout: 10_000 });
    await expect(sparkle).toBeVisible();
    await popup.close();
  });

  // Spring-loaded sections: the one the pointer dwells on during a drag unfolds, and only that
  // one (lib/drop/dragSections.js). Events are dispatched — a native drag can't hold a hover dwell.
  test('a drag springs open only the section it hovers, and folds it back if it ends elsewhere', async () => {
    test.slow();
    stub.reset();
    const host = await context.newPage();
    await host.goto(FIXTURE_URL);
    const panel = await context.newPage();
    await panel.goto(`chrome-extension://${extId}/src/sidepanel/sidepanel.html`);
    await panel.waitForSelector('.filters', { timeout: 15_000 });
    await host.bringToFront();
    await panel.evaluate(() => document.getElementById('rescan').click());
    await panel.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });

    // Dispatch one drag event, carrying a row-drag payload (the drag type our own rows
    // set, plus the image URL a drop would read).
    const fireDrag = (type, selector) => panel.evaluate(({ type, selector }) => {
      const dt = new DataTransfer();
      dt.setData('application/x-stencil-drag', 'img');
      dt.setData('text/uri-list', document.querySelector('.thumb').src);
      document.querySelector(selector).dispatchEvent(
        new DragEvent(type, { dataTransfer: dt, bubbles: true, cancelable: true }));
    }, { type, selector });

    const assistant = panel.locator('#sec-assistant');
    const assistantHead = assistant.locator('.section-head');
    const search = panel.locator('#sec-search');

    // Both drop targets folded: the assistant ships collapsed, fold the results too.
    await search.locator('.section-head').click();
    await expect(search).toHaveClass(/collapsed/);
    await expect(assistant).toHaveClass(/collapsed/);

    // 1. Sweeping ACROSS a header on the way somewhere else must not pop it open.
    await fireDrag('dragstart', '.row');
    await fireDrag('dragover', '#sec-assistant .section-head');
    await fireDrag('dragover', 'header h1');            // moved on before the dwell
    await panel.waitForTimeout(700);
    await expect(assistant).toHaveClass(/collapsed/);
    await expect(search).toHaveClass(/collapsed/);

    // 2. Dwelling on the assistant's header springs ONLY the assistant.
    await fireDrag('dragover', '#sec-assistant .section-head');
    await expect(assistant).not.toHaveClass(/collapsed/, { timeout: 5_000 });
    await expect(assistantHead).toHaveAttribute('aria-expanded', 'true');   // via the real toggler
    await expect(search).toHaveClass(/collapsed/);      // never visited → never opened

    // 3. Dropping into it attaches the image AND keeps the section open. The chat's
    // drop target is the COMPOSER (chatDrop wireDropTarget), not the whole section.
    await fireDrag('drop', '#sec-assistant .chat-composer');
    await fireDrag('dragend', '.row');
    await expect(panel.locator('#chat-tray .chip')).toHaveCount(1, { timeout: 15_000 });
    await expect(assistant).not.toHaveClass(/collapsed/);

    // 4. A second drag dwells on the RESULTS header: it springs open, and folds back
    //    when the drag ends without a drop there (the assistant, dropped into, stays).
    await fireDrag('dragstart', '.row');
    await fireDrag('dragover', '#sec-search .section-head');
    await expect(search).not.toHaveClass(/collapsed/, { timeout: 5_000 });
    await fireDrag('dragend', '.row');
    await expect(search).toHaveClass(/collapsed/);
    await expect(search.locator('.section-head')).toHaveAttribute('aria-expanded', 'false');
    await expect(assistant).not.toHaveClass(/collapsed/);
    await expect(panel.locator('#chat-tray .chip')).toHaveCount(1);   // nothing new attached

    await host.close();
    await panel.close();
  });
});
