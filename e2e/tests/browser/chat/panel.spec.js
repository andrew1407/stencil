// Browser-app AI assistant e2e, the panel: against a stub LLM server (helpers/llm-stub.js), a §1
// op-plan whose actions execute on the frozen window.stencil facade and whose variants render as
// thumbnail cards over the §6.2 openai-compat wire, the facade driving the same conversation, and
// the canvas context menu the Assistant entry sits in.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../../helpers/boot.js';
import { startLlmStub } from '../../../helpers/llm-stub.js';
import {
  SYSTEM_PROMPT_HEAD, contentText, seedLlmSettings, openChatPanel, sendChat, openCanvasMenu,
  expectFlyoutOnScreen,
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

    test('op-plan executes on the facade and variants render as cards', async ({ page }) => {
      // Rotation-only variants keep the top-level filter setting observable afterwards: variant
      // execution restores the image snapshot, not the settings.
      stub.queue({
        version: 1,
        reply: 'Applied sepia; here are two rotated variants.',
        actions: [{ op: 'filter', mode: 'sepia' }],
        variants: [
          { label: 'rotated', actions: [{ op: 'rotate', dir: 'right' }] },
          { label: 'upside down', actions: [{ op: 'rotate', dir: 'right', times: 2 }] },
        ],
      });

      await openChatPanel(page);
      await sendChat(page, 'Make it sepia and show me two rotated variants');

      // Reply bubble replaces the pending "…" once the plan has fully executed. (Scoped
      // to the panel: the context-menu flyout mirrors the very same rows.)
      await expect(page.locator('#chat-transcript .chat-msg-assistant')).toHaveText(/Applied sepia/, { timeout: 15_000 });

      // The action took effect through the same facade the toolbar uses.
      expect(await page.evaluate(() => window.stencil.settings.filter)).toBe('sepia');

      // Two variant result cards, each with a rendered thumbnail + its (sanitized) label.
      await expect(page.locator('#chat-transcript .chat-result')).toHaveCount(2);
      await expect(page.locator('#chat-transcript .chat-result-thumb')).toHaveCount(2);
      expect(await page.locator('#chat-transcript .chat-result-label').allTextContents()).toEqual(['rotated', 'upside down']);
      for (const src of await page.locator('#chat-transcript .chat-result-thumb').evaluateAll((els) => els.map((e) => e.src))) {
        expect(src).toMatch(/^data:image\//);
      }

      // The stub saw the §6.2 openai-compat wire shape.
      expect(stub.requests).toHaveLength(1);
      const r = stub.requests[0];
      expect(r.path).toBe('/v1/chat/completions');
      expect(r.body.model).toBe('e2e-model');
      expect(r.body.stream).toBe(false);
      expect(r.body.messages[0].role).toBe('system');
      expect(r.body.messages[0].content.startsWith(SYSTEM_PROMPT_HEAD)).toBeTruthy();
      const user = r.body.messages.at(-1);
      expect(user.role).toBe('user');
      expect(contentText(user.content)).toContain('two rotated variants');
      // …and the §7 auto-attach rode along: the working snapshot + its edge map.
      const images = (Array.isArray(user.content) ? user.content : []).filter((p) => p.type === 'image_url');
      expect(images.length).toBe(2);
      for (const p of images) expect(p.image_url.url).toMatch(/^data:image\//);
    });

    // The facade's chat surface (stencilApi.js → app.chat), which the UI tests never touch:
    // they click buttons, so a boot-order bug replacing app.chat stayed invisible here.

    test('the stencil facade drives the same conversation as the panel', async ({ page }) => {
      stub.queue({ version: 1, reply: 'Rotated it.', actions: [{ op: 'rotate', dir: 'right' }] });

      // The whole panel surface must survive boot — chat persistence wires afterwards.
      expect(await page.evaluate(() => ({
        prompt: typeof window.stencil.prompt,
        open: typeof window.stencil.chat.open,
        close: typeof window.stencil.chat.close,
        dock: typeof window.stencil.chat.dock,
        isOpen: typeof window.stencil.chat.isOpen,
      }))).toEqual({
        prompt: 'function', open: 'function', close: 'function', dock: 'function', isOpen: 'boolean',
      });

      await page.evaluate(() => window.stencil.chat.open());
      await expect(page.locator('#chat-panel')).toHaveClass(/chat-open/);
      expect(await page.evaluate(() => window.stencil.chat.isOpen)).toBe(true);

      const result = await page.evaluate(() => window.stencil.prompt('rotate it right'));
      expect(result.reply).toBe('Rotated it.');

      // One shared conversation: the programmatic turn is rendered in the panel too.
      await expect(page.locator('#chat-transcript .chat-msg-assistant')).toHaveText(/Rotated it\./);
      await expect(page.locator('#chat-transcript .chat-msg-user')).toHaveText(/rotate it right/);
      expect(stub.requests).toHaveLength(1);

      await page.evaluate(() => window.stencil.chat.close());
      await expect(page.locator('#chat-panel')).not.toHaveClass(/chat-open/);
    });

    test('context menu: classic submenus hover-open on-screen, with the Assistant entry present', async ({ page }) => {
      await openCanvasMenu(page);
      // The Assistant entry is a submenu PARENT sitting with the others (caret + flyout)…
      await expect(page.locator('#ctx-assist-menu')).toBeVisible();
      await expect(page.locator('#ctx-assist-menu .ctx-arrow')).toBeVisible();
      // …in the TOP group: it leads the group that runs Script → Start Drawing, below Fit
      // to Window, with its own separator between it and the drawing items.
      const order = await page.evaluate(() => {
        const kids = [...document.getElementById('ctx-menu').children];
        const at = (id) => kids.findIndex((k) => k.id === id);
        return { fit: at('ctx-fit-window'), assist: at('ctx-assist-menu'), script: at('ctx-script'),
          draw: at('ctx-draw-toggle'), seps: kids.filter((k) => k.classList.contains('ctx-sep')).length };
      });
      expect(order.fit).toBeLessThan(order.assist);
      expect(order.assist + 1).toBe(order.script);   // directly above Stencil Script, adjacent
      expect(order.script + 1).toBe(order.draw);

      // Two different classic flyouts still open on hover, and stay on-screen.
      for (const [item, sub] of [['#ctx-style-menu', '#ctx-style-sub'], ['#ctx-filter-menu', '#ctx-filter-sub'],
        ['#ctx-layout-menu', '#ctx-layout-sub'], ['#ctx-tooltip-menu', '#ctx-tooltip-sub']]) {
        await page.locator(item).hover();
        await expectFlyoutOnScreen(page, sub);
        // Hovering a sibling parent hands the flyout over, as always.
        await expect(page.locator('#ctx-menu')).toHaveClass(/ctx-open/);
      }
      // Hovering the Assistant opens ITS flyout, and the previous one closes.
      await page.locator('#ctx-assist-menu').hover();
      await expectFlyoutOnScreen(page, '#ctx-assist-sub');
      await expect(page.locator('#ctx-tooltip-sub')).not.toHaveClass(/ctx-sub-visible/);
      // …and hovering back to a classic parent closes the assistant flyout again.
      await page.locator('#ctx-style-menu').hover();
      await expectFlyoutOnScreen(page, '#ctx-style-sub');
      await expect(page.locator('#ctx-assist-sub')).not.toHaveClass(/ctx-sub-visible/);
    });

    // Hovering a parent WHILE the menu's entry pop runs: the pop scales the menu, so a flyout
    // placed against a transformed ancestor slid out from under the stationary cursor.
    test('context menu: a flyout opened DURING the entry animation opens and stays', async ({ page }) => {
      await page.waitForTimeout(400);
      await page.locator('#canvas').click({ button: 'right', position: { x: 40, y: 40 } });
      // No settle: hover the parent immediately, inside the ~140ms pop.
      await page.locator('#ctx-filter-menu').hover({ force: true });
      await page.waitForTimeout(600);   // past the pop AND the 180ms hide grace
      await expect(page.locator('#ctx-menu')).toHaveClass(/ctx-open/);
      await expectFlyoutOnScreen(page, '#ctx-filter-sub');
      // The same for the Assistant flyout, whose content is the tallest in the menu.
      await page.locator('#ctx-assist-menu').hover({ force: true });
      await page.waitForTimeout(600);
      await expectFlyoutOnScreen(page, '#ctx-assist-sub');
      await expect(page.locator('#ctx-assist-input')).toBeVisible();
    });
  });
});
