// The popup driver for the browser-extension capture: opening a surface in its theme, the popup
// (or side panel) scanned against the demo site and uncapped to its full height, its rows found by
// name, their menus and flyouts, and the hand-off into a new editor tab.
import { applyAppTheme, applyShellTheme } from '../lib/theme/page.mjs';
import { settle, waitForAnimations } from '../lib/waits.mjs';
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
    for (const over of document.querySelectorAll('#action-menu, #action-menu .flyout, .dialog-back .dialog')) {
      const box = over.getBoundingClientRect();
      if (box.height > 0) ends.push(box.bottom);
    }
    return Math.ceil(Math.max(...ends));
  });
  return { clip: { x: 0, y: 0, width: ui.viewportSize().width, height } };
};

// Only the part a shot is about: from the top of the first of `selectors` to the bottom of the
// last (a row and the menu or dialog it opened), `pad` CSS px around it.
export const regionClip = async (ui, selectors, pad = 10) => {
  await waitForAnimations(ui);
  const span = await ui.evaluate((sels) => {
    const boxes = sels.flatMap((s) => [...document.querySelectorAll(s)])
      .map((el) => el.getBoundingClientRect()).filter((b) => b.height > 0);
    return { top: Math.min(...boxes.map((b) => b.top)), bottom: Math.max(...boxes.map((b) => b.bottom)) };
  }, selectors);
  const y = Math.max(0, Math.floor(span.top - pad));
  return { clip: { x: 0, y, width: ui.viewportSize().width, height: Math.ceil(span.bottom + pad) - y } };
};

// The three filter sections folded to their headings, so a menu shot is not mostly filters.
export const foldFilters = async (ui) => {
  await ui.evaluate(() => {
    for (const head of document.querySelectorAll('.fsection:not([id]):not(.collapsed) > .section-head')) head.click();
  });
  await waitForAnimations(ui);
};

// The popup at the 600px a user gets, its list scrolling, instead of the full height above.
export const atRealHeight = (ui, on) =>
  ui.evaluate((real) => { document.getElementById('docs-uncapped').disabled = real; }, on);

export const surface = async (rel, viewport, theme) => {
  const page = await context.newPage();
  await page.setViewportSize(viewport);
  await page.goto(`chrome-extension://${extId}/${rel}`);
  await freezeMotion(page);
  await applyShellTheme(page, theme);
  return page;
};

// A docked side panel's "active tab" is the page beside it. Here the panel is a tab of its own,
// so its tab queries are answered with the demo site, as the docked panel's would be.
export const dockBesideSite = async (panel) => {
  const page = await host.evaluate(() => location.origin + location.pathname);
  await panel.evaluate((prefix) => {
    const query = chrome.tabs.query.bind(chrome.tabs);
    chrome.tabs.query = async (info, ...rest) => {
      if (!info?.active) return query(info, ...rest);
      const site = (await query({})).find((t) => (t.url || '').startsWith(prefix));
      return site ? [site] : query(info, ...rest);
    };
  }, page);
};

// The popup scans the ACTIVE tab: bring the site forward, rescan, then bring the popup back.
export const scanSite = async (ui) => {
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
  const uncapped = await ui.addStyleTag({ content: POPUP_UNCAPPED });
  await uncapped.evaluate((el) => { el.id = 'docs-uncapped'; });
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
  if (ctx.ui && !ctx.ui.isClosed()) await ctx.ui.close();
  ctx.ui = await openPopup(theme);
  return ctx.ui;
};

// The first row whose name reads `name`: the list re-sorts (pinned and opened rows float up),
// so a shot names its image rather than counting rows.
const exactly = (text) => new RegExp(`^\\s*${text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\s*$`);
export const rowNamed = (ui, name) => ui.locator('.row').filter({ has: ui.locator('.name', { hasText: exactly(name) }) }).first();

// The row a shot is about, marked so its clip can be measured with the menu it opened.
export const ROW = '.row[data-docs-row]';
export const markRow = (ui, name) => rowNamed(ui, name).evaluate((el) => {
  document.querySelectorAll('[data-docs-row]').forEach((other) => other.removeAttribute('data-docs-row'));
  el.setAttribute('data-docs-row', '');
});

export const openRowMenu = async (ui, name) => {
  await rowNamed(ui, name).locator('.more-btn').click();
  await ui.locator('#action-menu').waitFor();
  await waitForAnimations(ui, '#action-menu');
};
// CSS :hover alone shows a flyout (display:none otherwise); true once it is on screen, which only
// a shot of it needs — a click on one of its items works either way.
export const openFlyout = async (ui, label) => {
  const sub = ui.locator('#action-menu > .submenu').filter({ hasText: label }).first();
  for (let tries = 0; tries < 3; tries += 1) {
    await sub.hover();
    await waitForAnimations(ui, '#action-menu');
    if (await sub.evaluate((el) => el.matches(':hover')
      && getComputedStyle(el.querySelector('.flyout')).display !== 'none')) return true;
    await settle(150);
  }
  return false;
};
export const clickMenuItem = (ui, text, inFlyout = false) => ui.evaluate(([label, flyout]) => {
  const scope = flyout ? '#action-menu .flyout button' : '#action-menu button';
  [...document.querySelectorAll(scope)].find((btn) => btn.textContent.trim() === label).click();
}, [text, inFlyout]);

// The hand-off routes: the row menu's Open ▸ "In editor" / "In editor (incognito)" open a
// new editor tab carrying the image.
export const openInTab = async (ctx, theme, image, label, name) => {
  const ui = await popup(ctx, theme);
  await host.bringToFront();
  await openRowMenu(ui, image);
  await openFlyout(ui, 'Open');
  const [tab] = await Promise.all([context.waitForEvent('page'), clickMenuItem(ui, label, true)]);
  await tab.setViewportSize(VIEWS.site);
  await tab.waitForFunction(() => document.getElementById('canvas')?.width > 0, null, { timeout: TIMEOUTS.editorMs });
  await freezeMotion(tab);
  await applyAppTheme(tab, theme);
  await runner.shot(tab, name);
  await tab.close();
  await reopenPopup(ctx, theme);
};
