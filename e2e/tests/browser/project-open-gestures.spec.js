// Opening a project from the projects list, by gesture (js/ui/projectsModal.js):
//   MOUSE  click → confirm modal, this tab      dblclick → open now, no modal
//          ⌘/Ctrl+click → confirm, NEW TAB      ⌘/Ctrl+dblclick → new tab now, no modal
//   TOUCH  tap → confirm, this tab              long press (≥500ms) → confirm, NEW TAB
// Driven through the real browser: the app's shared confirm dialog, and a real second page.
import { test, expect } from '@playwright/test';
import { gotoApp, seedProjectsAndOpenList } from '../../helpers/boot.js';
import { finger, ghostBox } from '../../helpers/drag.js';

// Two saved local projects, then the Projects modal open (helpers/boot.js). Returns the
// row of the project that is NOT active, so every gesture has something real to switch to.
const seedAndOpenList = (page, extra = 0) => seedProjectsAndOpenList(page, { extra });

const confirmModal = (page) => page.locator('#confirm-modal-overlay');
const activeId = (page) => page.evaluate(() => window.stencil.current?.id ?? null);

// How far anything has scrolled (page or the list itself — either counts as "the list moved").
const scrollTop = (page) => page.evaluate(() => Math.max(
  document.scrollingElement?.scrollTop || 0,
  ...[...document.querySelectorAll('#projects-modal-overlay *')].map((e) => e.scrollTop || 0),
));

test.describe('projects list: open gestures', () => {
  test('single click asks first, and opens in THIS tab on confirm', async ({ page }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page);
    const before = await activeId(page);
    const targetId = await target.getAttribute('data-id');

    await target.click();
    await expect(confirmModal(page)).toHaveClass(/modal-open/);
    await expect(page.locator('#confirm-modal-message')).toHaveText(/Open ".*" here\?/);

    // Cancel leaves everything alone…
    await page.locator('#confirm-modal-cancel').click();
    await expect(confirmModal(page)).not.toHaveClass(/modal-open/);
    expect(await activeId(page)).toBe(before);

    // …confirming switches this tab to that project and closes the list.
    await target.click();
    await expect(confirmModal(page)).toHaveClass(/modal-open/);
    await page.locator('#confirm-modal-confirm').click();
    await expect(page.locator('#projects-modal-overlay')).not.toHaveClass(/modal-open/);
    expect(String(await activeId(page))).toBe(String(targetId));
  });

  test('double click opens immediately — the modal never even flashes', async ({ page }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page);
    const targetId = await target.getAttribute('data-id');

    // Watch for the modal appearing at ANY point during the gesture (the deferred
    // single click would pop it ~250ms later if the dblclick didn't cancel it).
    await page.evaluate(() => {
      window.__modalFlashes = 0;
      new MutationObserver(() => {
        if (document.getElementById('confirm-modal-overlay').classList.contains('modal-open')) window.__modalFlashes++;
      }).observe(document.getElementById('confirm-modal-overlay'), { attributes: true, attributeFilter: ['class'] });
    });

    await target.dblclick();
    await expect(page.locator('#projects-modal-overlay')).not.toHaveClass(/modal-open/);
    expect(String(await activeId(page))).toBe(String(targetId));
    await page.waitForTimeout(600);   // well past the double-click deferral
    expect(await page.evaluate(() => window.__modalFlashes), 'no confirmation modal at all').toBe(0);
    await expect(confirmModal(page)).not.toHaveClass(/modal-open/);
  });

  test('⌘/Ctrl + click asks, then opens in a NEW TAB', async ({ page, context }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page);
    const before = await activeId(page);

    await target.click({ modifiers: ['ControlOrMeta'] });
    await expect(confirmModal(page)).toHaveClass(/modal-open/);
    await expect(page.locator('#confirm-modal-message')).toHaveText(/in a new tab\?/);

    const opened = context.waitForEvent('page', { timeout: 10_000 });
    await page.locator('#confirm-modal-confirm').click();
    const tab = await opened;
    await tab.waitForLoadState('domcontentloaded');
    expect(tab.url()).toMatch(/[?#]open=/);              // the app's open-in-new-tab hand-off
    expect(await activeId(page)).toBe(before);           // THIS tab is untouched
    await tab.close();
  });

  test('⌘/Ctrl + double click opens a NEW TAB immediately, no modal', async ({ page, context }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page);
    const before = await activeId(page);

    const opened = context.waitForEvent('page', { timeout: 10_000 });
    await target.dblclick({ modifiers: ['ControlOrMeta'] });
    const tab = await opened;
    await tab.waitForLoadState('domcontentloaded');
    await expect(confirmModal(page)).not.toHaveClass(/modal-open/);
    await page.waitForTimeout(600);
    await expect(confirmModal(page)).not.toHaveClass(/modal-open/);
    expect(await activeId(page)).toBe(before);
    await tab.close();
  });

  test('keyboard: Enter on a focused row opens it, with the confirmation', async ({ page }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page);
    const targetId = await target.getAttribute('data-id');
    await target.focus();
    await expect(target).toBeFocused();                  // rows are reachable (tabindex/role)
    await expect(target).toHaveAttribute('role', 'button');
    await page.keyboard.press('Enter');
    await expect(confirmModal(page)).toHaveClass(/modal-open/);
    await page.locator('#confirm-modal-confirm').click();
    expect(String(await activeId(page))).toBe(String(targetId));
  });
});

