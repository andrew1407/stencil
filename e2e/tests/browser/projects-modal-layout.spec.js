// The Projects modal on a phone-sized viewport (browser/js/ui/projectsModal.js +
// its CSS in browser/css/components.css).
//
// Regression: the modal's filter row and footer were single, NON-wrapping flex rows
// sized for the 560px desktop dialog. At a Pixel-5 width (393px) that put the third
// select ("Name + keywords") ~45px past the right edge of the screen — laid out at
// right=437.7 with innerWidth=393, so it was clipped away entirely by .app-modal's
// `overflow: hidden` and simply unreachable — collapsed the search box to ~25px, and
// squeezed the footer hint to ~52px wide beside its three buttons, wrapping it one
// word per line (108px tall). Nothing showed up in the page's scrollWidth precisely
// because the modal clips, so this spec measures the CONTROLS, not just the document.
import { test, expect } from '@playwright/test';
import { gotoApp, seedProjectsAndOpenList } from '../../helpers/boot.js';

// Two saved projects, then the Projects modal open and settled (the modalPop scale
// animation is 0.3s — measuring during it reports ~97% of the real geometry).
const seedAndOpenList = (page) => seedProjectsAndOpenList(page, { settleMs: 500 });

// Every element inside the modal whose right/left edge escapes the viewport.
const overflowingInModal = (page) => page.evaluate(() => {
  const root = document.getElementById('projects-modal-overlay');
  const out = [];
  for (const el of root.querySelectorAll('*')) {
    const r = el.getBoundingClientRect();
    if (!r.width || !r.height) continue;                       // hidden / zero-box
    if (r.right > window.innerWidth + 0.5 || r.left < -0.5)
      out.push(`${el.tagName.toLowerCase()}${el.id ? '#' + el.id : ''}.${el.className || ''} [${r.left.toFixed(1)}…${r.right.toFixed(1)}]`);
  }
  return out;
});

const FILTER_CONTROLS = ['#projects-search', '#projects-filter', '#projects-sort', '#projects-search-mode'];

// Selects wear the app's own dropdown (customSelect.js): the native control is hidden
// and `.accent-dd-trigger` is what the user sees and taps — measure THAT. Inputs (the
// search box) are their own visible control and measure as themselves.
const boxes = (page, sels) => page.evaluate((list) => Object.fromEntries(list.map((s) => {
  const el = document.querySelector(s);
  const vis = (el.dataset.csEnhanced && el.closest('.cs-dd')?.querySelector('.accent-dd-trigger')) || el;
  const b = vis.getBoundingClientRect();
  return [s, { left: +b.left.toFixed(1), right: +b.right.toFixed(1), top: +b.top.toFixed(1), width: +b.width.toFixed(1), height: +b.height.toFixed(1) }];
})), sels);

