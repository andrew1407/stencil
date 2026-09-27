// Browser-app AI assistant e2e, the panel's placement: dock, float, drag and resize with the layout
// reset on reload, the paced double-click that opens the compact popover, and the fullscreen
// toolbar's cloned chat toggle.
import { test, expect } from '@playwright/test';
import { gotoApp, APP_URL } from '../../../helpers/boot.js';
import { openChatPanel, pointClearOfPanel } from '../../../helpers/chat.js';

test.describe('AI assistant chat panel', () => {
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

  // Fullscreen shows a CLONE of the toolbar (layer.js): the clone is a snapshot, so it
  // cannot follow the panel's open state, and the original #chat-btn it forwards to measures 0×0.
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
