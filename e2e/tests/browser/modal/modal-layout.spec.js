// Phone-width layout of the app's two big dialogs — the Projects list (js/ui/projectsModal.js)
// and the Settings / keyboard-shortcuts sheet — plus the 680px breakpoint that keeps the narrow
// rules off the desktop ones. An .app-modal clips, so overflow never shows in the page's
// scrollWidth: these measure the CONTROLS instead.
import { test, expect } from '@playwright/test';
import { gotoApp, seedProjectsAndOpenList, settleModalAnimations } from '../../../helpers/boot.js';

const PHONE = { viewport: { width: 393, height: 851 }, hasTouch: true, isMobile: true };   // Pixel 5
const DESKTOP = { viewport: { width: 1280, height: 800 } };

// Every element inside the overlay whose right/left edge escapes the viewport.
const overflowingInModal = (page, overlayId) => page.evaluate((oid) => {
  const root = document.getElementById(oid);
  const out = [];
  for (const el of root.querySelectorAll('*')) {
    const r = el.getBoundingClientRect();
    if (!r.width || !r.height) continue;                       // hidden / zero-box
    if (r.right > window.innerWidth + 0.5 || r.left < -0.5)
      out.push(`${el.tagName.toLowerCase()}${el.id ? '#' + el.id : ''}.${el.className || ''} [${r.left.toFixed(1)}…${r.right.toFixed(1)}]`);
  }
  return out;
}, overlayId);

// Open the Settings sheet on a booted app. Motion off (the dialog's flight from its icon
// would scale the column widths read mid-flight), so nothing here has an entrance to wait.
async function openSettings(page) {
  await gotoApp(page, { motion: 'none' });
  await page.locator('#settings-btn').click();
  await expect(page.locator('#settings-modal-overlay')).toHaveClass(/modal-open/);
  await expect(page.locator('#hotkey-table .hotkey-row').first()).toBeVisible();
  await settleModalAnimations(page, 'settings-modal-overlay');
}

async function openProjects(page) {
  await gotoApp(page, { motion: 'none' });
  await seedProjectsAndOpenList(page);
}

const MODALS = [
  ['projects', 'projects-modal-overlay', openProjects],
  ['settings', 'settings-modal-overlay', openSettings],
];

// ── The one assertion both dialogs owe a phone: everything they draw is on screen. ──
for (const [name, overlay, open] of MODALS) {
  test.describe(`${name} modal: phone layout`, () => {
    test.use(PHONE);

    test(`the ${name} dialog fits the screen — nothing clipped, nothing parked aside`, async ({ page }) => {
      await open(page);
      const doc = await page.evaluate(() => ({
        scrollWidth: document.documentElement.scrollWidth,
        bodyScrollWidth: document.body.scrollWidth,
        innerWidth: window.innerWidth,
      }));
      // The page itself never gains a horizontal scroll…
      expect(doc.scrollWidth).toBeLessThanOrEqual(doc.innerWidth);
      expect(doc.bodyScrollWidth).toBeLessThanOrEqual(doc.innerWidth);
      // …and neither does anything inside the dialog (the real assertion: the modal
      // clips, so an off-screen control is invisible, not scrollable).
      expect(await overflowingInModal(page, overlay),
        `no element inside the ${name} modal escapes the viewport`).toEqual([]);
    });
  });
}

// ── Projects: the filter row, the footer and the drag-out zones. ──
const FILTER_CONTROLS = ['#projects-search', '#projects-filter', '#projects-sort', '#projects-search-mode'];

// Selects wear the app's own dropdown (customSelect.js): the native control is hidden, so
// measure `.accent-dd-trigger`. Inputs are their own visible control.
const boxes = (page, sels) => page.evaluate((list) => Object.fromEntries(list.map((s) => {
  const el = document.querySelector(s);
  const vis = (el.dataset.csEnhanced && el.closest('.cs-dd')?.querySelector('.accent-dd-trigger')) || el;
  const b = vis.getBoundingClientRect();
  return [s, { left: +b.left.toFixed(1), right: +b.right.toFixed(1), top: +b.top.toFixed(1), width: +b.width.toFixed(1), height: +b.height.toFixed(1) }];
})), sels);

