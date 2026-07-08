// Drag-and-drop smoke: the projects modal's drag-to-reorder and the drag-out zones
// (Open here / Remove) driven through the REAL browser with real HTML5 drag events.
// These exercise projectsModal.js attachRowDrag / performZoneAction end-to-end (no server).
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';

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
  await gotoApp(page);
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
  await gotoApp(page);
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
