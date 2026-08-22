// Every <select> in the app wears the app's OWN dropdown (js/ui/customSelect.js), not the
// OS one: macOS draws the native popup itself — centred over the control, in its own
// palette — so a toolbar of themed controls answered a click with a system menu. The
// desktop app makes the same swap (support/searchCombo.hpp), and the exception on both
// sides is the ZOOM control, which is a number field with a preset list attached.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';

test('every selector opens the app dropdown, and hovers as a pointer', async ({ page }) => {
  await gotoApp(page);

  // Not one native popup left anywhere in the app — modals included; they are all in the
  // DOM from boot, so a single pass at wiring time reaches them.
  const selects = await page.evaluate(() =>
    [...document.querySelectorAll('select')].map((s) => ({
      id: s.id,
      enhanced: !!s.dataset.csEnhanced,
      hidden: getComputedStyle(s).display === 'none',
      trigger: !!s.parentElement?.querySelector('.accent-dd-trigger'),
    })));
  expect(selects.length).toBeGreaterThan(10);
  expect(selects.filter((s) => !s.enhanced || !s.trigger || !s.hidden)).toEqual([]);

  // The trigger is the control now: a pointer while it is live, and the same
  // not-allowed the rest of the toolbar shows once its select goes dead.
  const style = await page.evaluate(() => {
    const trig = (id) => document.getElementById(id).parentElement.querySelector('.accent-dd-trigger');
    return {
      live: getComputedStyle(trig('line-style')).cursor,
      deadCursor: getComputedStyle(trig('image-filter')).cursor,   // no image loaded yet
      deadDisabled: trig('image-filter').disabled,
    };
  });
  expect(style.live).toBe('pointer');
  expect(style.deadDisabled).toBe(true);
  expect(style.deadCursor).toBe('not-allowed');

  // Opening one shows the app's own list, and picking a row drives the native select
  // (still the source of truth) so every existing change handler fires.
  await page.locator('#line-style').locator('xpath=..').locator('.accent-dd-trigger').click();
  const menu = page.locator('.accent-dd-menu:visible');
  await expect(menu).toBeVisible();
  await expect(menu.locator('.accent-dd-opt')).toHaveText(['Solid', 'Dashed', 'Dotted']);
  await menu.locator('.accent-dd-opt', { hasText: 'Dashed' }).click();
  await expect(menu).toBeHidden();
  expect(await page.evaluate(() => document.getElementById('line-style').value)).toBe('dashed');
  expect(await page.evaluate(() => window.stencil.lineStyle)).toBe('dashed');

  // A dead selector opens nothing at all.
  await page.locator('#image-filter').locator('xpath=..').locator('.accent-dd-trigger')
    .click({ force: true });
  await expect(page.locator('.accent-dd-menu:visible')).toHaveCount(0);
});
