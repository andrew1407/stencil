// Browser-app AI assistant e2e, the context-menu flyout: it continues the panel's conversation
// across a turn, feeds the shared attachment queue, resizes its composer, and on a phone becomes
// a plain item that opens the chat panel.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../../helpers/boot.js';
import { startLlmStub } from '../../../helpers/llm-stub.js';
import {
  LLM_SETTINGS_KEY, contentText, seedLlmSettings, openChatPanel, sendChat, openCanvasMenu,
  expectFlyoutOnScreen, openAssistantFlyout, settleFlyout,
} from '../../../helpers/chat.js';

test.describe('AI assistant chat panel', () => {
  /** @type {Awaited<ReturnType<typeof startLlmStub>>} */
  let stub;

  test.beforeAll(async () => { stub = await startLlmStub(); });
  test.afterAll(async () => { await stub?.close(); });
  test.beforeEach(() => stub.reset());

  test.describe(() => {
    test.beforeEach(async ({ page }) => {
      await gotoApp(page);
      await seedLlmSettings(page, stub.url + '/v1');
      await page.evaluate(async () => {
        await window.stencil.blank('#ffffff', { size: { width: 200, height: 150 } });
      });
    });

    test('context-menu assistant: the flyout survives a turn, on the same conversation', async ({ page }) => {
      // Turn 1 in the PANEL, then close it — the menu must continue this conversation.
      stub.queue({ version: 1, reply: 'Panel turn.', actions: [], variants: [] });
      await openChatPanel(page);
      await sendChat(page, 'remember me');
      await expect(page.locator('#chat-transcript .chat-msg-assistant').last()).toHaveText(/Panel turn/, { timeout: 15_000 });
      await page.locator('#chat-close').click();

      // Turn 2 from the CONTEXT MENU: open the Assistant flyout, type, Enter.
      stub.queue({ version: 1, reply: 'Menu turn — sepia applied.', actions: [{ op: 'filter', mode: 'sepia' }], variants: [] });
      const menu = page.locator('#ctx-menu');
      await openAssistantFlyout(page);
      // It opens as a real chat window: the transcript fills the flyout (≥ 300px, and
      // up to 60vh) instead of sitting at its floor under the composer.
      const transcriptH = await page.locator('#ctx-assist-transcript').evaluate((e) => e.getBoundingClientRect().height);
      expect(transcriptH).toBeGreaterThanOrEqual(300);
      expect(transcriptH).toBeLessThanOrEqual(page.viewportSize().height * 0.6 + 1);
      await page.locator('#ctx-assist-input').fill('make it sepia');
      await expect(menu).toHaveClass(/ctx-open/);                          // typing never dismisses it
      await expect(page.locator('#ctx-assist-sub')).toHaveClass(/ctx-sub-visible/);
      await page.locator('#ctx-assist-input').press('Enter');
      // (The flyout also shows the panel's earlier turn — one shared transcript — so
      // assert on the LAST assistant row.)
      await expect(page.locator('#ctx-assist-transcript .chat-msg-assistant').last())
        .toHaveText(/Menu turn/, { timeout: 15_000 });
      // …through the whole turn — including the plan executing on the facade, which
      // relayouts the canvas (that used to scroll the menu away).
      await expect(menu).toHaveClass(/ctx-open/);
      await expectFlyoutOnScreen(page, '#ctx-assist-sub');
      expect(await page.evaluate(() => window.stencil.settings.filter)).toBe('sepia');

      // Shared history (contract §7 replay): the menu's request replays the panel turn.
      const texts = stub.requests.at(-1).body.messages.map((m) => contentText(m.content));
      expect(texts.some((t) => t.includes('remember me'))).toBeTruthy();
      expect(texts.at(-1)).toContain('make it sepia');

      // Escape from the input closes the menu (menu semantics win over textarea ones).
      await page.locator('#ctx-assist-input').press('Escape');
      await expect(menu).not.toHaveClass(/ctx-open/);

      // No provider → the entry stays (its chat status says what is missing), with no separator
      // of its own, classic flyouts included.
      await page.evaluate((key) => {
        localStorage.setItem(key, JSON.stringify({ provider: 'none' }));
        window.dispatchEvent(new Event('stencil:llm-settings-changed'));
      }, LLM_SETTINGS_KEY);
      await openCanvasMenu(page);
      await expect(page.locator('#ctx-assist-menu')).toBeVisible();
      const offSeps = await page.locator('#ctx-menu > .ctx-sep:visible').count();
      expect(offSeps, 'the entry brings no separator of its own').toBe(3);
      await page.locator('#ctx-style-menu').hover();
      await expectFlyoutOnScreen(page, '#ctx-style-sub');
    });


    // The composer carries the panel's whole action row: send · attach · gear.
    test('context-menu assistant: attach feeds the shared queue, the gear opens the modal', async ({ page }) => {
      await openAssistantFlyout(page);

      // Send and "…" are both squares of the same size on one row — measure after the flyout's pop
      // settles, mid-pop everything is scaled by ~0.95.
      await settleFlyout(page);
      const sizes = await page.locator('#ctx-assist-send, #ctx-assist-more-btn')
        .evaluateAll((els) => els.map((e) => {
          const r = e.getBoundingClientRect();
          return { w: Math.round(r.width), h: Math.round(r.height), y: Math.round(r.top) };
        }));
      expect(sizes).toHaveLength(2);
      expect(new Set(sizes.map((s) => `${s.w}x${s.h}`)).size, 'identical sizes').toBe(1);
      expect(sizes[0].w).toBe(34);
      expect(new Set(sizes.map((s) => s.y)).size, 'no wrapping — one row').toBe(1);

      // Attach a file through the picker: it queues on the SHARED controller, so the
      // chip shows here AND in the panel's own attachments row.
      await page.setInputFiles('#ctx-assist-attach-input', 'fixtures/pixel.png');
      await expect(page.locator('#ctx-assist-attachments .chat-attach-chip')).toHaveCount(1);
      // VISIBLE, not merely present — the row hides itself with an inline display:none.
      await expect(page.locator('#ctx-assist-attachments .chat-attach-chip')).toBeVisible();
      await expect(page.locator('#ctx-assist-attachments .chat-attach-name')).toHaveText(/pixel\.png/);
      await expect(page.locator('#ctx-menu')).toHaveClass(/ctx-open/);   // the picker never dismissed it
      await expect(page.locator('#chat-attachments .chat-attach-chip')).toHaveCount(1);   // same queue

      // It rides the next turn from the menu, and both rows empty afterwards.
      stub.queue({ version: 1, reply: 'Got the image.', actions: [], variants: [] });
      await page.locator('#ctx-assist-input').fill('what is this?');
      await page.locator('#ctx-assist-input').press('Enter');
      await expect(page.locator('#ctx-assist-transcript .chat-msg-assistant').last()).toHaveText(/Got the image/, { timeout: 15_000 });
      const sent = stub.requests.at(-1).body.messages.at(-1);
      const parts = Array.isArray(sent.content) ? sent.content : [];
      expect(parts.some((p) => p.type === 'image_url' || p.type === 'image'), 'the attachment went with the turn').toBeTruthy();
      await expect(page.locator('#ctx-assist-attachments .chat-attach-chip')).toHaveCount(0);
      await expect(page.locator('#ctx-assist-attachments')).toBeHidden();
      await expect(page.locator('#chat-attachments .chat-attach-chip')).toHaveCount(0);

      // The gear closes the menu and opens the ONE settings modal — on top, interactive.
      // It lives in the "…" overflow now, so open that first.
      await page.locator('#ctx-assist-more-btn').click();
      await page.locator('#ctx-assist-settings-btn').click();
      await expect(page.locator('#ctx-menu')).not.toHaveClass(/ctx-open/);
      await expect(page.locator('#chat-settings-overlay')).toHaveClass(/modal-open/);
      await page.locator('#chat-model').fill('typed-into-the-modal');
      await expect(page.locator('#chat-model')).toHaveValue('typed-into-the-modal');
    });

    // The composer inside the flyout resizes with the panel's slider handle.
    test('context-menu assistant: the composer is resizable inside the flyout', async ({ page }) => {
      await openAssistantFlyout(page);
      await settleFlyout(page);

      const inputH = () => page.locator('#ctx-assist-input').evaluate((e) => e.getBoundingClientRect().height);
      const transcriptH = () => page.locator('#ctx-assist-transcript').evaluate((e) => e.getBoundingClientRect().height);
      const before = { input: await inputH(), transcript: await transcriptH() };

      // Drag the slider strip UP: the input grows, the transcript gives up the room,
      // and the flyout stays put (the ROOT menu is never re-placed).
      const grip = await page.locator('#ctx-assist-sizer').boundingBox();
      const menuBox = await page.locator('#ctx-menu').boundingBox();
      await page.mouse.move(grip.x + grip.width / 2, grip.y + grip.height / 2);
      await page.mouse.down();
      await page.mouse.move(grip.x + grip.width / 2, grip.y + grip.height / 2 - 60, { steps: 6 });
      await page.mouse.up();

      const after = { input: await inputH(), transcript: await transcriptH() };
      expect(after.input).toBeGreaterThan(before.input + 30);
      expect(after.transcript).toBeLessThan(before.transcript);
      expect(await page.locator('#ctx-menu').boundingBox()).toEqual(menuBox);   // menu never moved
      await expect(page.locator('#ctx-menu')).toHaveClass(/ctx-open/);          // and never closed
      await expectFlyoutOnScreen(page, '#ctx-assist-sub');
      // Still usable after the resize.
      await page.locator('#ctx-assist-input').fill('still typeable');
      await expect(page.locator('#ctx-assist-input')).toHaveValue('still typeable');
    });

    test.describe(() => {
      test.use({ viewport: { width: 390, height: 780 } });   // phone width: < 680px

      test('context-menu assistant on a phone: a plain item that opens the chat panel', async ({ page }) => {
        await openCanvasMenu(page);

        const item = page.locator('#ctx-assist-menu');
        await expect(item).toBeVisible();
        await expect(item).toHaveClass(/ctx-assist-plain/);
        await expect(page.locator('#ctx-assist-menu .ctx-arrow')).toBeHidden();   // no dangling caret
        // Hover can't open a flyout here…
        await item.hover();
        await expect(page.locator('#ctx-assist-sub')).toBeHidden();
        // …activating it closes the menu and opens the (modal) chat panel instead.
        await item.click();
        await expect(page.locator('#ctx-menu')).not.toHaveClass(/ctx-open/);
        await expect(page.locator('#chat-panel')).toHaveClass(/chat-open/);
        // Same shared conversation: a turn typed here continues in the panel.
        stub.queue({ version: 1, reply: 'Phone turn.', actions: [], variants: [] });
        await sendChat(page, 'from the phone');
        await expect(page.locator('#chat-transcript .chat-msg-assistant').last()).toHaveText(/Phone turn/, { timeout: 15_000 });
      });
    });
  });
});
