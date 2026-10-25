// The projects modal's DOM-side wiring contract: the shared open paths, the touch media query,
// the focus ring, the metadata tooltip and the hover preview. From projectOpenGesture.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { DOUBLE_CLICK_MS, DRAG_SLOP_PX } from '../../../js/core/project/openGesture.js';
import { isTouchLike, TOUCH_MEDIA } from '../../../js/utils.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { mountContextMenu } from '../../helpers/ctxMenuMountRig.js';
import { mountProjectsModal } from '../../helpers/projectsModalRig.js';
import { projectsModalSource } from '../../helpers/moduleSource.js';

// ── Wiring contract: a mounted projects list, its row driven the way a pointer or a key drives it ──
const mountRow = async (t) => {
  const calls = [];
  const m = await mountProjectsModal(t, { metas: [{ id: 'a', name: 'Beta', keywords: [], updatedAt: 1 }],
    app: { confirm: async (msg) => { calls.push(['confirm', msg]); return true; },
      projectTransfer: { switchToProject: (id) => calls.push(['here', id]), openProjectInNewTab: (id) => calls.push(['newtab', id]) } } });
  m.$('projects-modal-overlay').classList.add('modal-open');
  m.change('projects-filter', 'all');
  const row = () => m.rows().find((r) => r.dataset.filterKey === 'local:a');
  const settle = () => new Promise((r) => setImmediate(r));
  const fireClick = async () => { m.timers.splice(0).filter((x) => x.ms === DOUBLE_CLICK_MS).forEach((x) => x.fn()); await settle(); };
  return { m, row, calls, settle, fireClick };
};
const HERE = ['confirm', 'Open "Beta" here? Any unsaved changes in the current tab will be replaced.'];
const NEWTAB = ['confirm', 'Open "Beta" in a new tab?'];

test('a double-click opens the row here at once; with Ctrl or Cmd, in a new tab — the paths the ⋯ menu uses', async (t) => {
  const r = await mountRow(t);
  r.row().dispatch('dblclick', {});
  await r.settle();
  assert.deepStrictEqual(r.calls.splice(0), [['here', 'a']], 'a double-click never asks');
  r.row().dispatch('dblclick', { ctrlKey: true });
  r.row().dispatch('dblclick', { metaKey: true });
  await r.settle();
  assert.deepStrictEqual(r.calls, [['newtab', 'a'], ['newtab', 'a']]);
});

test('a single click waits out the double-click interval, then confirms before it opens', async (t) => {
  const r = await mountRow(t);
  r.row().dispatch('click', {});
  await r.settle();
  assert.deepStrictEqual(r.calls, [], 'nothing before the interval');
  await r.fireClick();
  assert.deepStrictEqual(r.calls.splice(0), [HERE, ['here', 'a']]);
  r.row().dispatch('click', { ctrlKey: true });
  await r.fireClick();
  assert.deepStrictEqual(r.calls, [NEWTAB, ['newtab', 'a']], 'the new-tab wording');
});

test('a press that travels past the slop is a reorder drag: it opens nothing, not even on release', async (t) => {
  const r = await mountRow(t);
  r.row().dispatch('pointerdown', { button: 0, clientX: 10, clientY: 10 });
  r.row().dispatch('pointermove', { clientX: 10 + DRAG_SLOP_PX + 5, clientY: 10 });
  r.row().dispatch('click', {});
  await r.fireClick();
  r.row().dispatch('dragstart', {});
  await r.fireClick();
  assert.deepStrictEqual(r.calls, []);
});

test('the row is a focusable button that Enter and Space open, and it stays draggable', async (t) => {
  const r = await mountRow(t);
  const row = r.row();
  assert.strictEqual(row.tabIndex, 0);
  assert.strictEqual(row.getAttribute('role'), 'button');
  assert.strictEqual(row.draggable, true, 'reordering is untouched');
  row.dispatch('keydown', { key: 'Enter', preventDefault() {} });
  await r.settle();
  row.dispatch('keydown', { key: ' ', preventDefault() {} });
  await r.settle();
  row.dispatch('keydown', { key: 'a', preventDefault() {} });
  await r.settle();
  assert.deepStrictEqual(r.calls, [HERE, ['here', 'a'], HERE, ['here', 'a']]);
});

test('touch is the app-wide helper, never a user-agent sniff', () => {
  const src = projectsModalSource();
  assert.match(src, /import \{[^}]*\bisTouchLike\b[^}]*\} from '[^']*utils\.js';/);
  assert.ok(!/navigator\.userAgent/.test(src), 'no UA sniffing');
});

test('the touch rule is the app-wide media query, shared with the chat surfaces', async (t) => {
  assert.ok(TOUCH_MEDIA.includes('(max-width: 680px)') && TOUCH_MEDIA.includes('(hover: none) and (pointer: coarse)'));
  assert.strictEqual(isTouchLike((q) => ({ matches: q === TOUCH_MEDIA })), true);
  assert.strictEqual(isTouchLike(() => ({ matches: false })), false);
  assert.strictEqual(isTouchLike(null), false, 'no matchMedia (Node) → desktop mapping');
  assert.strictEqual(isTouchLike(() => { throw new Error('bad query'); }), false);
  // The context menu's assistant and script gates are the very same rule (one helper, one
  // behaviour): the mounted menu's page answers ONLY TOUCH_MEDIA, so a private query stays a flyout.
  const m = await mountContextMenu(t, { settings: { provider: 'ollama', baseUrl: 'http://localhost:11434' } });
  const plain = () => ['ctx-assist-menu', 'ctx-script'].map((id) => m.$(id).dataset.noSub);
  assert.deepStrictEqual(plain(), ['0', '0'], 'a desktop pointer keeps both flyouts');
  m.env.touch = true;
  m.open();
  assert.deepStrictEqual(plain(), ['1', '1'], 'the shared touch rule makes both plain items');
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
  const drag = readFileSync(new URL('../../../js/ui/canvas/touchDrag.js', import.meta.url), 'utf8');
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
  const src = readFileSync(new URL('../../../js/ui/projects/window/projectThumbZoom.js', import.meta.url), 'utf8');
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
