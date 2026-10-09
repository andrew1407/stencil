// A filter over a list longer than its window (browser/js/ui/motion/enterLeave.js createFilterAnimator)
// plays only the rows in view, each cloud cut at the list's edge (disintegrate `clip`), and the
// window keeps its height whatever the row count (css/components/projects/projects.css).
import { test, expect } from '@playwright/test';
import { gotoApp, seedProjectsAndOpenList } from '../../../helpers/boot.js';

test.use({ viewport: { width: 1280, height: 800 } });

test('a re-sort flies only the rows in view, clipped to the list', async ({ page }) => {
  await gotoApp(page, { motion: 'particles' });
  await seedProjectsAndOpenList(page, { extra: 8 });
  const seen = await page.evaluate(() => {
    const list = document.querySelector('#projects-modal-overlay .settings-body');
    const v = list.getBoundingClientRect();
    const inView = [...list.querySelectorAll('.project-row')]
      .filter((r) => { const b = r.getBoundingClientRect(); return b.bottom > v.top && b.top < v.bottom; }).length;
    const s = /** @type {HTMLSelectElement} */ (document.getElementById('projects-sort'));
    s.value = [...s.options].find((o) => o.value !== s.value).value;
    s.dispatchEvent(new Event('change', { bubbles: true }));
    // Row-sized and inside the window: the window's own and its controls' clouds aside.
    const rowH = list.querySelector('.project-row[data-id]').getBoundingClientRect().height;
    const clouds = [...document.querySelectorAll('.disintegrate-host.dust-forming')]
      .filter((h) => Math.abs(parseFloat(h.style.height) - rowH) < 1);
    return { inView, total: list.querySelectorAll('.project-row').length, clouds: clouds.length,
      clipped: clouds.every((h) => h.style.clipPath.startsWith('inset(')) };
  });
  expect(seen.total, 'the list is longer than its window').toBeGreaterThan(seen.inView);
  expect(seen.clouds).toBeGreaterThan(0);
  expect(seen.clouds, 'no cloud for a row out of view').toBeLessThanOrEqual(seen.inView);
  expect(seen.clipped, 'every cloud is cut at the list edge').toBe(true);
});

test('the window keeps its height when a search empties the list', async ({ page }) => {
  await gotoApp(page, { motion: 'none' });
  await seedProjectsAndOpenList(page);
  const box = page.locator('#projects-modal-overlay .app-modal');
  const before = (await box.boundingBox()).height;
  await page.locator('#projects-search').fill('no project is called this');
  await expect(page.locator('.project-row[data-id]')).toHaveCount(0);
  expect((await box.boundingBox()).height).toBeCloseTo(before, 0);
  expect(before, 'the shared ceiling: 82vh of an 800px viewport').toBeCloseTo(656, 0);
});
