// UI regression pins for the browser app — the guard for a refactor that moves modules and
// splits stylesheets. Each state is driven through the same seams the other specs use, then its
// subtree is recorded as computed styles + DOM shape and deep-equalled against
// e2e/pins/<name>.json (helpers/uiPin.js). Order inside a group is load-bearing: the empty
// editor before the image, the toolbar before anything enables its image actions.
import { test, expect } from '@playwright/test';
import { gotoApp, settleModalAnimations, expectModalOpen } from '../../helpers/boot.js';
import { expectPin, freezeMotion } from '../../helpers/uiPin.js';

// A known two-line layout: every coordinate in the points table and every swatch in the
// selection panel is then a value this file chose, not one the app happened to pick.
const LINES = [
  { points: [{ x: 20, y: 20 }, { x: 120, y: 60 }, { x: 60, y: 140 }], color: '#c81e1e', thickness: 3, style: 'solid' },
  { points: [{ x: 150, y: 30 }, { x: 190, y: 170 }], color: '#1e63c8', thickness: 2, style: 'dashed' },
];

const boot = async (page) => {
  await gotoApp(page);
  await freezeMotion(page);
  return page;
};

test.describe('browser UI pins', () => {
  test('editor chrome: toolbar, idle card, context menu, panels, toast', async ({ page }) => {
    await boot(page);

    await expectPin(page, { name: 'toolbar-default', root: '.controls-wrapper' });
    await expectPin(page, { name: 'canvas-empty-idle-card', root: '#idle-create-wrap' });

    // The canvas is 0x0 until an image exists, so this comes after the blank — but before the lines,
    // so no vertex is ever under the pointer and the menu is the plain canvas one.
    await page.evaluate(() => window.stencil.blank('#ffffff', { size: { width: 240, height: 200 } }));
    await page.locator('#canvas').click({ button: 'right', position: { x: 40, y: 40 } });
    await expect(page.locator('#ctx-menu')).toHaveClass(/ctx-open/);
    await expectPin(page, { name: 'context-menu-open', root: '#ctx-menu' });
    await page.keyboard.press('Escape');

    // The fixed layout above; the Lines tab's first row selects a line, which is what
    // raises the selection panel.
    await page.evaluate((lines) => window.stencil.setLines(lines, { history: false }), LINES);
    await page.locator('#coord-tab-lines').click();
    await page.locator('#lines-list .lines-row').first().click();
    await expect(page.locator('#selection-panel')).toBeVisible();
    await expectPin(page, { name: 'selection-panel-with-line', root: '#selection-panel' });

    // Back to Points: the selected line's vertices, in the table.
    await page.locator('#coord-tab-points').click();
    await expect(page.locator('#coordinates-body tr').first()).toBeVisible();
    await expectPin(page, { name: 'coord-table-with-points', root: '#coord-panel' });

    // Wait the editor's own "Saved" toast out first so the stack holds exactly this one message;
    // toasts auto-hide after 2.4s.
    const toasts = page.locator('#notify-balloon .notify-toast');
    await expect(toasts).toHaveCount(0, { timeout: 10_000 });
    await page.evaluate(() => document.getElementById('notify-balloon').notify('Pinned toast', 'ok'));
    await expect(toasts).toHaveCount(1);
    await expectPin(page, { name: 'toast-notification', root: '#notify-balloon' });
  });

  test('modals: projects, shortcuts, visuals + accent picker, servers, open image', async ({ page }) => {
    await boot(page);

    // Modals are opened through the facade's named openers — the app's real path, reachable even for
    // image-only controls. Projects is pinned EMPTY: names and timestamps would be generated.
    const openModal = async (opener, overlay) => {
      await page.evaluate((fn) => window.stencil[fn](), opener);
      await expectModalOpen(page, overlay);
      await settleModalAnimations(page, overlay);
    };
    const closeModal = async (overlay) => {
      await page.evaluate(() => window.stencil.closeWindow());
      await expect(page.locator(`#${overlay}`)).not.toHaveClass(/modal-open/);
    };

    for (const [name, opener, overlay] of [
      ['projects-modal', 'openProjectsWindow', 'projects-modal-overlay'],
      ['connect-modal', 'openServersWindow', 'connect-modal-overlay'],
      ['open-image-modal', 'openImageWindow', 'open-image-modal-overlay'],
    ]) {
      await openModal(opener, overlay);
      await expectPin(page, { name, root: `#${overlay}` });
      await closeModal(overlay);
    }

    // Shortcuts: pinned with the search filtered, so the pin covers the modal's chrome
    // and a handful of rows instead of the whole ~120-row hotkey table.
    await openModal('openShortcutsWindow', 'settings-modal-overlay');
    await page.locator('#hotkey-search').fill('zoom');
    await expectPin(page, { name: 'settings-modal', root: '#settings-modal-overlay' });
    await closeModal('settings-modal-overlay');

    // Visuals, plus the accent dropdown it hosts — an open .accent-dd-menu is portaled
    // to <body> (dropdownMenu.js showMenu), so it is pinned there, not under the modal.
    await openModal('openVisualsWindow', 'visuals-modal-overlay');
    await expectPin(page, { name: 'visuals-modal', root: '#visuals-modal-overlay' });
    await page.locator('#vs-accent .accent-dd-trigger').click();
    await expect(page.locator('body > .accent-dd-menu')).toBeVisible();
    await expectPin(page, { name: 'accent-picker-open', root: 'body > .accent-dd-menu' });
    await page.keyboard.press('Escape');
    await closeModal('visuals-modal-overlay');
  });

  test('chat panel placements and the fullscreen control strip', async ({ page }) => {
    await boot(page);

    await page.evaluate(() => window.stencil.chat.open().chat.dock('right'));
    await expect(page.locator('#chat-panel')).toHaveClass(/chat-open/);
    await expectPin(page, { name: 'chat-panel-docked-right', root: '#chat-panel' });

    await page.evaluate(() => window.stencil.chat.dock('float'));
    await expectPin(page, { name: 'chat-panel-floating', root: '#chat-panel' });
    await page.evaluate(() => window.stencil.chat.close());

    // Fullscreen is a CSS mode (no Fullscreen API): the top trigger zone reveals the
    // cloned control strip on hover, and hovering the strip itself cancels its auto-hide.
    await page.evaluate(() => { window.stencil.fullscreen = true; });
    const strip = page.locator('#fs-controls-panel');
    await page.mouse.move(400, 2);
    await expect(strip).toHaveClass(/fs-panel-visible/);
    const box = await strip.boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await expectPin(page, { name: 'fullscreen-strip', root: '#fs-controls-panel' });
    await page.evaluate(() => { window.stencil.fullscreen = false; });
  });
});
