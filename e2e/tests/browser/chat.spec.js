// Browser-app AI assistant e2e: the chat panel (browser/js/ui/chatPanel.js) against a
// STUB LLM server (helpers/llm-stub.js) — no real model, no external network. Covers
// the llm-contract.md seams: §5 settings in localStorage, the §6.2 openai-compat
// wire shape, and a §1 op-plan whose actions execute against the frozen window.stencil
// facade (sepia filter) and whose variants render as thumbnail cards. Plus a
// dock/float placement smoke with cross-page persistence.
import { test, expect } from '@playwright/test';
import { gotoApp, APP_URL } from '../../helpers/boot.js';
import { startLlmStub } from '../../helpers/llm-stub.js';

// Contract §4: every request must lead with the canonical system prompt.
const SYSTEM_PROMPT_HEAD = 'You are the AI assistant inside Stencil';

// The §6.2 openai-compat wire: a text-only message is a plain string, but a turn
// carrying images (contract §7 auto-attaches the working snapshot + its edge map)
// is an array of typed parts — read the text from either shape.
const contentText = (content) => (typeof content === 'string'
  ? content
  : (content || []).filter((p) => p.type === 'text').map((p) => p.text).join('\n'));

test.describe('AI assistant chat panel', () => {
  /** @type {Awaited<ReturnType<typeof startLlmStub>>} */
  let stub;

  test.beforeAll(async () => { stub = await startLlmStub(); });
  test.afterAll(async () => { await stub?.close(); });
  test.beforeEach(() => stub.reset());

  // Seed the §5 provider settings (localStorage `drawingApp_llmSettings`) pointing at
  // the stub. The panel re-reads settings on every send (loadLlmSettings inside
  // getClient), so seeding after boot needs no reload.
  const seedSettings = (page) => page.evaluate((baseUrl) => {
    localStorage.setItem('drawingApp_llmSettings', JSON.stringify({
      provider: 'openai-compat', baseUrl, model: 'e2e-model', apiKey: '', serverUrl: '',
    }));
  }, stub.url + '/v1');

  const openPanel = async (page) => {
    await page.locator('#chat-btn').click();
    await expect(page.locator('#chat-panel')).toHaveClass(/chat-open/);
  };

  // Attach / clear / settings live in the composer's "…" overflow (chatView.js
  // chatComposerActionsHtml), so reaching one means opening that menu first. The
  // menu closes itself after an item runs.
  const clearConversation = async (page, prefix = 'chat') => {
    await page.locator(`#${prefix}-more-btn`).click();
    await page.locator(`#${prefix}-clear`).click();
  };

  // Enter-to-send (the input's documented shortcut): the floating "Get Stencil"
  // install button can overlap the docked panel's send button and swallow clicks.
  const send = async (page, text) => {
    await page.locator('#chat-input').fill(text);
    await page.locator('#chat-input').press('Enter');
  };

  // ── The canvas right-click menu's "Assistant ▸" entry (browser/js/ui/contextMenu.js).
  // It is an ordinary submenu parent whose flyout is a chat, so the two things worth
  // driving for real are (a) the CLASSIC flyouts still hover-open on-screen — an
  // earlier build re-clamped the whole menu as the chat grew, which slid the hovered
  // item out from under the cursor and killed every flyout — and (b) the assistant
  // flyout survives a whole turn (typing, sending, a plan executing on the canvas)
  // on the panel's own conversation.
  const openMenu = async (page) => {
    // Loading/relaying out an image scrolls the canvas viewport, and a viewport scroll
    // dismisses the menu by design — settle first, and reopen if a stray scroll lands
    // between the right-click and the assertion (same flake as the extension flyouts).
    await page.waitForTimeout(400);
    const menu = page.locator('#ctx-menu');
    for (let i = 0; i < 4; i++) {
      await page.locator('#canvas').click({ button: 'right', position: { x: 40, y: 40 } });
      if (await menu.evaluate((el) => el.classList.contains('ctx-open'))) break;
      await page.waitForTimeout(250);
    }
    await expect(menu).toHaveClass(/ctx-open/);
  };
  // Same, but with the panel possibly open and overlapping: right-click the first
  // canvas corner the panel does NOT cover (a real right-click at viewport
  // coordinates, so the topmost element there receives it).
  const openMenuClearOfPanel = async (page) => {
    await page.waitForTimeout(400);
    const menu = page.locator('#ctx-menu');
    for (let i = 0; i < 4; i++) {
      const pt = await page.evaluate(() => {
        const c = document.getElementById('canvas').getBoundingClientRect();
        const panel = document.getElementById('chat-panel');
        const pb = panel && panel.classList.contains('chat-open') ? panel.getBoundingClientRect() : null;
        const covered = (x, y) => !!pb && x >= pb.left && x <= pb.right && y >= pb.top && y <= pb.bottom;
        const corners = [[c.right - 8, c.top + 8], [c.left + 8, c.top + 8], [c.right - 8, c.bottom - 8], [c.left + 8, c.bottom - 8]];
        const free = corners.find(([x, y]) => !covered(x, y));
        return free ? { x: free[0], y: free[1] } : null;
      });
      if (!pt) return false;
      await page.mouse.click(pt.x, pt.y, { button: 'right' });
      if (await menu.evaluate((el) => el.classList.contains('ctx-open'))) return true;
      await page.waitForTimeout(250);
    }
    return false;
  };
  // A flyout must be visible AND fully inside the viewport.
  const expectFlyoutOnScreen = async (page, id) => {
    const sub = page.locator(id);
    await expect(sub).toHaveClass(/ctx-sub-visible/, { timeout: 5000 });
    await expect(sub).toBeVisible();
    const box = await sub.boundingBox();
    const vp = page.viewportSize();
    expect(box.width, `${id} has width`).toBeGreaterThan(0);
    expect(box.x, `${id} left edge on-screen`).toBeGreaterThanOrEqual(0);
    expect(box.y, `${id} top edge on-screen`).toBeGreaterThanOrEqual(0);
    expect(box.x + box.width, `${id} right edge on-screen`).toBeLessThanOrEqual(vp.width + 1);
    expect(box.y + box.height, `${id} bottom edge on-screen`).toBeLessThanOrEqual(vp.height + 1);
  };
  // Open the menu and hover the Assistant parent until its flyout is up on-screen.
  const openAssistantFlyout = async (page) => {
    await openMenu(page);
    await page.locator('#ctx-assist-menu').hover();
    await expectFlyoutOnScreen(page, '#ctx-assist-sub');
  };
  // Wait out the flyout's pop animation — mid-pop everything is scaled by ~0.95.
  const settleFlyout = (page) => page.waitForFunction(() => document.getElementById('ctx-assist-sub')
    .getAnimations({ subtree: true }).every((a) => a.playState === 'finished'));

  // Every test in this group starts the same way: boot, point the §5 settings at the
  // stub, and lay down a working image so plans have something to apply to and
  // variants can export. (The dock/float smoke at the bottom needs neither, so it
  // sits outside the group.)
  test.describe(() => {
    test.beforeEach(async ({ page }) => {
      await gotoApp(page);
      await seedSettings(page);
      await page.evaluate(async () => {
        await window.stencil.blank('#ffffff', { size: { width: 200, height: 150 } });
      });
    });

    test('op-plan executes on the facade and variants render as cards', async ({ page }) => {
      // §1 plan: one in-place action (sepia) + two variants. Rotation-only variants keep
      // the top-level filter setting observable afterwards (variant execution restores
      // the image snapshot, not the settings).
      stub.queue({
        version: 1,
        reply: 'Applied sepia; here are two rotated variants.',
        actions: [{ op: 'filter', mode: 'sepia' }],
        variants: [
          { label: 'rotated', actions: [{ op: 'rotate', dir: 'right' }] },
          { label: 'upside down', actions: [{ op: 'rotate', dir: 'right', times: 2 }] },
        ],
      });

      await openPanel(page);
      await send(page, 'Make it sepia and show me two rotated variants');

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
      await openMenu(page);
      // The Assistant entry is a submenu PARENT sitting with the others (caret + flyout)…
      await expect(page.locator('#ctx-assist-menu')).toBeVisible();
      await expect(page.locator('#ctx-assist-menu .ctx-arrow')).toBeVisible();
      // …in the TOP group: above Start Drawing, below Fit to Window, with its own
      // separator between it and the drawing items.
      const order = await page.evaluate(() => {
        const kids = [...document.getElementById('ctx-menu').children];
        const at = (id) => kids.findIndex((k) => k.id === id);
        return { fit: at('ctx-fit-window'), assist: at('ctx-assist-menu'), draw: at('ctx-draw-toggle'),
          seps: kids.filter((k) => k.classList.contains('ctx-sep')).length };
      });
      expect(order.fit).toBeLessThan(order.assist);
      expect(order.assist + 1).toBe(order.draw);   // directly above Start Drawing, adjacent

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

    // The regression that shipped once: hovering a parent WHILE the menu's entry pop is
    // still running. The pop scales the menu, so the hovered item slid out from under the
    // stationary cursor — the flyout was placed against a transformed ancestor and then
    // torn down by the layout-induced mouseleave. It looked like submenus stopped working.
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
      await openPanel(page);
      await send(page, 'remember me');
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
      await page.evaluate(() => {
        localStorage.setItem('drawingApp_llmSettings', JSON.stringify({ provider: 'none' }));
        window.dispatchEvent(new Event('stencil:llm-settings-changed'));
      });
      await openMenu(page);
      await expect(page.locator('#ctx-assist-menu')).toBeHidden();
      // The separator set is the original one — nothing dangling where the entry was.
      const offSeps = await page.locator('#ctx-menu > .ctx-sep:visible').count();
      expect(offSeps, 'same separators as a menu that never had an assistant').toBe(3);
      // The hidden entry occupies no space: the Drawing group sits right under the
      // group separator (≈11px), with no dead band where the Assistant row was.
      const gap = await page.evaluate(() => {
        const above = document.getElementById('ctx-fullscreen').getBoundingClientRect().bottom;
        const draw = document.getElementById('ctx-draw-toggle').getBoundingClientRect().top;
        return draw - above;
      });
      expect(gap, 'the Drawing group runs straight on from the separator').toBeLessThan(20);
      expect(await page.evaluate(() => document.getElementById('ctx-assist-menu').getBoundingClientRect().height))
        .toBe(0);
      await page.locator('#ctx-style-menu').hover();
      await expectFlyoutOnScreen(page, '#ctx-style-sub');
    });

    // The composer carries the panel's whole action row: send · attach · gear.
    test('context-menu assistant: attach feeds the shared queue, the gear opens the modal', async ({ page }) => {
      await openAssistantFlyout(page);

      // The composer row is Send + "…" (attach and settings moved into the overflow):
      // both squares, same size, on one row. (Measure after the flyout's pop animation
      // settles — mid-pop everything is scaled by ~0.95.)
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
      await openPanel(page);
      expect(await panelRows()).toEqual(['user:one', 'assistant:First, from the menu.']);
      await expect(page.locator('#chat-transcript .chat-empty')).toHaveCount(0);

      // 3. A turn sent from the PANEL lands in both, in order, once.
      stub.queue({ version: 1, reply: 'Second, from the panel.', actions: [], variants: [] });
      await send(page, 'two');
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
      await page.evaluate(() => {
        const s = JSON.parse(localStorage.getItem('drawingApp_llmSettings'));
        localStorage.setItem('drawingApp_llmSettings', JSON.stringify({ ...s, baseUrl: 'http://127.0.0.1:9/v1' }));
      });
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

      // 5. Clear from the panel empties BOTH and brings each empty state back.
      // Dismiss the context menu first: it is still open over the floated panel, and
      // Clear now lives in the composer's "…" overflow, which has to be clickable.
      // (Escape only closes the chat when it is a compact popover; this one is a
      // float the user adopted, so it stays open.)
      await page.keyboard.press('Escape');
      await expect(page.locator('#ctx-menu')).not.toHaveClass(/ctx-open/);
      await clearConversation(page);
      // Cleared rows leave on a dissolve (chatView.js) — poll until the motion is done.
      await expect.poll(panelRows, { timeout: 5000 }).toEqual([]);
      await expect.poll(menuRows, { timeout: 5000 }).toEqual([]);
      await expect(page.locator('#chat-transcript .chat-empty')).toHaveCount(1);
      await expect(page.locator('#ctx-assist-transcript .chat-empty')).toHaveCount(1);
    });

    // The empty state is a property of the shared conversation, not of either surface:
    // chips whenever the log is empty, gone with the first message, back after Clear —
    // in BOTH transcripts at once, and still clickable after being rebuilt.
    test('suggestion chips track the shared conversation in both surfaces', async ({ page }) => {
      const chips = (sel) => page.locator(`${sel} .chat-suggest`);
      const panelChips = () => chips('#chat-transcript');
      const menuChips = () => chips('#ctx-assist-transcript');

      // Both start with the SAME chips (one shared list).
      await openPanel(page);
      await page.locator('#chat-float-btn').click();
      const prompts = await panelChips().evaluateAll((els) => els.map((e) => e.dataset.prompt));
      expect(prompts.length).toBeGreaterThanOrEqual(4);
      expect(await menuChips().evaluateAll((els) => els.map((e) => e.dataset.prompt))).toEqual(prompts);

      // First message: both drop them.
      stub.queue({ version: 1, reply: 'Done.', actions: [], variants: [] });
      await send(page, 'hello');
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
        };
      });
      // The composer is Send + "…" (attach / clear / settings moved into the
      // overflow), so those two are the tiles that must match each other.
      const [send_, more] = await Promise.all([look('ctx-assist-send'), look('ctx-assist-more-btn')]);
      for (const b of [send_, more]) {
        expect({ w: b.w, h: b.h, radius: b.radius }).toEqual({ w: 34, h: 34, radius: '6px' });
      }
      // Send is the disabled one, and it wears the app's shared disabled palette —
      // the SAME treatment the panel's send has in that state (no bespoke flyout rule).
      expect(send_.disabled).toBe(true);
      expect(more.disabled).toBe(false);
      const panelDisabled = await look('chat-send');
      expect({ bg: send_.bg, color: send_.color, opacity: send_.opacity })
        .toEqual({ bg: panelDisabled.bg, color: panelDisabled.color, opacity: panelDisabled.opacity });
      // Typing enables it, and it becomes an accent tile exactly like its sibling.
      // (Poll: the button's background is transitioned, so it arrives a frame later.)
      await page.locator('#ctx-assist-input').fill('hi');
      await expect.poll(async () => {
        const b = await look('ctx-assist-send');
        return { disabled: b.disabled, bg: b.bg, color: b.color };
      }, { timeout: 5000 }).toEqual({ disabled: false, bg: more.bg, color: more.color });

      // The three overflow actions exist, hidden until the "…" is opened — the same
      // set, the same order, as the panel's own menu.
      const items = page.locator('#ctx-assist-more-menu .chat-more-item');
      await expect(items).toHaveCount(3);
      await expect(page.locator('#ctx-assist-more-menu')).toBeHidden();
      await page.locator('#ctx-assist-more-btn').click();
      await expect(page.locator('#ctx-assist-more-menu')).toBeVisible();
      expect(await items.evaluateAll((els) => els.map((e) => e.id))).toEqual(
        ['ctx-assist-attach-btn', 'ctx-assist-clear', 'ctx-assist-settings-btn']);
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
        await openMenu(page);

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
        await send(page, 'from the phone');
        await expect(page.locator('#chat-transcript .chat-msg-assistant').last()).toHaveText(/Phone turn/, { timeout: 15_000 });
      });
    });
  });

  test('dock, float, drag and resize work; layout resets to defaults on reload', async ({ page, context }) => {
    await gotoApp(page);
    await openPanel(page);
    const panel = page.locator('#chat-panel');

    // ≤680px viewports force a bottom sheet and hide the dock buttons/resizer, so the
    // placement chrome is only drivable on a desktop-sized viewport (the harness uses
    // Desktop Chrome 1280×720; this guard keeps the intent explicit).
    const vp = page.viewportSize();
    test.skip(!vp || vp.width <= 680, 'dock/resize chrome is hidden on small viewports (bottom sheet mode)');

    // Placement buttons: dock right, then bottom — the host class follows.
    await page.locator('#chat-dock-right-btn').click();
    await expect(panel).toHaveClass(/chat-dock-right/);
    await page.locator('#chat-dock-bottom-btn').click();
    await expect(panel).toHaveClass(/chat-dock-bottom/);

    // Float: the panel takes its session rect as inline style. The open/dock
    // animations (chatSlide*/popIn, animations.css) scale/translate the panel for
    // ~200ms, so wait for them to finish before trusting any boundingBox.
    await page.locator('#chat-float-btn').click();
    await expect(panel).toHaveClass(/chat-dock-float/);
    const settle = (p) => p.waitForFunction(() => {
      const el = document.getElementById('chat-panel');
      return el && el.getAnimations({ subtree: false }).every((a) => a.playState === 'finished');
    });
    await settle(page);
    const before = await panel.boundingBox();

    // Drag the header (left edge — away from the header buttons) to move the panel.
    // The drag arms after a 4px threshold and anchors its offset at the FIRST
    // post-threshold pointermove; pointermoves are rAF-coalesced, so the exact anchor
    // (and hence the exact delta) is timing-dependent — assert direction/magnitude
    // leniently (exactness is pinned by the resize below, which anchors at
    // pointerdown). The release point stays > 72px from every viewport edge so it
    // lands in no dock zone (which would re-dock instead of keeping float).
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

    // Layout is deliberately session-only: a fresh page of the same context (shared
    // origin storage) must come up with the chat CLOSED, and opening it lands at
    // the defaults — docked left, and the stock float rect after clicking float.
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

  // Fullscreen hides the real toolbar and shows a CLONE of it (fullscreenLayer.js
  // clones .controls into #fs-controls-panel), which breaks two things that only a
  // real browser shows: the clone is a snapshot, so it can't follow the panel's
  // open state, and the original #chat-btn it forwards clicks to measures 0×0 —
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

    // Enter fullscreen from the KEYBOARD (Alt+F): clicking the toolbar button would
    // be a press outside the popover, which correctly dismisses it — and then there
    // would be no stranded shape left to assert about. Blur the composer first
    // (openCompact focuses it) or the hotkey is swallowed by the text field.
    await page.evaluate(() => document.activeElement?.blur());
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

    // And leaving fullscreen hands the state back to the real toolbar. The clone is
    // left in the DOM with the same id, so scope to the real one.
    await page.locator('#fs-exit-btn').click();
    await expect(page.locator('body')).not.toHaveClass(/fullscreen-mode/);
    await expect(page.locator('#controls-body #chat-btn')).toHaveClass(/active/);
  });
});
