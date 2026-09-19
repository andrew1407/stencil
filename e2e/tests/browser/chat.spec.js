// Browser-app AI assistant e2e: the chat panel (js/ui/chatPanel.js) against a stub LLM server
// (helpers/llm-stub.js). Covers the llm-contract seams — §5 settings in localStorage, the §6.2
// openai-compat wire shape, and a §1 op-plan whose actions execute against the frozen
// window.stencil facade and whose variants render as thumbnail cards — plus a dock/float
// placement smoke with cross-page persistence.
import { test, expect } from '@playwright/test';
import { gotoApp, APP_URL } from '../../helpers/boot.js';
import { startLlmStub } from '../../helpers/llm-stub.js';
import {
  SYSTEM_PROMPT_HEAD, LLM_SETTINGS_KEY, contentText, seedLlmSettings, openChatPanel,
  clearConversation, sendChat, openCanvasMenu, openMenuClearOfPanel, pointClearOfPanel,
  expectFlyoutOnScreen, openAssistantFlyout, settleFlyout,
} from '../../helpers/chat.js';

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

      // Assistant switched off → the entry is gone and the menu is its old self,
      // classic flyouts included.
      await page.evaluate((key) => {
        localStorage.setItem(key, JSON.stringify({ provider: 'none' }));
        window.dispatchEvent(new Event('stencil:llm-settings-changed'));
      }, LLM_SETTINGS_KEY);
      await openCanvasMenu(page);
      await expect(page.locator('#ctx-assist-menu')).toBeHidden();
      // The separator set is the original one — nothing dangling where the entry was.
      const offSeps = await page.locator('#ctx-menu > .ctx-sep:visible').count();
      expect(offSeps, 'same separators as a menu that never had an assistant').toBe(3);
      // The hidden entry occupies no space: the group leader (Stencil Script) sits right
      // under the group separator (≈11px), with no dead band where the Assistant row was.
      const gap = await page.evaluate(() => {
        const above = document.getElementById('ctx-fullscreen').getBoundingClientRect().bottom;
        const lead = document.getElementById('ctx-script').getBoundingClientRect().top;
        return lead - above;
      });
      expect(gap, 'the group runs straight on from the separator').toBeLessThan(20);
      expect(await page.evaluate(() => document.getElementById('ctx-assist-menu').getBoundingClientRect().height))
        .toBe(0);
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
      // Cleared rows leave on a dissolve (chatView.js) — poll until the motion is done.
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
      // With voice supported, idle Send stays clickable as the mic (chatView.js syncComposerControls)
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
      // same order, as the panel's own menu (one chatView.js template, two id prefixes).
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

  test('dock, float, drag and resize work; layout resets to defaults on reload', async ({ page, context }) => {
    await gotoApp(page);
    await openChatPanel(page);
    const panel = page.locator('#chat-panel');

    // ≤680px viewports force a bottom sheet and hide the dock buttons/resizer, so the placement
    // chrome is only drivable at the harness's desktop 1280×720.
    const vp = page.viewportSize();
    test.skip(!vp || vp.width <= 680, 'dock/resize chrome is hidden on small viewports (bottom sheet mode)');

    // Placement buttons: dock right, then bottom — the host class follows.
    await page.locator('#chat-dock-right-btn').click();
    await expect(panel).toHaveClass(/chat-dock-right/);
    await page.locator('#chat-dock-bottom-btn').click();
    await expect(panel).toHaveClass(/chat-dock-bottom/);

    // The open/dock animations scale/translate the panel for ~200ms, so wait them out before
    // trusting any boundingBox.
    await page.locator('#chat-float-btn').click();
    await expect(panel).toHaveClass(/chat-dock-float/);
    const settle = (p) => p.waitForFunction(() => {
      const el = document.getElementById('chat-panel');
      return el && el.getAnimations({ subtree: false }).every((a) => a.playState === 'finished');
    });
    await settle(page);
    const before = await panel.boundingBox();

    // The drag arms after 4px and anchors at the first (rAF-coalesced) post-threshold pointermove,
    // so assert direction leniently; release >72px from every edge or it lands in a dock zone.
    const ARM = 8;                     // first move past the 4px threshold — arms the drag
    const DRAG_DX = 90, DRAG_DY = 50;  // travel after arming; observed delta ∈ (lenient, ARM + travel]
    const header = await page.locator('#chat-header').boundingBox();
    const hx = header.x + 12, hy = header.y + header.height / 2;
    await page.mouse.move(hx, hy);
    await page.mouse.down();
    await page.mouse.move(hx + ARM, hy + ARM);                      // cross the threshold: drag begins
    await page.mouse.move(hx + ARM + DRAG_DX, hy + ARM + DRAG_DY, { steps: 4 });
    await page.mouse.up();
    await expect(panel).toHaveClass(/chat-dock-float/);             // no dock zone caught the drop
    const moved = await panel.boundingBox();
    expect(moved.x - before.x).toBeGreaterThan(40);
    expect(moved.x - before.x).toBeLessThanOrEqual(ARM + DRAG_DX + 1);
    expect(moved.y - before.y).toBeGreaterThan(20);
    expect(moved.y - before.y).toBeLessThanOrEqual(ARM + DRAG_DY + 1);
    // The transient drop-zone overlay exists only while a drag is live.
    await expect(page.locator('.chat-dock-zones')).toHaveCount(0);

    // Resize from the float corner handle: the handler anchors at pointerdown, so
    // the delta is exact (coalescing only ever drops midpoints).
    const RESIZE_DX = 60, RESIZE_DY = 40;
    const grip = await page.locator('#chat-resizer').boundingBox();
    await page.mouse.move(grip.x + grip.width / 2, grip.y + grip.height / 2);
    await page.mouse.down();
    await page.mouse.move(grip.x + grip.width / 2 + RESIZE_DX, grip.y + grip.height / 2 + RESIZE_DY, { steps: 5 });
    await page.mouse.up();
    const after = await panel.boundingBox();
    expect(Math.round(after.width - before.width)).toBe(RESIZE_DX);
    expect(Math.round(after.height - before.height)).toBe(RESIZE_DY);

    // Layout is deliberately session-only: a fresh page of the same context comes up with the chat
    // closed, and opening it lands at the defaults.
    const page2 = await context.newPage();
    await page2.goto(APP_URL);
    await page2.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });
    const panel2 = page2.locator('#chat-panel');
    await expect(panel2).not.toHaveClass(/chat-open/);   // always starts closed
    await page2.locator('#chat-btn').click();
    await expect(panel2).toHaveClass(/chat-open/);
    await expect(panel2).toHaveClass(/chat-dock-left/);  // default dock, not the moved one
    await page2.locator('#chat-float-btn').click();
    await settle(page2);
    const fresh = await panel2.boundingBox();            // FLOAT_DEFAULT = 80/80/360/440
    expect({ x: Math.round(fresh.x), y: Math.round(fresh.y), w: Math.round(fresh.width), h: Math.round(fresh.height) })
      .toEqual({ x: 80, y: 80, w: 360, h: 440 });
    await page2.close();
  });

  // Fullscreen shows a CLONE of the toolbar (fullscreenLayer.js): the clone is a snapshot, so it
  // cannot follow the panel's open state, and the original #chat-btn it forwards to measures 0×0.
  /* A double-click a HUMAN would make: two presses a beat apart, the second carrying the
   * clickCount that raises `dblclick`. Opening the panel on the FIRST click docked it and
   * pushed #chat-btn ~350px along the toolbar, so the second press landed on empty chrome
   * and the compact gesture never reached the icon. Playwright's own dblclick() dispatches
   * both presses before layout reflows, which is why it never caught this. */
  test('a paced double-click on the chat icon opens the compact popover', async ({ page }) => {
    await gotoApp(page);
    const panel = page.locator('#chat-panel');
    const btn = page.locator('#chat-btn');
    const box = await btn.boundingBox();
    const at = { x: box.x + box.width / 2, y: box.y + box.height / 2 };

    await page.mouse.move(at.x, at.y);
    await page.mouse.down({ clickCount: 1 });
    await page.mouse.up({ clickCount: 1 });
    await page.waitForTimeout(150);
    // The icon must still be under the pointer, or the second press cannot reach it.
    const moved = await btn.boundingBox();
    expect(Math.round(moved.x)).toBe(Math.round(box.x));
    await page.mouse.down({ clickCount: 2 });
    await page.mouse.up({ clickCount: 2 });

    await expect(panel).toHaveClass(/chat-open/);
    await expect(panel).toHaveClass(/chat-dock-float/);
  });

  // pinning a compact popover to the top-left corner instead of the icon.
  test('fullscreen: the cloned chat toggle tracks the panel, and the panel is not stranded', async ({ page }) => {
    await gotoApp(page);
    // #fullscreen-toggle is disabled until there is something to view fullscreen.
    await page.evaluate(async () => {
      await window.stencil.blank('#ffffff', { size: { width: 200, height: 150 } });
    });
    const panel = page.locator('#chat-panel');

    // Open it as the COMPACT popover first (dblclick — the popover gesture), so the
    // panel carries a shape pinned to an icon that fullscreen is about to hide.
    await page.locator('#chat-btn').dblclick();
    await expect(panel).toHaveClass(/chat-open/);
    await expect(panel).toHaveClass(/chat-dock-float/);

    // Enter fullscreen from the keyboard (Alt+F): clicking the toolbar button is a press outside the
    // popover, which dismisses it. Blur the composer first or the hotkey is swallowed by the field.
    await page.evaluate(() => document.activeElement?.blur());
    // Alt down over a toolbar icon fires that icon's Alt-glide, which closes every other mini
    // window (popover.js closeFromGlide), so park the cursor off the toolbar.
    const parked = await pointClearOfPanel(page);
    if (parked) await page.mouse.move(parked.x, parked.y);
    await page.keyboard.press('Alt+f');
    await expect(page.locator('body')).toHaveClass(/fullscreen-mode/);
    // Reveal the fullscreen toolbar so the clone exists and is measurable.
    await page.locator('#fs-top-trigger').hover();
    const fsBtn = page.locator('#fs-controls-panel #chat-btn');
    await expect(fsBtn).toBeVisible();

    // The popover shape was dropped on the transition: the panel is back in a real
    // layout, not a 340×460 box pinned to a corner it can't be reached from.
    await expect(panel).not.toHaveClass(/chat-dock-float/);
    const box = await panel.boundingBox();
    expect(box.width).toBeGreaterThan(0);

    // The clone reflects the OPEN panel, and keeps tracking it across a toggle —
    // this is the "closed it but the icon still reads active" bug.
    await expect(fsBtn).toHaveClass(/active/);
    await fsBtn.click();
    await expect(panel).not.toHaveClass(/chat-open/);
    await expect(fsBtn).not.toHaveClass(/active/);
    await fsBtn.click();
    await expect(panel).toHaveClass(/chat-open/);
    await expect(fsBtn).toHaveClass(/active/);

    // The strip IS the toolbar, so its own fullscreen toggle leaves (Escape does too); the clone
    // shares the real one's id, so scope to the clone here.
    await page.locator('#fs-controls-panel #fullscreen-toggle').click();
    await expect(page.locator('body')).not.toHaveClass(/fullscreen-mode/);
    await expect(page.locator('#controls-body #chat-btn')).toHaveClass(/active/);
  });
});
