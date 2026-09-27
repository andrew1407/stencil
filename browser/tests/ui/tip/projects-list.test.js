// The projects list's motion (js/ui/projects/window/projectsModal.js + list/renderList.js): every
// filter control re-lists through the one transition, render() and that transition share one
// plan, a removal's reveal arrives on its own clock, and only a real removal scatters.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  wipeDurationMs, FILTER_ENTERING_CLASS, MATERIALIZE_CLASS, LEAVING_CLASS, ROW_ARRIVE_MS,
  ROW_ARRIVE_DELAY_MS, ITEM_DUST_MS,
} from '../../../js/ui/motion.js';
import { setMotionOverride } from '../../../js/ui/motion/motionPrefs.js';
import { createRenderList } from '../../../js/ui/projects/list/renderList.js';
import { installMenuDom } from '../../helpers/menuDom.js';
import { mountProjectsModal } from '../../helpers/projectsModalRig.js';

const METAS = [
  { id: 'a', name: 'Beta', keywords: ['fruit'], updatedAt: 1 },
  { id: 'b', name: 'Alpha', keywords: [], updatedAt: 2 },
];
const entering = (m) => m.rows().filter((r) => r.classList.contains(FILTER_ENTERING_CLASS)).map((r) => r.dataset.filterKey);

// createRenderList alone, over rows keyed by `plan.keys`; every setTimeout held for the test.
const renderRig = (t, initial) => {
  const doc = installMenuDom();
  const realSetTimeout = globalThis.setTimeout;
  const timers = [];
  globalThis.setTimeout = (fn, ms) => timers.push({ fn, ms });
  t.after(() => { doc.restore(); globalThis.setTimeout = realSetTimeout; });
  const list = doc.body.appendChild(doc.createElement('div'));
  const plan = { keys: initial };
  const row = () => Object.assign(doc.createElement('div'), { className: 'project-row' });
  const rows = createRenderList({
    app: {}, list, search: { value: '' }, clearAllBtn: doc.body.appendChild(doc.createElement('button')),
    remotes: { cache: null, loading: false, failed: false }, remoteObjectUrls: new Set(),
    rowPlan: () => plan.keys.map((key) => ({ key, build: row })), showsServer: () => false,
    filterMode: () => 'all', hasServers: () => false, ensureRemotes() {}, keyMeta: new Map(),
    attachRowDrag() {}, hideZoom() {}, closeMenu() {}, selected: new Map(), selectables: new Map(), updateBatchBar() {},
  });
  const fire = (ms) => timers.splice(0).forEach((x) => { assert.strictEqual(x.ms, ms); x.fn(); });
  const shown = () => list.children.filter((r) => r.classList.contains('project-row')).map((r) => r.dataset.filterKey);
  return { doc, list, plan, rows, timers, fire, keys: shown };
};
const settled = () => new Promise((r) => setImmediate(r));

test('every projects filter control re-lists through the shared transition', async (t) => {
  const m = await mountProjectsModal(t, { metas: METAS, temporary: true });
  // The kind filter: the rows a change brings back arrive through the transition.
  m.change('projects-filter', 'incognito');
  assert.deepStrictEqual(m.keys(), [], 'the incognito filter lists nothing here');
  m.change('projects-filter', 'all');
  assert.deepStrictEqual(entering(m), ['temp', 'local:b', 'local:a'], 'the kind filter');
  m.change('projects-filter', 'all');
  assert.deepStrictEqual(entering(m), [], 'a change that moves nothing replays nothing');
  // The sort select: the moved rows replay, and the choice is remembered.
  m.change('projects-sort', 'date-asc');
  assert.deepStrictEqual(entering(m), ['temp', 'local:a', 'local:b'], 'the sort select');
  assert.strictEqual(m.session.get('stencil_projects_sortmode'), 'date-asc');
  // The name search, and the search-mode select re-running it over names only.
  m.type('fruit');
  assert.deepStrictEqual(m.keys(), ['local:a'], 'the name search (a keyword hit in the common mode)');
  m.change('projects-search-mode', 'names');
  assert.deepStrictEqual(m.keys(), [], 'the search-mode select');
  m.change('projects-search-mode', 'keywords');
  assert.deepStrictEqual(entering(m), ['local:a'], 'and what it brings back arrives');
  m.type('');
  assert.deepStrictEqual(entering(m), ['temp', 'local:a', 'local:b'], 'clearing the search re-lists them');
});

