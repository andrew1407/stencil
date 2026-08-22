// The translucent copy of a row that follows the pointer while it is dragged
// (browser/js/ui/dragGhost.js), on BOTH input paths.
//
// It used to be the browser's own drag image, rasterized from a cloned element by
// `setDragImage`. On a HiDPI display Chrome rendered that snapshot at the device scale
// while applying the grab offset in the other one, so the ghost came out oversized and
// trailing far to the right of the cursor — and none of it was reachable from the DOM to
// test. It is now an element we position ourselves, which is what these assert: right
// size, right place, gone afterwards, and the same on mouse and finger.
import { test, expect } from '@playwright/test';
import { gotoApp, seedProjectsAndOpenList } from '../../helpers/boot.js';

// Record what gets handed to the native drag image, to prove we suppress it.
const spyOnDragImage = (page) => page.addInitScript(() => {
  window.__dragImages = [];
  const orig = DataTransfer.prototype.setDragImage;
  DataTransfer.prototype.setDragImage = function (img, x, y) {
    window.__dragImages.push({ tag: img && img.tagName, w: img && img.width, h: img && img.height, x, y });
    return orig.apply(this, arguments);
  };
});

// Three saved projects (helpers/boot.js), so nth(1) is a draggable middle row.
const seedAndOpenList = (page) => seedProjectsAndOpenList(page, { extra: 1 });

// The ghost's box, and the source row's measured at the SAME instant — row heights settle a
// little after first paint, so a pre-drag measurement would drift against a mid-drag ghost.
const ghostBox = (page) => page.evaluate(() => {
  const g = document.querySelector('[data-drag-ghost]');
  if (!g) return null;
  const b = g.getBoundingClientRect();
  const src = document.querySelector('.project-row.project-dragging');
  const s = src && src.getBoundingClientRect();
  return {
    left: b.left, top: b.top, width: b.width, height: b.height,
    row: s ? { width: s.width, height: s.height } : null,
  };
});

test.describe('drag ghost: mouse', () => {
  test.use({ viewport: { width: 1400, height: 900 } });

  test('is row-sized, stays under the cursor, and is cleaned up', async ({ page }) => {
    await spyOnDragImage(page);
    await gotoApp(page);
    await seedAndOpenList(page);

    const row = page.locator('.project-row[data-drag-key]').nth(1);
    const box = await row.boundingBox();
    // Grab well off-centre — an anchoring error is invisible in the middle of the row.
    const gx = box.x + 40, gy = box.y + box.height / 2;

    await page.mouse.move(gx, gy);
    await page.mouse.down();
    await page.mouse.move(gx + 30, gy - 10);
    await page.waitForTimeout(80);

    const g = await ghostBox(page);
    expect(g, 'a ghost of our own is drawn').not.toBeNull();
    // Same size as the row it copies (bar the 1.02 lift), NOT a rescaled snapshot: the old
    // native drag image came out at the DEVICE scale — 2× on a Retina screen. The bounds are
    // loose because row heights settle by a pixel or two as thumbnails decode.
    for (const axis of ['width', 'height']) {
      const ratio = g[axis] / g.row[axis];
      expect(ratio, `ghost ${axis} vs the row`).toBeGreaterThan(0.95);
      expect(ratio, `ghost ${axis} vs the row`).toBeLessThan(1.1);
    }

    // The native drag image was replaced by a 1×1 transparent stand-in.
    const imgs = await page.evaluate(() => window.__dragImages);
    expect(imgs.length, 'setDragImage was called').toBeGreaterThan(0);
    expect(imgs[0].tag).toBe('IMG');
    expect(imgs[0].w, 'a 1×1 image — the browser draws nothing').toBe(1);
    expect(imgs[0].h).toBe(1);

    // …and it follows. The anchor is read off the ghost itself (the list can settle by a
    // pixel between measurements); what must hold is that it does not DRIFT as the drag
    // goes on — the old ghost slid further from the cursor the further you dragged.
    const anchorX = gx + 30 - g.left, anchorY = gy - 10 - g.top;
    for (const [dx, dy] of [[80, -20], [200, 40], [340, 90]]) {
      await page.mouse.move(gx + dx, gy + dy);
      await page.waitForTimeout(80);
      const moved = await ghostBox(page);
      expect(Math.abs((gx + dx) - moved.left - anchorX), `ghost x at +${dx}`).toBeLessThan(2);
      expect(Math.abs((gy + dy) - moved.top - anchorY), `ghost y at +${dy}`).toBeLessThan(2);
    }

    await page.mouse.up();
    await page.waitForTimeout(200);
    expect(await ghostBox(page), 'the ghost is removed when the drag ends').toBeNull();
  });

  test('the ghost is not mistaken for a real row', async ({ page }) => {
    await gotoApp(page);
    await seedAndOpenList(page);
    const before = await page.locator('.project-row[data-drag-key]').count();
    const box = await page.locator('.project-row[data-drag-key]').nth(1).boundingBox();

    await page.mouse.move(box.x + 40, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + 120, box.y + box.height / 2 - 20);
    await page.waitForTimeout(80);

    // The clone carries no drag key, no ids and not the source's dimming class, so the
    // reorder hit-test and every query over the list still see exactly the real rows.
    expect(await page.locator('.project-row[data-drag-key]').count()).toBe(before);
    expect(await page.locator('.project-dragging').count(), 'one row reads as dragged').toBe(1);
    expect(await page.locator('[data-drag-ghost] [id]').count()).toBe(0);
    await page.mouse.up();
  });
});

test.describe('drag ghost: touch', () => {
  test.use({ viewport: { width: 393, height: 851 }, hasTouch: true, isMobile: true });

  test('a finger gets the same ghost, anchored the same way', async ({ page }) => {
    await gotoApp(page);
    await seedAndOpenList(page);

    const row = page.locator('.project-row[data-drag-key]').nth(1);
    const box = await row.boundingBox();
    const cdp = await page.context().newCDPSession(page);
    const touch = (type, x, y) => cdp.send('Input.dispatchTouchEvent', {
      type, touchPoints: type === 'touchEnd' ? [] : [{ x, y, id: 1 }],
    });

    const gx = box.x + 40, gy = box.y + box.height / 2;
    await touch('touchStart', gx, gy);
    await page.waitForTimeout(400);                 // the 280ms pickup
    const g = await ghostBox(page);
    expect(g, 'the finger drag draws the same ghost element').not.toBeNull();
    for (const axis of ['width', 'height']) {
      const ratio = g[axis] / g.row[axis];
      expect(ratio, `ghost ${axis} vs the row`).toBeGreaterThan(0.95);
      expect(ratio, `ghost ${axis} vs the row`).toBeLessThan(1.1);
    }

    const anchorX = gx - g.left, anchorY = gy - g.top;
    await touch('touchMove', gx, gy - 60);
    await page.waitForTimeout(60);
    const moved = await ghostBox(page);
    expect(Math.abs(gx - moved.left - anchorX), 'anchored at the grab point').toBeLessThan(2);
    expect(Math.abs((gy - 60) - moved.top - anchorY)).toBeLessThan(2);

    await touch('touchEnd', gx, gy - 60);
    await page.waitForTimeout(300);
    expect(await ghostBox(page)).toBeNull();
  });
});