test.describe('projects modal: phone layout', () => {
  test.use(PHONE);

  test('every filter control is on screen, usable, and the footer keeps its line', async ({ page }) => {
    await openProjects(page);
    const w = page.viewportSize().width;
    const card = await page.locator('#projects-modal-overlay .app-modal').boundingBox();
    const b = await boxes(page, FILTER_CONTROLS);
    for (const sel of FILTER_CONTROLS) {
      expect(b[sel].left, `${sel} left edge on screen`).toBeGreaterThanOrEqual(0);
      expect(b[sel].right, `${sel} right edge on screen`).toBeLessThanOrEqual(w);
      expect(b[sel].right, `${sel} is inside the dialog card, not clipped by it`)
        .toBeLessThanOrEqual(card.x + card.width + 0.5);
      // Custom-select pill triggers are label-driven and narrower than the old native selects, so the
      // floor guards the collapse (the search box once ended up ~25px wide), not the chrome.
      expect(b[sel].width, `${sel} is wide enough to use`).toBeGreaterThan(64);
      // The app's touch convention: ≥40px tap targets on a coarse pointer.
      expect(b[sel].height, `${sel} tap target`).toBeGreaterThanOrEqual(40);
    }
    // The row really did wrap onto multiple lines — that's how it fits.
    const rows = new Set(FILTER_CONTROLS.map((s) => b[s].top));
    expect(rows.size, 'the filters row wraps on a phone').toBeGreaterThan(1);

    // The footer note gets its own line instead of wrapping one word per line, and the
    // three action buttons stay on screen beside/below it.
    const foot = await boxes(page, ['#projects-modal-overlay .footer-hint', '#projects-blank-image', '#projects-new-editor', '#projects-clear-all']);
    const hint = foot['#projects-modal-overlay .footer-hint'];
    expect(hint.width, 'the footer hint spans the footer').toBeGreaterThan(w * 0.7);
    expect(hint.height, 'the footer hint is one or two lines, not one word per line').toBeLessThan(45);
    for (const sel of ['#projects-blank-image', '#projects-new-editor', '#projects-clear-all']) {
      expect(foot[sel].right, `${sel} on screen`).toBeLessThanOrEqual(w);
      expect(foot[sel].left, `${sel} on screen`).toBeGreaterThanOrEqual(0);
    }
  });

  test('the drag-out drop zones stay inside the viewport', async ({ page }) => {
    await openProjects(page);
    const zones = await page.evaluate(() => {
      const overlay = document.getElementById('projects-modal-overlay');
      const row = overlay.querySelector('.project-row[data-id]');
      row.dispatchEvent(new DragEvent('dragstart', { bubbles: true, dataTransfer: new DataTransfer() }));
      const el = overlay.querySelector('.project-dropzones');
      if (!el) return null;
      el.classList.add('is-dragging');
      const card = overlay.querySelector('.app-modal').getBoundingClientRect();
      return [...el.querySelectorAll('.pdz')].map((z) => {
        const b = z.getBoundingClientRect();
        const l = z.querySelector('.pdz-label').getBoundingClientRect();
        return {
          action: z.dataset.action, left: +b.left.toFixed(1), right: +b.right.toFixed(1),
          // Is the label in the strip the card leaves exposed, rather than behind it?
          labelClear: l.bottom <= card.top + 0.5 || l.top >= card.bottom - 0.5,
        };
      });
    });
    expect(zones, 'the drag-out zones exist once a row drag starts').not.toBeNull();
    const w = page.viewportSize().width;
    for (const z of zones) {
      expect(z.left, `${z.action} zone left edge`).toBeGreaterThanOrEqual(0);
      expect(z.right, `${z.action} zone right edge`).toBeLessThanOrEqual(w);
      // The dialog is near full-bleed on a phone: a label laid out behind it is invisible,
      // so the zone reads as an unlabelled band you can't tell apart from its neighbour.
      expect(z.labelClear, `${z.action} label clears the dialog card`).toBe(true);
    }
  });
});

