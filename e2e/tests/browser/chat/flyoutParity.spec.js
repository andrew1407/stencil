// Browser-app AI assistant e2e, panel and flyout as one conversation: the two transcripts stay in
// lockstep, the suggestion chips track the shared log in both, and the flyout's action buttons
// match the panel's button-for-button.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../../helpers/boot.js';
import { startLlmStub } from '../../../helpers/llm-stub.js';
import {
  LLM_SETTINGS_KEY, seedLlmSettings, openChatPanel, clearConversation, sendChat,
  openMenuClearOfPanel, expectFlyoutOnScreen, openAssistantFlyout, settleFlyout,
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

    // One conversation ⇒ one transcript: whatever arrives must appear in BOTH surfaces,
    // in the same order, once — and a surface that opens later must render the backlog.
    test('panel and flyout transcripts stay in lockstep, in one run', async ({ page }) => {
      // Rendered rows of a transcript: role + text, in DOM order.
      const rows = (sel) => page.locator(`${sel} .chat-msg`).evaluateAll((els) => els.map((e) => {
        const role = e.classList.contains('chat-msg-user') ? 'user' : 'assistant';
        return `${role}:${(e.textContent || '').trim()}${e.classList.contains('chat-msg-error') ? ' [err]' : ''}`;
      }));
      const panelRows = () => rows('#chat-transcript');
      const menuRows = () => rows('#ctx-assist-transcript');

      // 1. A menu-only conversation, with the panel still CLOSED.
      stub.queue({ version: 1, reply: 'First, from the menu.', actions: [], variants: [] });
      await openAssistantFlyout(page);
      await page.locator('#ctx-assist-input').fill('one');
      await page.locator('#ctx-assist-input').press('Enter');
      await expect(page.locator('#ctx-assist-transcript .chat-msg-assistant').last())
        .toHaveText(/First, from the menu/, { timeout: 15_000 });
      await page.keyboard.press('Escape');

      // 2. Opening the panel AFTERWARDS must render that history, not an empty pane.
      await openChatPanel(page);
      expect(await panelRows()).toEqual(['user:one', 'assistant:First, from the menu.']);
      await expect(page.locator('#chat-transcript .chat-empty')).toHaveCount(0);

      // 3. A turn sent from the PANEL lands in both, in order, once.
      stub.queue({ version: 1, reply: 'Second, from the panel.', actions: [], variants: [] });
      await sendChat(page, 'two');
      await expect(page.locator('#chat-transcript .chat-msg-assistant').last())
        .toHaveText(/Second, from the panel/, { timeout: 15_000 });
      expect(await panelRows()).toEqual(await menuRows());
      expect(await panelRows()).toEqual([
        'user:one', 'assistant:First, from the menu.',
        'user:two', 'assistant:Second, from the panel.',
      ]);

      // 4. …and a turn sent from the MENU with the panel STILL OPEN — including an
      // error row. (Float the panel into a corner so the canvas stays right-clickable.)
      await page.locator('#chat-float-btn').click();
      expect(await openMenuClearOfPanel(page), 'menu opened with the panel open').toBeTruthy();
      await page.locator('#ctx-assist-menu').hover();
      await expectFlyoutOnScreen(page, '#ctx-assist-sub');
      await page.evaluate((key) => {
        const s = JSON.parse(localStorage.getItem(key));
        localStorage.setItem(key, JSON.stringify({ ...s, baseUrl: 'http://127.0.0.1:9/v1' }));
      }, LLM_SETTINGS_KEY);
      await page.locator('#ctx-assist-input').fill('three');
      await page.locator('#ctx-assist-input').press('Enter');
      await expect(page.locator('#ctx-assist-transcript .chat-msg-error')).toHaveCount(1, { timeout: 20_000 });
      const both = [await panelRows(), await menuRows()];
      expect(both[0]).toEqual(both[1]);                       // identical, including the error
      expect(both[0]).toHaveLength(6);                        // nothing duplicated, nothing lost
      expect(both[0][4]).toBe('user:three');
      expect(both[0][5]).toMatch(/^assistant:Couldn't reach .* \[err\]$/);
      // Both surfaces render the same configure CTA on that error card.
      await expect(page.locator('#chat-transcript .chat-config-cta')).toHaveCount(1);
      await expect(page.locator('#ctx-assist-transcript .chat-config-cta')).toHaveCount(1);

      // Dismiss the context menu first: Clear lives in the composer's "…" overflow, which has to be
      // clickable. Escape only closes a compact popover, not a float the user adopted.
      await page.keyboard.press('Escape');
      await expect(page.locator('#ctx-menu')).not.toHaveClass(/ctx-open/);
      await clearConversation(page);
      // Cleared rows leave on a dissolve (view.js) — poll until the motion is done.
      await expect.poll(panelRows, { timeout: 5000 }).toEqual([]);
      await expect.poll(menuRows, { timeout: 5000 }).toEqual([]);
      await expect(page.locator('#chat-transcript .chat-empty')).toHaveCount(1);
      await expect(page.locator('#ctx-assist-transcript .chat-empty')).toHaveCount(1);
    });

    // The empty state belongs to the shared conversation, not either surface: chips in BOTH
    // transcripts whenever the log is empty, and still clickable after being rebuilt.
    test('suggestion chips track the shared conversation in both surfaces', async ({ page }) => {
      const chips = (sel) => page.locator(`${sel} .chat-suggest`);
      const panelChips = () => chips('#chat-transcript');
      const menuChips = () => chips('#ctx-assist-transcript');

      // Both start with the SAME chips (one shared list).
      await openChatPanel(page);
      await page.locator('#chat-float-btn').click();
      const prompts = await panelChips().evaluateAll((els) => els.map((e) => e.dataset.prompt));
      expect(prompts.length).toBeGreaterThanOrEqual(4);
      expect(await menuChips().evaluateAll((els) => els.map((e) => e.dataset.prompt))).toEqual(prompts);

      // First message: both drop them.
      stub.queue({ version: 1, reply: 'Done.', actions: [], variants: [] });
      await sendChat(page, 'hello');
      await expect(page.locator('#chat-transcript .chat-msg-assistant').last()).toHaveText(/Done/, { timeout: 15_000 });
      await expect(panelChips()).toHaveCount(0);
      await expect(menuChips()).toHaveCount(0);

      // Clear: both bring them back…
      await clearConversation(page);
      await expect(panelChips()).toHaveCount(prompts.length);
      await expect(menuChips()).toHaveCount(prompts.length);
      // …and the rebuilt chips still work (they were dead after Clear when the click
      // handler was bound to the block instead of the transcript).
      await panelChips().first().click();
      await expect(page.locator('#chat-input')).toHaveValue(prompts[0]);
    });

    // The three action buttons must read as ONE set in the flyout, matching the panel.
    test('flyout action buttons match the panel button-for-button', async ({ page }) => {
      await openAssistantFlyout(page);
      await settleFlyout(page);

      const look = (id) => page.locator(`#${id}`).evaluate((e) => {
        const cs = getComputedStyle(e);
        const r = e.getBoundingClientRect();
        return {
          w: Math.round(r.width), h: Math.round(r.height), radius: cs.borderRadius,
          bg: cs.backgroundColor, color: cs.color, opacity: cs.opacity, disabled: e.disabled,
          idle: e.getAttribute('aria-disabled') === 'true',
        };
      });
      // The composer is Send + "…" (attach / clear / settings moved into the
      // overflow), so those two are the tiles that must match each other.
      const [send_, more] = await Promise.all([look('ctx-assist-send'), look('ctx-assist-more-btn')]);
      for (const b of [send_, more]) {
        expect({ w: b.w, h: b.h, radius: b.radius }).toEqual({ w: 34, h: 34, radius: '6px' });
      }
      // With voice supported, idle Send stays clickable as the mic (view.js syncComposerControls)
      // and wears the app's shared idle palette, not a bespoke flyout rule.
      expect(send_.idle).toBe(true);
      expect(more.idle).toBe(false);
      expect(more.disabled).toBe(false);
      const panelIdle = await look('chat-send');
      expect(panelIdle.idle).toBe(true);
      expect({ disabled: send_.disabled, bg: send_.bg, color: send_.color, opacity: send_.opacity })
        .toEqual({ disabled: panelIdle.disabled, bg: panelIdle.bg, color: panelIdle.color, opacity: panelIdle.opacity });
      // Typing enables it, and it becomes an accent tile exactly like its sibling.
      // (Poll: the button's background is transitioned, so it arrives a frame later.)
      await page.locator('#ctx-assist-input').fill('hi');
      await expect.poll(async () => {
        const b = await look('ctx-assist-send');
        return { idle: b.idle, disabled: b.disabled, bg: b.bg, color: b.color };
      }, { timeout: 5000 }).toEqual({ idle: false, disabled: false, bg: more.bg, color: more.color });

      // The overflow actions exist, hidden until the "…" is opened — the same set, the
      // same order, as the panel's own menu (one view.js template, two id prefixes).
      const panelItems = await page.locator('#chat-more-menu .chat-more-item')
        .evaluateAll((els) => els.map((e) => e.id.replace(/^chat-/, '')));
      expect(panelItems).toContain('attach-btn');
      expect(panelItems).toContain('clear');
      expect(panelItems).toContain('settings-btn');
      const items = page.locator('#ctx-assist-more-menu .chat-more-item');
      await expect(items).toHaveCount(panelItems.length);
      await expect(page.locator('#ctx-assist-more-menu')).toBeHidden();
      await page.locator('#ctx-assist-more-btn').click();
      await expect(page.locator('#ctx-assist-more-menu')).toBeVisible();
      expect(await items.evaluateAll((els) => els.map((e) => e.id.replace(/^ctx-assist-/, '')))).toEqual(panelItems);
    });
  });
});
