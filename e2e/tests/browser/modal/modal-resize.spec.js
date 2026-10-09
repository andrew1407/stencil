// A window dragged narrower by its edge (browser/js/ui/modal/resize.js) re-lays its content for
// its own width (container `modal`, css/components/modalShell.css), as a phone or the popover does:
// at the narrowest a drag reaches, nothing inside runs past the window's edge.
import { test, expect } from '@playwright/test';
import { gotoApp, seedProjectsAndOpenList, settleModalAnimations } from '../../../helpers/boot.js';

test.use({ viewport: { width: 1280, height: 800 } });

const byButton = (btn, overlay) => async (page) => {
  await gotoApp(page, { motion: 'none' });
  await page.locator(btn).click();
  await expect(page.locator(`#${overlay}`)).toHaveClass(/modal-open/);
  await settleModalAnimations(page, overlay);
};

const WINDOWS = [
  ['projects', 'projects-modal-overlay', async (page) => {
    await gotoApp(page, { motion: 'none' });
    await seedProjectsAndOpenList(page);
  }],
  ['script', 'script-overlay', byButton('#script-btn', 'script-overlay')],
  ['visuals', 'visuals-modal-overlay', byButton('#visuals-btn', 'visuals-modal-overlay')],
  ['settings', 'settings-modal-overlay', byButton('#settings-btn', 'settings-modal-overlay')],
];

// Every drawn element inside the box whose edge leaves the box (a horizontal scroller's rows aside).
const spilling = (page, overlayId) => page.evaluate((oid) => {
  const box = document.querySelector(`#${oid} :is(.app-modal, #settings-modal)`);
  const b = box.getBoundingClientRect();
  const scrolls = (el) => {
    for (let a = el.parentElement; a && a !== box; a = a.parentElement)
      if (/auto|scroll/.test(getComputedStyle(a).overflowX)) return true;
    return false;
  };
  return [...box.querySelectorAll('*')].filter((el) => {
    const r = el.getBoundingClientRect();
    if (!r.width || !r.height || getComputedStyle(el).position === 'fixed' || scrolls(el)) return false;
    return r.right > b.right + 1 || r.left < b.left - 1;
  }).map((el) => `${el.tagName.toLowerCase()}${el.id ? `#${el.id}` : ''}.${el.className?.baseVal ?? el.className}`);
}, overlayId);

for (const [name, overlay, open] of WINDOWS) {
  test(`the ${name} window, dragged to its narrowest, keeps everything inside its edge`, async ({ page }) => {
    await open(page);
    const box = page.locator(`#${overlay} :is(.app-modal, #settings-modal)`);
    const before = await box.boundingBox();
    // The right edge's band, dragged far past the floor: the window stops at its minimum.
    const y = before.y + before.height / 2;
    await page.mouse.move(before.x + before.width - 2, y);
    await page.mouse.down();
    await page.mouse.move(before.x + 40, y, { steps: 8 });
    await page.mouse.up();
    const after = await box.boundingBox();
    expect(after.width, 'the drag reached the floor').toBeLessThan(before.width - 150);
    expect(await spilling(page, overlay)).toEqual([]);
  });
}

test('a narrowed projects window wraps its filters and its footer hint', async ({ page }) => {
  await gotoApp(page, { motion: 'none' });
  await seedProjectsAndOpenList(page);
  const box = page.locator('#projects-modal-overlay .app-modal');
  const before = await box.boundingBox();
  const y = before.y + before.height / 2;
  await page.mouse.move(before.x + before.width - 2, y);
  await page.mouse.down();
  await page.mouse.move(before.x + 40, y, { steps: 8 });
  await page.mouse.up();
  const tops = await page.locator('.projects-filter-row > *').evaluateAll((els) => els.map((e) => e.getBoundingClientRect().top));
  expect(new Set(tops.map(Math.round)).size, 'the filters take more than one row').toBeGreaterThan(1);
  const hint = await page.locator('#projects-modal-overlay .footer-hint').boundingBox();
  const footer = await page.locator('#projects-modal-overlay .settings-footer').boundingBox();
  expect(hint.width, 'the hint takes the whole footer line').toBeGreaterThan(footer.width - 60);
});
