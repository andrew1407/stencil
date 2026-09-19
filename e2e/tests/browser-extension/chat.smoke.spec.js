// Extension AI assistant e2e: the popup's embedded Assistant section chats about the CURRENT
// scan against a stub LLM server (helpers/llm-stub.js). Covers the llm-contract §8 extension
// profile — `focus` (the injected on-page highlight) and `open` (the `#stencil=` editor
// hand-off) — with settings in chrome.storage.local `llmSettings` (§5 shape). Editor-internal
// state is deliberately not asserted: the hand-off URL payload is the headless-safe seam.
import { setTimeout as sleep } from 'node:timers/promises';
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';
import { startLlmStub } from '../../helpers/llm-stub.js';
import { contentText, imageUrls, llmSettings as stubLlmSettings } from '../../helpers/chat.js';

const FIXTURE_URL = APP_URL + '__e2e__/page-with-image.html';

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

  const sendPrompt = async (popup, text) => {
    await popup.locator('#chat-input').fill(text);
    await popup.locator('#chat-send').click();
  };

  test('popup Assistant section: focus highlights on the page, open hands off with the filter', async () => {
    test.slow();
    // Host fixture + popup (as an ordinary chrome-extension:// page — Playwright can't
    // pop the real toolbar popup), following the popup.smoke pattern.
    const host = await context.newPage();
    await host.goto(FIXTURE_URL);
    const popup = await context.newPage();
    await popup.goto(`chrome-extension://${extId}/src/popup/popup.html`);
    await popup.waitForSelector('.filters', { timeout: 15_000 });

    // The initial scan hit the popup page itself; make the fixture the active tab and
    // re-scan so the assistant's live scan state targets the HOST tab's images.
    await host.bringToFront();
    await popup.evaluate(() => document.getElementById('rescan').click());
    await popup.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });

    // The Assistant section ships collapsed; the sparkle header button expands it,
    // boots the chat lazily, and focuses the input.
    await expect(popup.locator('#sec-assistant')).toHaveClass(/collapsed/);
    await popup.evaluate(() => document.getElementById('open-chat').click());
    await expect(popup.locator('#sec-assistant')).not.toHaveClass(/collapsed/);

    // The provider line lives on the … trigger's rich tooltip (lib/chatStatusTip.js), built on
    // hover — not a title attribute.
    await popup.locator('#chat-more-btn').hover();
    await expect(popup.locator('.chat-status-tip')).toContainText('OpenAI API', { timeout: 15_000 });
    await expect(popup.locator('.chat-status-tip')).toContainText('e2e-model');
    await popup.mouse.move(0, 0);

    // ── Turn 1: §8 `focus` → the injected on-page highlight marking. ──
    stub.queue({ version: 1, reply: 'Highlighted it for you.', actions: [{ op: 'focus', image: 0 }], variants: [] });
    await sendPrompt(popup, 'Highlight the first image on the page');
    await expect(popup.locator('#sec-assistant .msg.assistant')).toHaveText('Highlighted it for you.', { timeout: 15_000 });
    const focusCard = popup.locator('#sec-assistant .card', { hasText: 'Focused image 0' });
    await expect(focusCard).toBeVisible();
    await expect(focusCard).not.toHaveClass(/fail/);
    // hoverHighlight.js marks the matched element with data-stencil-listhover.
    await expect(host.locator('[data-stencil-listhover]')).toBeAttached({ timeout: 10_000 });

    // ── Turn 2: §8 `open` with a core filter action → editor hand-off whose
    // `#stencil=` payload carries a layout with imageFilter (translateOpenActions). ──
    stub.queue({
      version: 1, reply: 'Opening it in the editor with sepia.',
      actions: [{ op: 'open', image: 0, actions: [{ op: 'filter', mode: 'sepia' }] }],
      variants: [],
    });
    const editorPagePromise = context.waitForEvent('page', { timeout: 15_000 });
    await sendPrompt(popup, 'Open the first image in the editor and make it sepia');
    const editor = await editorPagePromise;
    await editor.waitForLoadState('domcontentloaded');
    await expect(popup.locator('#sec-assistant .msg.assistant').nth(1)).toHaveText('Opening it in the editor with sepia.', { timeout: 15_000 });

    // The hand-off URL is the assertable seam (the editor strips the fragment only
    // after it consumes the payload, so it is still present at domcontentloaded).
    const url = editor.url();
    expect(url.startsWith(APP_URL)).toBeTruthy();
    expect(url).toContain('#stencil=');
    const payload = JSON.parse(decodeURIComponent(url.slice(url.indexOf('#stencil=') + '#stencil='.length)));
    expect(payload.dataUrl).toMatch(/^data:image\//);
    expect(payload.layout).toBeTruthy();
    expect(payload.layout.imageFilter).toBe('sepia');

    // Both model calls hit the stub with the §8 system prompt suffix (the numbered
    // scan listing the ops reference by index).
    expect(stub.requests).toHaveLength(2);
    const system = stub.requests[0].body.messages[0];
    expect(system.role).toBe('system');
    expect(contentText(system.content)).toContain('Images scanned from the current page');

    await editor.close();
    await host.close();
    await popup.close(); // the embedded assistant lives and dies with the popup
  });

  // Chrome's createImageBitmap refuses an `image/svg+xml` blob, so lib/rasterize.js decodes it
  // with an <img>; the bytes that reach the model must be PNG (contract §7).
  test('popup Assistant section: an SVG image attaches as rasterised PNG', async () => {
    test.slow();
    stub.reset();
    const host = await context.newPage();
    await host.goto(FIXTURE_URL);
    // An SVG with only a viewBox — no intrinsic pixel size at all (the hard case).
    await host.evaluate(() => {
      const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10">'
        + '<circle cx="5" cy="5" r="4" fill="red"/></svg>';
      const img = document.createElement('img');
      img.id = 'svg-target';
      img.alt = 'e2e-svg';
      img.src = 'data:image/svg+xml;base64,' + btoa(svg);
      document.body.appendChild(img);
    });
    await host.waitForFunction(() => document.getElementById('svg-target')?.complete === true);

    const popup = await context.newPage();
    await popup.goto(`chrome-extension://${extId}/src/popup/popup.html`);
    await popup.waitForSelector('.filters', { timeout: 15_000 });
    await host.bringToFront();
    await popup.evaluate(() => document.getElementById('rescan').click());
    await popup.waitForFunction(() => document.querySelectorAll('.row').length > 1, null, { timeout: 15_000 });
    await popup.evaluate(() => document.getElementById('open-chat').click());
    await popup.locator('#chat-more-btn').hover();
    await expect(popup.locator('.chat-status-tip')).toContainText('e2e-model', { timeout: 15_000 });
    await popup.mouse.move(0, 0);

    // Turn 1 (chat-only) just to read back the §8 listing and learn the SVG's index —
    // the ops address images by their position in that listing.
    stub.queue({ version: 1, reply: 'Ready.', actions: [], variants: [] });
    await sendPrompt(popup, 'What is on this page?');
    await expect(popup.locator('#sec-assistant .msg.assistant')).toHaveText('Ready.', { timeout: 15_000 });
    const listing = contentText(stub.requests[0].body.messages[0].content);
    const idx = Number(/^(\d+):[^\n]*e2e-svg/m.exec(listing)?.[1]);
    expect(Number.isInteger(idx)).toBeTruthy();

    // Turn 2: `attach` the SVG → the client fetches + rasterises it and auto-continues
    // ONCE with the image in context (contract §8), so two replies are consumed.
    stub.queue({ version: 1, reply: 'Fetching it.', actions: [{ op: 'attach', image: idx }], variants: [] });
    stub.queue({ version: 1, reply: 'It is a red circle.', actions: [], variants: [] });
    await sendPrompt(popup, 'What is in the SVG?');
    await expect(popup.locator('#sec-assistant .card', { hasText: `Attached image ${idx}` })).toBeVisible({ timeout: 15_000 });
    // Assistant messages so far: "Ready." (turn 1), "Fetching it." (the attach plan),
    // then the auto-continuation's answer.
    await expect(popup.locator('#sec-assistant .msg.assistant').nth(2)).toHaveText('It is a red circle.', { timeout: 15_000 });

    // The continuation call carries the rasterised bytes — PNG, never image/svg+xml.
    expect(stub.requests).toHaveLength(3);
    const images = imageUrls(stub.requests[2]);
    expect(images.length).toBeGreaterThan(0);
    for (const url of images) expect(url.startsWith('data:image/png;base64,')).toBeTruthy();

    await host.close();
    await popup.close();
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
  // one (lib/dragSections.js). Events are dispatched — a native drag can't hold a hover dwell.
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
