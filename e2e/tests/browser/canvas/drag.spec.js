// The projects modal's drag surfaces, driven with real drag input: reorder plus the drag-out
// zones (projectsModal.js attachRowDrag / performZoneAction), and the translucent copy of the row
// that follows the pointer (ui/dragGhost.js). The ghost is an element the app positions itself,
// not the browser's `setDragImage` snapshot — which rasterized at the device scale on HiDPI and
// was unreachable from the DOM. Right size, right place, gone afterwards, on mouse and finger.
import { test, expect } from '@playwright/test';
import { gotoApp, seedProjectsAndOpenList } from '../../../helpers/boot.js';
import { finger, ghostBox, spyOnDragImage } from '../../../helpers/drag.js';

// Create N distinct saved local projects via the facade (each blank auto-saves; newEditor
// starts a fresh one), then open the Projects modal and wait for the draggable rows.
async function seedProjects(page, colors) {
  await page.evaluate(async (cols) => {
    for (let i = 0; i < cols.length; i++) {
      if (i > 0) window.stencil.newEditor();
      await window.stencil.blank(cols[i], { size: { width: 200, height: 150 } });
    }
  }, colors);
  await page.locator('#projects-btn').click();
  const rows = page.locator('.project-row[draggable="true"]');
  await expect(rows).toHaveCount(colors.length, { timeout: 5000 });
  return rows;
}

test('projects: drag-to-reorder switches to manual order and moves the row', async ({ page }) => {
  await gotoApp(page, { motion: 'none' });
  const rows = await seedProjects(page, ['#ffffff', '#000000', '#ff8800']);

  const firstKey = await rows.first().getAttribute('data-drag-key');
  // Drag the first draggable row onto the third → a manual reorder is persisted.
  await rows.first().dragTo(rows.nth(2));

  // The sort selector flips to "manual" once a manual drop is persisted (persistManualDrop).
  await expect(page.locator('#projects-sort')).toHaveValue('manual');
  // The dragged row is no longer first (it moved down past the drop target).
  await expect(page.locator('.project-row[draggable="true"]').first())
    .not.toHaveAttribute('data-drag-key', firstKey);
});

test('projects: drag a row to the Remove zone deletes it (after confirm)', async ({ page }) => {
  await gotoApp(page, { motion: 'none' });
  const rows = await seedProjects(page, ['#ffffff', '#000000']);
  const countBefore = await rows.count();

  // Real HTML5 drag to the bottom-centre Remove zone (position-based, outside the card): drop on
  // the full-viewport overlay at a point in the bottom band → performZoneAction('remove').
  const overlay = page.locator('#projects-modal-overlay');
  const ob = await overlay.boundingBox();
  await rows.first().dragTo(overlay, { targetPosition: { x: ob.width / 2, y: ob.height - 12 } });

  // The Remove zone raises a Yes/No confirm; confirming removes the row.
  const confirm = page.locator('#confirm-modal-overlay.modal-open');
  await expect(confirm).toBeVisible({ timeout: 3000 });
  await confirm.getByRole('button', { name: /^yes$/i }).click();
  await expect(page.locator('.project-row[draggable="true"]')).toHaveCount(countBefore - 1);
});

// Three saved projects (helpers/boot.js), so nth(1) is a draggable middle row.
const seedAndOpenList = (page) => seedProjectsAndOpenList(page, { extra: 1 });

test.describe('drag ghost: mouse', () => {
  test.use({ viewport: { width: 1400, height: 900 } });

  test('is row-sized, stays under the cursor, and is cleaned up', async ({ page }) => {
    await spyOnDragImage(page);
    await gotoApp(page, { motion: 'none' });
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
    // Same size as the row it copies (bar the 1.02 lift), not a rescaled snapshot. The bounds are
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

    // The anchor is read off the ghost itself (the list settles by a pixel between measurements);
    // what must hold is that the offset does not DRIFT as the drag goes on.
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
    await gotoApp(page, { motion: 'none' });
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
    await gotoApp(page, { motion: 'none' });
    await seedAndOpenList(page);

    const row = page.locator('.project-row[data-drag-key]').nth(1);
    const box = await row.boundingBox();
    const touch = await finger(page);
    const gx = box.x + 40, gy = box.y + box.height / 2;
    await touch.down(gx, gy);
    await page.waitForTimeout(400);                 // the 280ms pickup
    const g = await ghostBox(page);
    expect(g, 'the finger drag draws the same ghost element').not.toBeNull();
    for (const axis of ['width', 'height']) {
      const ratio = g[axis] / g.row[axis];
      expect(ratio, `ghost ${axis} vs the row`).toBeGreaterThan(0.95);
      expect(ratio, `ghost ${axis} vs the row`).toBeLessThan(1.1);
    }

    const anchorX = gx - g.left, anchorY = gy - g.top;
    await touch.move(gx, gy - 60);
    await page.waitForTimeout(60);
    const moved = await ghostBox(page);
    expect(Math.abs(gx - moved.left - anchorX), 'anchored at the grab point').toBeLessThan(2);
    expect(Math.abs((gy - 60) - moved.top - anchorY)).toBeLessThan(2);

    await touch.up(gx, gy - 60);
    await page.waitForTimeout(300);
    expect(await ghostBox(page)).toBeNull();
  });
});