test.describe('projects modal: phone layout', () => {
  test.use({ viewport: { width: 393, height: 851 }, hasTouch: true, isMobile: true });   // Pixel 5

  test('nothing overflows horizontally and every filter control is on screen', async ({ page }) => {
    await gotoApp(page);
    await seedAndOpenList(page);

    // 1. The page itself never gains a horizontal scroll.
    const doc = await page.evaluate(() => ({
      scrollWidth: document.documentElement.scrollWidth,
      bodyScrollWidth: document.body.scrollWidth,
      innerWidth: window.innerWidth,
    }));
    expect(doc.scrollWidth).toBeLessThanOrEqual(doc.innerWidth);
    expect(doc.bodyScrollWidth).toBeLessThanOrEqual(doc.innerWidth);

    // 2. …and neither does anything inside the dialog (this is the real assertion:
    //    the modal clips, so an off-screen control is invisible, not scrollable).
    expect(await overflowingInModal(page), 'no element inside the projects modal escapes the viewport').toEqual([]);

    // 3. Each filter control is fully within the viewport AND within the dialog card.
    const card = await page.locator('#projects-modal-overlay .app-modal').boundingBox();
    const b = await boxes(page, FILTER_CONTROLS);
    for (const sel of FILTER_CONTROLS) {
      expect(b[sel].left, `${sel} left edge on screen`).toBeGreaterThanOrEqual(0);
      expect(b[sel].right, `${sel} right edge on screen`).toBeLessThanOrEqual(doc.innerWidth);
      expect(b[sel].right, `${sel} is inside the dialog card, not clipped by it`)
        .toBeLessThanOrEqual(card.x + card.width + 0.5);
      // Not squeezed to uselessness either (the search box used to end up ~25px wide).
      // Custom-select pill triggers are label-driven and narrower than the old native
      // selects ("All" ≈ 74px grown), so the floor guards the collapse, not the chrome.
      expect(b[sel].width, `${sel} is wide enough to use`).toBeGreaterThan(64);
      // The app's touch convention: ≥40px tap targets on a coarse pointer.
      expect(b[sel].height, `${sel} tap target`).toBeGreaterThanOrEqual(40);
    }
    // The row really did wrap onto multiple lines — that's how it fits.
    const rows = new Set(FILTER_CONTROLS.map((s) => b[s].top));
    expect(rows.size, 'the filters row wraps on a phone').toBeGreaterThan(1);

    // 4. The footer note gets its own line instead of wrapping one word per line,
    //    and the three action buttons stay on screen beside/below it.
    const foot = await boxes(page, ['#projects-modal-overlay .footer-hint', '#projects-blank-image', '#projects-new-editor', '#projects-clear-all']);
    const hint = foot['#projects-modal-overlay .footer-hint'];
    expect(hint.width, 'the footer hint spans the footer').toBeGreaterThan(doc.innerWidth * 0.7);
    expect(hint.height, 'the footer hint is one or two lines, not one word per line').toBeLessThan(45);
    for (const sel of ['#projects-blank-image', '#projects-new-editor', '#projects-clear-all']) {
      expect(foot[sel].right, `${sel} on screen`).toBeLessThanOrEqual(doc.innerWidth);
      expect(foot[sel].left, `${sel} on screen`).toBeGreaterThanOrEqual(0);
    }
  });

  test('the drag-out drop zones stay inside the viewport', async ({ page }) => {
    await gotoApp(page);
    await seedAndOpenList(page);
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

// The phone rules are breakpoint-gated (max-width: 680px) — they must NOT leak into the
// desktop dialog, where all four filter controls share one row.
test.describe('projects modal: desktop layout is unaffected', () => {
  test.use({ viewport: { width: 1280, height: 800 } });

  test('the filters stay on a single row at 1280px', async ({ page }) => {
    await gotoApp(page);
    await seedAndOpenList(page);
    const b = await boxes(page, FILTER_CONTROLS);
    // The desktop dialog stacks the search field above the filter row (its own bar,
    // .projects-filter-row) — the three selects themselves share ONE row: only the
    // phone breakpoint may wrap them onto more.
    const selectTops = FILTER_CONTROLS.slice(1).map((s) => b[s].top);
    expect(Math.max(...selectTops) - Math.min(...selectTops), 'one row of filters').toBeLessThanOrEqual(2);
    expect(b['#projects-search'].top, 'the search bar sits above the filters').toBeLessThan(Math.min(...selectTops));
    const card = await page.locator('#projects-modal-overlay .app-modal').boundingBox();
    expect(card.width, 'the desktop dialog keeps its 560px width').toBe(560);
    // The footer hint still sits BESIDE the buttons (one row), as on desktop before.
    const foot = await boxes(page, ['#projects-modal-overlay .footer-hint', '#projects-clear-all']);
    expect(Math.abs(foot['#projects-modal-overlay .footer-hint'].top - foot['#projects-clear-all'].top))
      .toBeLessThanOrEqual(4);
    expect(await overflowingInModal(page)).toEqual([]);
  });
});