// ── Touch: no modifiers, no double click — tap and long press instead ──
test.describe('projects list: touch gestures', () => {
  test.use({ viewport: { width: 390, height: 780 }, hasTouch: true, isMobile: true });

  test('tap asks and opens here; the ⋯ menu is the new-tab route', async ({ page, context }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page);
    const before = await activeId(page);
    const box = await target.boundingBox();

    // 1. A quick tap → the confirmation, targeting THIS tab.
    await page.touchscreen.tap(box.x + box.width / 2, box.y + box.height / 2);
    await expect(confirmModal(page)).toHaveClass(/modal-open/);
    await expect(page.locator('#confirm-modal-message')).toHaveText(/here\?/);
    await page.locator('#confirm-modal-cancel').click();
    expect(await activeId(page)).toBe(before);

    // 2. "Open in a new tab" on touch lives in the row's ⋯ menu — the hold belongs to
    // drag-to-reorder (touchDrag.js picks the row up at 280ms), so it is NOT overloaded.
    await expect(page.locator('#projects-modal-overlay')).toHaveClass(/modal-open/);
    await target.locator('.project-more').click();
    const item = page.locator('.project-menu-item', { hasText: 'Open in new tab' });
    await expect(item).toBeVisible();
    await item.click();
    await expect(confirmModal(page)).toHaveClass(/modal-open/);
    await expect(page.locator('#confirm-modal-message')).toHaveText(/in a new tab\?/);
    const opened = context.waitForEvent('page', { timeout: 10_000 });
    await page.locator('#confirm-modal-confirm').click();
    const tab = await opened;
    await tab.waitForLoadState('domcontentloaded');
    expect(await activeId(page)).toBe(before);           // this tab untouched
    await tab.close();
  });

  test('a press-and-hold in place picks the row up to REORDER — it opens nothing', async ({ page }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page);
    const before = await activeId(page);
    const box = await target.boundingBox();
    const touch = await finger(page);

    // Hold well past every threshold without moving, then release in place: the touch
    // drag engine owns this gesture (ghost at 280ms) and no project may open.
    await touch.down(box.x + box.width / 2, box.y + box.height / 2);
    await page.waitForTimeout(700);
    expect(await page.locator('.project-dragging').count(), 'the hold is the reorder pickup').toBe(1);
    await touch.up(box.x + box.width / 2, box.y + box.height / 2);

    await page.waitForTimeout(500);
    await expect(confirmModal(page)).not.toHaveClass(/modal-open/);
    expect(await activeId(page)).toBe(before);
  });

  test('a touch reorder drag completes — the finger keeps the gesture, the list never scrolls', async ({ page }) => {
    await gotoApp(page, { motion: 'none' });
    const { rows, target } = await seedAndOpenList(page, 2);
    const before = await activeId(page);
    const orderBefore = await page.locator('.project-row[data-drag-key]')
      .evaluateAll((els) => els.map((e) => e.dataset.dragKey));
    const from = await target.boundingBox();
    const other = await rows.first().boundingBox();
    const scrollBefore = await scrollTop(page);
    const touch = await finger(page);

    // Grab well off-centre: an anchoring error in the ghost only shows away from the middle.
    const gx = from.x + 40, gy = from.y + from.height / 2;
    await touch.down(gx, gy);
    await page.waitForTimeout(400);                       // pickup (280ms) + margin
    const grabOffset = gx - (await ghostBox(page)).left;

    // Drop in the TOP quarter of the other row → land BEFORE it (a real move).
    const tx = other.x + 40, ty = other.y + 6;
    await touch.glide(gx, gy, tx, ty);

    // The ghost is still there — i.e. the browser did NOT steal the gesture to scroll —
    // and it is still anchored under the finger at the point it was grabbed by.
    const held = await ghostBox(page);
    expect(held, 'the drag survived the move (no scroll steal)').not.toBeNull();
    expect(Math.abs(tx - held.left - grabOffset), 'the ghost stays under the finger').toBeLessThan(2);
    await touch.up(tx, ty);

    await page.waitForTimeout(500);
    expect(await scrollTop(page), 'a reorder drag must not scroll anything').toBe(scrollBefore);
    // The reorder happened (manual order persisted, the row moved) …
    await expect(page.locator('#projects-sort')).toHaveValue('manual');
    const orderAfter = await page.locator('.project-row[data-drag-key]')
      .evaluateAll((els) => els.map((e) => e.dataset.dragKey));
    expect(orderAfter, 'the rows really did reorder').not.toEqual(orderBefore);
    expect([...orderAfter].sort()).toEqual([...orderBefore].sort());
    // … and nothing opened.
    await expect(confirmModal(page)).not.toHaveClass(/modal-open/);
    expect(await activeId(page)).toBe(before);
  });

  test('dragging a row out to the "Open here" zone opens it, by finger', async ({ page }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page);
    const before = await activeId(page);
    const targetId = await target.getAttribute('data-id');
    const box = await target.boundingBox();
    const touch = await finger(page);

    // Pick the row up and carry it off the dialog card, into the top-left band. The zones sit
    // OUTSIDE the card (zoneForPoint), so this only works while the drag survives the move.
    const gx = box.x + box.width / 2, gy = box.y + box.height / 2;
    await touch.down(gx, gy);
    await page.waitForTimeout(400);
    await touch.glide(gx, gy, 60, 30, 8);
    await expect(page.locator('.pdz-here.pdz-over'), 'the Open here zone lit up').toHaveCount(1);
    await touch.up(60, 30);

    // The zone asks first, the same as a tap on the row does.
    await expect(confirmModal(page)).toHaveClass(/modal-open/);
    await page.locator('#confirm-modal-confirm').click();
    await expect.poll(() => activeId(page), { timeout: 5000 }).toBe(targetId);
    expect(targetId).not.toBe(before);
  });

  test('a swipe over a row still scrolls the list — it never opens anything', async ({ page }) => {
    await gotoApp(page, { motion: 'none' });
    const { target } = await seedAndOpenList(page, 3);
    const before = await activeId(page);
    const box = await target.boundingBox();
    const scrollBefore = await scrollTop(page);
    const touch = await finger(page);

    // Swipe up straight away (no hold): the browser keeps this one — suppressing scroll for
    // the drag must not cost the list its ordinary scrolling.
    const x = box.x + box.width / 2, y = box.y + box.height / 2;
    await touch.down(x, y);
    await touch.glide(x, y, x, y - 120);
    await touch.up(x, y - 120);

    await page.waitForTimeout(400);
    expect(await scrollTop(page), 'the swipe scrolled the list').toBeGreaterThan(scrollBefore);
    await expect(confirmModal(page)).not.toHaveClass(/modal-open/);
    expect(await activeId(page)).toBe(before);
  });
});
