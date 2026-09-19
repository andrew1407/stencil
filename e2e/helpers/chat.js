// Shared helpers for the AI-assistant e2e specs — the browser app's chat panel and
// context-menu flyout (tests/browser/chat.spec.js) and the extension popup's embedded
// Assistant section (tests/browser-extension/chat.smoke.spec.js). Both drive helpers/llm-stub.js,
// so the wire-shape readers below are common; the gestures are the browser app's.
import { expect } from '@playwright/test';

// Contract §4: every request must lead with the canonical system prompt.
export const SYSTEM_PROMPT_HEAD = 'You are the AI assistant inside Stencil';

// Contract §5 provider settings. Same shape on every surface; the browser keeps them in
// localStorage under this key, the extension in chrome.storage.local `llmSettings`.
export const LLM_SETTINGS_KEY = 'drawingApp_llmSettings';
export const llmSettings = (baseUrl) => ({
  provider: 'openai-compat', baseUrl, model: 'e2e-model', apiKey: '', serverUrl: '',
});

// The §6.2 openai-compat wire: a text-only message is a plain string, but a turn carrying
// images (§7 auto-attaches the working snapshot + its edge map) is an array of typed
// parts — read the text from either shape.
export const contentText = (content) => (typeof content === 'string'
  ? content
  : (content || []).filter((p) => p.type === 'text').map((p) => p.text).join('\n'));

// Every image a recorded stub request carries, as data: URLs.
export const imageUrls = (request) => (request.body.messages || [])
  .flatMap((m) => (Array.isArray(m.content) ? m.content : []))
  .filter((p) => p.type === 'image_url')
  .map((p) => p.image_url.url);

// ── Browser app ───────────────────────────────────────────────────────────────
// The panel re-reads settings on every send (loadLlmSettings inside getClient), so
// seeding after boot needs no reload. The event is what the settings window itself
// publishes, and surfaces that only EXIST for a configured provider — the context
// menu's Assistant row (ctxAssistant syncAssistant) — are built off it: the assistant
// ships off until a provider is picked (llm-contract §5), so writing the key alone
// leaves that row absent.
export const seedLlmSettings = async (page, baseUrl) => {
  await page.evaluate(([key, s]) => {
    localStorage.setItem(key, JSON.stringify(s));
    window.dispatchEvent(new Event('stencil:llm-settings-changed'));
  }, [LLM_SETTINGS_KEY, llmSettings(baseUrl)]);
};

export const openChatPanel = async (page) => {
  await page.locator('#chat-btn').click();
  await expect(page.locator('#chat-panel')).toHaveClass(/chat-open/);
};

// Attach / clear / settings live in the composer's "…" overflow (chatView.js
// chatComposerActionsHtml), so reaching one means opening that menu first. The menu
// closes itself after an item runs.
export const clearConversation = async (page, prefix = 'chat') => {
  await page.locator(`#${prefix}-more-btn`).click();
  await page.locator(`#${prefix}-clear`).click();
};

// Enter-to-send (the input's documented shortcut): the floating "Get Stencil" install
// button can overlap the docked panel's send button and swallow clicks.
export const sendChat = async (page, text) => {
  await page.locator('#chat-input').fill(text);
  await page.locator('#chat-input').press('Enter');
};

// ── The canvas right-click menu's "Assistant ▸" entry (browser/js/ui/contextMenu.js) ──
// Loading/relaying out an image scrolls the canvas viewport, and a viewport scroll
// dismisses the menu by design — settle first, and reopen if a stray scroll lands between
// the right-click and the assertion (same flake as the extension flyouts).
export const openCanvasMenu = async (page) => {
  await page.waitForTimeout(400);
  const menu = page.locator('#ctx-menu');
  for (let i = 0; i < 4; i++) {
    await page.locator('#canvas').click({ button: 'right', position: { x: 40, y: 40 } });
    if (await menu.evaluate((el) => el.classList.contains('ctx-open'))) break;
    await page.waitForTimeout(250);
  }
  await expect(menu).toHaveClass(/ctx-open/);
};

// The first canvas corner the chat panel does NOT cover — a viewport point clear of the
// toolbar AND of the panel, so a click there reaches the canvas and a parked pointer
// rests on no toolbar icon.
export const pointClearOfPanel = (page) => page.evaluate(() => {
  const c = document.getElementById('canvas').getBoundingClientRect();
  const panel = document.getElementById('chat-panel');
  const pb = panel && panel.classList.contains('chat-open') ? panel.getBoundingClientRect() : null;
  const covered = (x, y) => !!pb && x >= pb.left && x <= pb.right && y >= pb.top && y <= pb.bottom;
  const corners = [[c.right - 8, c.top + 8], [c.left + 8, c.top + 8], [c.right - 8, c.bottom - 8], [c.left + 8, c.bottom - 8]];
  const free = corners.find(([x, y]) => !covered(x, y));
  return free ? { x: free[0], y: free[1] } : null;
});

// Same as openCanvasMenu, but with the panel possibly open and overlapping: right-click at
// viewport coordinates, so the topmost element there receives it.
export const openMenuClearOfPanel = async (page) => {
  await page.waitForTimeout(400);
  const menu = page.locator('#ctx-menu');
  for (let i = 0; i < 4; i++) {
    const pt = await pointClearOfPanel(page);
    if (!pt) return false;
    await page.mouse.click(pt.x, pt.y, { button: 'right' });
    if (await menu.evaluate((el) => el.classList.contains('ctx-open'))) return true;
    await page.waitForTimeout(250);
  }
  return false;
};

// A flyout must be visible AND fully inside the viewport.
export const expectFlyoutOnScreen = async (page, id) => {
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
export const openAssistantFlyout = async (page) => {
  await openCanvasMenu(page);
  await page.locator('#ctx-assist-menu').hover();
  await expectFlyoutOnScreen(page, '#ctx-assist-sub');
};

// Wait out the flyout's pop animation — mid-pop everything is scaled by ~0.95.
export const settleFlyout = (page) => page.waitForFunction(() => document.getElementById('ctx-assist-sub')
  .getAnimations({ subtree: true }).every((a) => a.playState === 'finished'));
