// The projects modal's DOM-side wiring contract: the shared open paths, the touch media query,
// the focus ring, the metadata tooltip and the hover preview. From projectOpenGesture.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { DOUBLE_CLICK_MS, DRAG_SLOP_PX } from '../js/core/projectOpenGesture.js';
import { isTouchLike, TOUCH_MEDIA } from '../js/utils.js';
import { COMPONENTS_CSS } from './helpers/css.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';
import { projectsModalSource } from './helpers/projectsModalSource.js';

// ── Wiring contract (what the DOM side must keep doing) ──
test('the row wires every gesture to the SAME open paths, and stays keyboard-usable', () => {
  const src = projectsModalSource();
  // One intent runner, reusing the existing open paths — no duplicated open logic.
  assert.ok(src.includes('const openWithIntent = async ({ confirm = true, target = \'here\', closeAnchor = null } = {}) => {'));
  assert.ok(src.includes('app.openProjectInNewTab(meta.id);   // the same path the ⋯ menu uses'));
  assert.ok(src.includes('app.switchToProject(meta.id);'));
  assert.ok(src.includes('if (confirm && !(await confirmOpen(meta.name, true, closeAnchor))) return;'), 'new-tab wording');
  assert.ok(src.includes('const open = () => openWithIntent({ confirm: true, target: \'here\', closeAnchor: menuBtn });'), 'the ⋯ menu keeps its Open — flying back into the ⋯, not the menu row that is gone');
  // Every gesture goes through the machine.
  for (const wire of [
    "row.addEventListener('click', (e) => gesture.click(e));",
    "row.addEventListener('dblclick', (e) => gesture.dblclick(e));",
    "row.addEventListener('pointerdown', (e) => {",
    "row.addEventListener('pointermove', (e) => gesture.pressMove({ x: e.clientX, y: e.clientY }));",
    "row.addEventListener('dragstart', () => gesture.dragStart());",
  ]) assert.ok(src.includes(wire), `wired: ${wire}`);
  // Both drag engines cancel a pending open: HTML5 dragstart (mouse) and the touch
  // engine's pickup callback (touchDrag.js onStart).
  assert.ok(src.includes('row._openGesture?.dragStart(); dragKey = key;'), 'touch pickup cancels the open');
  // Reordering itself is untouched — the row is still draggable and still carries the
  // reorder flag the global drop overlay ignores.
  assert.ok(src.includes('row.draggable = true;'));
  assert.ok(src.includes("e.dataTransfer.setData('application/x-stencil-reorder', 'project')"));
  assert.ok(src.includes('makeTouchDraggable(row, {'), 'the touch reorder engine is still attached');
  // Keyboard: focusable, announced as a button, Enter/Space activate.
  assert.ok(src.includes('row.tabIndex = 0;') && src.includes("row.setAttribute('role', 'button');"));
  assert.ok(src.includes("if (e.key !== 'Enter' && e.key !== ' ') return;"));
  // The rename path cancels a pending open so no modal lands over the input.
  assert.ok(src.includes('gesture: rowGesture }') && src.includes('gesture?.cancel();'));
  // The overflow menu still opens from a right-click / touch callout — that menu is
  // where "Open in new tab" lives for touch, since the hold belongs to reordering.
  assert.match(src, /row\.addEventListener\('contextmenu', e => \{\s+e\.preventDefault\(\);\s+showMenu\(menuBtn, menuItems\(\), \{ x: e\.clientX, y: e\.clientY \}\);/);
  assert.ok(src.includes("{ icon: 'external', label: 'Open in new tab', onClick:"), 'the touch route exists');
  // Touch detection is the app-wide helper, never a user-agent sniff; matched on the named import
  // rather than the whole statement, which another helper in it would break.
  assert.match(src, /import \{[^}]*\bisTouchLike\b[^}]*\} from '[^']*utils\.js';/);
  assert.ok(!/navigator\.userAgent/.test(src), 'no UA sniffing');
});

