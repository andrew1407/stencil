// Opening a project from the projects list by mouse and keyboard (browser/js/ui/projects/window/projectsModal.js):
//   click → confirm modal, this tab              dblclick → open now, no modal
//   ⌘/Ctrl+click → confirm, NEW TAB              ⌘/Ctrl+dblclick → new tab now, no modal
// Driven through the real browser: the app's shared confirm dialog, and a real second page.
import { test, expect } from '@playwright/test';
import { gotoApp, seedProjectsAndOpenList } from '../../../helpers/boot.js';

// Two saved local projects, then the Projects modal open (helpers/boot.js). Returns the
// row of the project that is NOT active, so every gesture has something real to switch to.
const seedAndOpenList = (page, extra = 0) => seedProjectsAndOpenList(page, { extra });

const confirmModal = (page) => page.locator('#confirm-modal-overlay');
const activeId = (page) => page.evaluate(() => window.stencil.current?.id ?? null);

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