test('the filter transition and render() agree on the rows by construction', (t) => {
  // ONE plan builds the list and answers "what would this state show?" — two independent
  // lists would silently disagree about what a change moves.
  const r = renderRig(t, ['x', 'y']);
  r.rows.render();
  assert.deepStrictEqual(r.keys(), ['x', 'y'], 'render() walks the plan and stamps each row with its key');
  assert.deepStrictEqual(r.rows.shownKeys, ['x', 'y'], 'and records what it listed');
  r.plan.keys = ['y'];
  r.rows.render();
  assert.deepStrictEqual([r.keys(), r.rows.shownKeys], [['y'], ['y']]);
  assert.ok(r.rows.rowByFilterKey('y') === r.list.children[0], 'a key finds the row it stamped');
});

test('what a removal REVEALS arrives, it does not simply appear', async (t) => {
  // Only the rows the settle ADDED materialize — veiled behind their own motes, the removal played
  // backwards — on the desktop's own ROW_ARRIVE_MS, twin of ListFilterFade::dustRowIn.
  const r = renderRig(t, ['x', 'y']);
  r.rows.render();
  const settle = r.rows.beginRemoval();
  r.plan.keys = ['y', 'z'];
  settle();
  r.fire(Math.min(ROW_ARRIVE_DELAY_MS, wipeDurationMs()));
  await settled();
  const [y, z] = r.list.children;
  assert.ok(!y.classList.contains(MATERIALIZE_CLASS) && z.classList.contains(MATERIALIZE_CLASS),
    'only the key the settle ADDED materializes');
  const cloud = r.doc.body.children.find((h) => h.__cloud);
  assert.deepStrictEqual([cloud.__cloud.span, cloud.__cloud.flight], [ROW_ARRIVE_MS, 'gather'], 'on the arrival clock');
});

test('the row and its arrival land together, once the ash has thinned', async (t) => {
  // The settle waits the falling leg out, THEN renders and materializes in one turn: rendering as the
  // collapse ends drops the arrival inside a full-strength scatter, where its own motes are invisible.
  const r = renderRig(t, ['x', 'y']);
  r.rows.render();
  const settle = r.rows.beginRemoval();
  r.plan.keys = ['y'];
  const done = settle();
  const wipe = wipeDurationMs();
  const arrive = Math.min(ROW_ARRIVE_DELAY_MS, wipe);
  assert.deepStrictEqual([r.keys(), r.timers.map((x) => x.ms)], [['x', 'y'], [arrive]], 'the ash thins first');
  r.fire(arrive);
  await settled();
  assert.deepStrictEqual(r.keys(), ['y'], 'then the rebuild');
  assert.ok(r.rows.removing(), 'the hold still stands');
  assert.deepStrictEqual(r.timers.map((x) => x.ms), [wipe - arrive]);
  r.fire(wipe - arrive);
  await done;
  assert.ok(!r.rows.removing(), 'only the remainder of the wipe trails the arrival');
  // Nothing flies in 'slide'/'none', so wipeDurationMs() is 0 and the row must go THEN.
  setMotionOverride({ mode: 'slide' });
  t.after(() => setMotionOverride(null));
  const settleNow = r.rows.beginRemoval();
  r.plan.keys = [];
  const now = settleNow();
  await settled();
  assert.deepStrictEqual([r.keys(), r.timers.map((x) => x.ms)], [[], [0]], 'no arrival beat without a wipe');
  r.fire(0);
  await now;
});

test('a real removal keeps the destructive wipe — a filter is not a delete', async (t) => {
  // The scatter stays on the removal paths only, on the ITEM clock: a project card is read, not merely
  // noticed, so its wipe runs half again as long as the connections list's.
  const removed = [];
  const m = await mountProjectsModal(t, { metas: METAS, mode: 'particles',
    app: { confirm: async () => true, removeProject: (id) => removed.push(id) } });
  m.$('projects-modal-overlay').classList.add('modal-open');
  m.change('projects-filter', 'local');
  m.type('alp');
  const clouds = () => m.doc.querySelectorAll('.disintegrate-host').map((h) => h.__cloud.flight);
  assert.ok(clouds().includes('gather') && !clouds().includes('scatter'), 'the filter path gathers, it never scatters');
  assert.ok(m.rows().every((r) => !r.classList.contains(LEAVING_CLASS)), 'and never plays a row out');
  m.type('');
  const row = m.rows().find((r) => r.dataset.id === 'b');
  row.dispatch('contextmenu', { clientX: 5, clientY: 5 });
  m.doc.querySelectorAll('.project-menu-item').find((b) => b.textContent.startsWith('Remove')).click();
  await settled();
  const host = row.parentElement.children.find((h) => h.__cloud);
  assert.ok(row.classList.contains(LEAVING_CLASS), 'deleting a project still scatters');
  assert.deepStrictEqual([host.__cloud.flight, host.__cloud.span], ['scatter', ITEM_DUST_MS]);
});