test('the touch rule is the app-wide media query, shared with the chat surfaces', () => {
  assert.ok(TOUCH_MEDIA.includes('(max-width: 680px)') && TOUCH_MEDIA.includes('(hover: none) and (pointer: coarse)'));
  assert.strictEqual(isTouchLike((q) => ({ matches: q === TOUCH_MEDIA })), true);
  assert.strictEqual(isTouchLike(() => ({ matches: false })), false);
  assert.strictEqual(isTouchLike(null), false, 'no matchMedia (Node) → desktop mapping');
  assert.strictEqual(isTouchLike(() => { throw new Error('bad query'); }), false);
  // The context menu's assistant gate is the very same rule (one helper, one behaviour).
  const ctx = contextMenuSource();
  assert.ok(ctx.includes('const plain = isTouchLike();'));
});

test('constants + focus ring; the hold stays the reorder pickup, unstyled by us', () => {
  assert.strictEqual(DOUBLE_CLICK_MS, 250);
  assert.strictEqual(DRAG_SLOP_PX, 10);
  const css = COMPONENTS_CSS;
  assert.ok(css.includes('.project-row:focus-visible'), 'keyboard focus is visible');
  assert.strictEqual(css.split('.project-row.project-holding').length - 1, 0,
    'no press state of ours — the drag ghost is the hold feedback');
  // The list still scrolls by finger and the drag pickup keeps its own dim state.
  assert.ok(css.includes('touch-action: pan-y') && css.includes('.project-row.project-dragging'));
  // The touch reorder engine still owns the hold (its own threshold, unchanged).
  const drag = readFileSync(new URL('../js/ui/touchDrag.js', import.meta.url), 'utf8');
  assert.ok(drag.includes('longPressMs = 280'), 'reorder pickup threshold untouched');
});

// The metadata tooltip lives on the TEXT column, never the row: a native tooltip anywhere up
// the thumb's ancestor chain survives the move onto the thumb and covers the preview.
test('the metadata tooltip is on the text column, not the row', () => {
  const src = projectsModalSource();
  assert.match(src, /if \(tip\) info\.dataset\.title = tip;/, 'the tooltip hangs off .project-info');
  assert.ok(!/if \(tip\) row\.dataset\.title = tip;/.test(src), 'never on the row — it would cover the preview');
});

// The stored thumbnail is only ~160 px wide, so a max-width can never enlarge it: the
// preview sets an explicit width, scaled from the thumbnail's own pixels.
test('the hover preview renders larger than the thumbnail', () => {
  const src = readFileSync(new URL('../js/ui/projectThumbZoom.js', import.meta.url), 'utf8');
  assert.match(src, /const PREVIEW_ZOOM = 1\.67;/, 'the factor is named, not buried');
  assert.match(src, /const PREVIEW_MAX_VW = 0\.25;/, 'and so is the width ceiling');
  assert.match(src, /const PREVIEW_MAX_VH = 0\.20;/, 'and the height ceiling');
  // ONE scale factor for both axes: the CSS max-width/max-height pair clamps each axis
  // independently, which letterboxes a portrait thumbnail.
  assert.match(src, /const scale = Math\.min\(PREVIEW_ZOOM \* f,/, 'one scale factor, so the aspect ratio holds');
  assert.match(src, /img\.style\.width = `\$\{Math\.round\(zoomSize\.nw \* scale\)\}px`/, 'width from that factor');
  assert.match(src, /img\.style\.height = `\$\{Math\.round\(zoomSize\.nh \* scale\)\}px`/, 'height from the SAME factor');
  // Alt held doubles the glance — the factor rides both the zoom cap and the ceilings.
  assert.match(src, /const f = zoomAlt \? 2 : 1;/, 'the Alt factor is named');
  const css = COMPONENTS_CSS;
  // A hover preview is a GLANCE, not a lightbox: capped to a quarter of the width and a fifth of
  // the height, with `width: auto` so the HEIGHT cap binds on portrait images.
  const zoom = css.slice(css.indexOf('.project-thumb-zoom img {'));
  assert.match(zoom.slice(0, 260), /max-width: 25vw/, 'capped to a quarter of the viewport width');
  assert.match(zoom.slice(0, 260), /max-height: 20vh/, 'and a fifth of its height');
  assert.match(zoom.slice(0, 260), /width: auto/, 'so the height cap binds on tall thumbnails');
});

// canRefreshList is the shared gate — no rebuild mid-drag or while a removal is in flight:
