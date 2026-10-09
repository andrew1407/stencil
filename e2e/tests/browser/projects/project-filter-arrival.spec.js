// A row a filter switch drops dissolves where it sat, and one it brings back (browser/js/ui/motion/
// enterLeave.js createFilterAnimator) is formed by its motes: hidden while they gather, whole once
// they land — never shown first with the cloud still flying into it. The temporary row is the one
// "Not open elsewhere" drops.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../../helpers/boot.js';

test('the temporary row dissolves when dropped, then returns under its motes', async ({ page }) => {
  await gotoApp(page, { motion: 'particles' });
  await page.locator('#projects-btn').click();
  await expect(page.locator('#projects-modal-overlay')).toHaveClass(/modal-open/);
  await expect(page.locator('.project-row.project-temp')).toBeVisible();
  // Dropped: gone from the list at once, its cloud starting from the very box it had.
  const leave = await page.evaluate(() => {
    const was = document.querySelector('.project-row.project-temp').getBoundingClientRect();
    const s = /** @type {HTMLSelectElement} */ (document.getElementById('projects-filter'));
    s.value = 'peer-closed';
    s.dispatchEvent(new Event('change', { bubbles: true }));
    const cloud = [...document.querySelectorAll('.disintegrate-host')].map((h) => h.getBoundingClientRect())
      .find((r) => Math.abs(r.top - was.top) < 1 && Math.abs(r.left - was.left) < 1 && Math.abs(r.height - was.height) < 1);
    return { gone: !document.querySelector('.project-row.project-temp'), cloud: !!cloud };
  });
  expect(leave.gone, 'the row leaves the list at once').toBe(true);
  expect(leave.cloud, 'a cloud scatters from where it sat').toBe(true);
  await expect(page.locator('.project-row.project-temp')).toHaveCount(0);
  await page.waitForTimeout(1500);   // the switch's own arrivals settle

  // Sampled inside the frame of the switch: the row exists, its motes are flying, it is not seen.
  const early = await page.evaluate(async () => {
    const s = /** @type {HTMLSelectElement} */ (document.getElementById('projects-filter'));
    s.value = 'all';
    s.dispatchEvent(new Event('change', { bubbles: true }));
    const seen = [];
    const t0 = performance.now();
    while (performance.now() - t0 < 200) {
      const row = document.querySelector('.project-row.project-temp');
      seen.push({ opacity: row ? Number(getComputedStyle(row).opacity) : -1,
        motes: document.querySelectorAll('.disintegrate-host').length });
      await new Promise((r) => requestAnimationFrame(r));
    }
    return seen;
  });
  expect(early.length).toBeGreaterThan(3);
  for (const s of early) {
    expect(s.opacity, 'hidden while its motes gather').toBe(0);
    expect(s.motes, 'a cloud is forming it').toBeGreaterThan(0);
  }
  await expect.poll(() => page.locator('.project-row.project-temp')
    .evaluate((el) => Number(getComputedStyle(el).opacity)), { timeout: 3000 }).toBeCloseTo(0.92, 2);
});
