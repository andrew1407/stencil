// The popup driver for the browser-extension capture: opening a surface in its theme, the popup
// scanned against the demo site and uncapped to its full height, its row menu and flyouts, and
// the hand-off into a new editor tab.
import { applyAppTheme, applyShellTheme } from '../lib/theme/page.mjs';
import { waitForAnimations } from '../lib/waits.mjs';
import { MIN_ROWS, TIMEOUTS, VIEWS, context, extId, freezeMotion, host, runner } from './session.mjs';

// A real popup box stops at 600px and scrolls its list; the picture is of what the popup HOLDS,
// so the capture lets it stand at its full height rather than cutting a row in half.
const POPUP_UNCAPPED = `html, body { max-height: none !important; overflow: visible !important; }
  .list { overflow: visible !important; max-height: none !important; }`;

// The shot is the popup and nothing else: the clip ends at its last pixel, so none of the
// viewport left under it rides along.
export const popupClip = async (ui) => {
  await waitForAnimations(ui);
  const height = await ui.evaluate(() => {
    const ends = [document.body.getBoundingClientRect().bottom];
    for (const over of document.querySelectorAll('#action-menu, #action-menu .flyout')) {
      const box = over.getBoundingClientRect();
      if (box.height > 0) ends.push(box.bottom);
    }
    return Math.ceil(Math.max(...ends));
  });
  return { clip: { x: 0, y: 0, width: VIEWS.popup.width, height } };
};

export const surface = async (rel, viewport, theme) => {
  const page = await context.newPage();
  await page.setViewportSize(viewport);
  await page.goto(`chrome-extension://${extId}/${rel}`);
  await freezeMotion(page);
  await applyShellTheme(page, theme);
  return page;
};
// The popup scans the ACTIVE tab: bring the site forward, rescan, then bring the popup back.
const scanSite = async (ui) => {
  await host.bringToFront();
  await ui.evaluate(() => document.getElementById('rescan').click());
  await ui.waitForFunction((min) => document.querySelectorAll('.row').length > min, MIN_ROWS, { timeout: TIMEOUTS.scanMs });
  await ui.bringToFront();
  // The scan's status line leaves on a 200ms timer while freezeMotion has already faded it to
  // nothing, so an unwaited shot keeps its empty band above the first row.
  await ui.waitForFunction(() => !document.getElementById('status').textContent, null, { timeout: TIMEOUTS.scanMs });
  await waitForAnimations(ui);
};
const openPopup = async (theme) => {
  const ui = await surface('src/popup/popup.html', VIEWS.popup, theme);
  await ui.waitForSelector('.filters', { timeout: TIMEOUTS.scanMs });
  await ui.addStyleTag({ content: POPUP_UNCAPPED });
  await scanSite(ui);
  return ui;
};
// A popup closes itself after an in-page action (crop, open here), as the real one does.
export const popup = async (ctx, theme) => {
  ctx.ui ??= await openPopup(theme);
  if (!ctx.ui.isClosed()) await applyShellTheme(ctx.ui, theme);
  return ctx.ui;
};
export const reopenPopup = async (ctx, theme) => {
  ctx.ui = await openPopup(theme);
  return ctx.ui;
};
export const openRowMenu = async (ui, nth = 1) => {
  await ui.locator('.row .more-btn').nth(nth).click();
  await ui.locator('#action-menu').waitFor();
  await waitForAnimations(ui, '#action-menu');
};
export const openFlyout = async (ui, nth = 0) => {
  await ui.locator('#action-menu > .submenu').nth(nth).hover();
  await ui.locator('#action-menu .flyout').nth(nth).waitFor();
  await waitForAnimations(ui, '#action-menu');
};
export const clickMenuItem = (ui, text, inFlyout = false) => ui.evaluate(([label, flyout]) => {
  const scope = flyout ? '#action-menu .flyout button' : '#action-menu button';
  [...document.querySelectorAll(scope)].find((btn) => btn.textContent.trim() === label).click();
}, [text, inFlyout]);

// The hand-off routes: the row menu's Open ▸ "In editor" / "In editor (incognito)" open a
// new editor tab carrying the image.
export const openInTab = async (ctx, theme, label, name) => {
  const ui = await popup(ctx, theme);
  await host.bringToFront();
  await openRowMenu(ui);
  await openFlyout(ui);
  const [tab] = await Promise.all([context.waitForEvent('page'), clickMenuItem(ui, label, true)]);
  await tab.setViewportSize(VIEWS.site);
  await tab.waitForFunction(() => document.getElementById('canvas')?.width > 0, null, { timeout: TIMEOUTS.editorMs });
  await freezeMotion(tab);
  await applyAppTheme(tab, theme);
  await runner.shot(tab, name);
  await tab.close();
  await reopenPopup(ctx, theme);
};