// ── Settings: the hotkey table's four columns. ──
// Table geometry + whether its scroller has anything hidden to the side.
const tableBox = (page) => page.evaluate(() => {
  const body = document.querySelector('.settings-body');
  const table = document.getElementById('hotkey-table');
  const b = table.getBoundingClientRect();
  return {
    right: b.right, width: b.width,
    scrollW: body.scrollWidth, clientW: body.clientWidth,
    heads: [...table.querySelectorAll('.hotkey-head > span')].map((th) => {
      const r = th.getBoundingClientRect();
      return { text: th.textContent.trim(), left: r.left, right: r.right, width: r.width };
    }),
  };
});

test.describe('settings modal: phone layout', () => {
  test.use(PHONE);

  test('the whole hotkey table fits — no sideways scrolling to reach a column', async ({ page }) => {
    await openSettings(page);
    const t = await tableBox(page);
    const w = page.viewportSize().width;

    expect(t.scrollW, 'the table scroller has nothing parked off to the side').toBe(t.clientW);
    expect(t.right, 'the table ends inside the screen').toBeLessThanOrEqual(w);
    // Every column — including Default and the reset-button column — is on screen and real.
    expect(t.heads.map((h) => h.text)).toEqual(['Action', 'Current shortcut', 'Default', '']);
    for (const h of t.heads) {
      expect(h.left, `${h.text || 'reset'} column starts on screen`).toBeGreaterThanOrEqual(0);
      expect(h.right, `${h.text || 'reset'} column ends on screen`).toBeLessThanOrEqual(w);
      expect(h.width, `${h.text || 'reset'} column has width`).toBeGreaterThan(20);
    }
  });
});

// ── The narrow rules are breakpoint-gated (max-width: 680px): the desktop dialogs keep
// the roomy chrome they had before. ──
test.describe('modal layout: desktop is unaffected', () => {
  test.use(DESKTOP);

  test('the projects filters stay on a single row at 1280px', async ({ page }) => {
    await openProjects(page);
    const b = await boxes(page, FILTER_CONTROLS);
    // The desktop dialog stacks the search field above `.projects-filter-row`, and the three selects
    // share ONE row: only the phone breakpoint may wrap them onto more.
    const selectTops = FILTER_CONTROLS.slice(1).map((s) => b[s].top);
    expect(Math.max(...selectTops) - Math.min(...selectTops), 'one row of filters').toBeLessThanOrEqual(2);
    expect(b['#projects-search'].top, 'the search bar sits above the filters').toBeLessThan(Math.min(...selectTops));
    const card = await page.locator('#projects-modal-overlay .app-modal').boundingBox();
    expect(card.width, 'the desktop dialog keeps its 560px width').toBe(560);
    // The footer hint still sits BESIDE the buttons (one row), as on desktop before.
    const foot = await boxes(page, ['#projects-modal-overlay .footer-hint', '#projects-clear-all']);
    expect(Math.abs(foot['#projects-modal-overlay .footer-hint'].top - foot['#projects-clear-all'].top))
      .toBeLessThanOrEqual(4);
    expect(await overflowingInModal(page, 'projects-modal-overlay')).toEqual([]);
  });

  test('the hotkey table keeps its full-size columns at 1280px', async ({ page }) => {
    await openSettings(page);
    const t = await tableBox(page);
    expect(t.scrollW).toBe(t.clientW);
    // The combo cell's 110px floor still applies here, so the Shortcut column stays wide.
    const shortcut = t.heads.find((h) => h.text === 'Current shortcut');
    expect(shortcut.width, 'Shortcut column keeps its desktop width').toBeGreaterThan(120);
    expect(await page.evaluate(() => getComputedStyle(document.querySelector('.hotkey-cell')).minWidth))
      .toBe('110px');
  });
});
