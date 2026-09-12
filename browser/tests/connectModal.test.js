import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

// layout() transitively imports every ui component, including the connect modal.
import { layout } from '../js/ui/layout.js';
import {
  createListHold, emptyStateVisible, tileMotion, materialize,
  MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, LEAVE_MS, DISINTEGRATE_MS,
  FILTER_ENTERING_CLASS, FILTER_ENTER_MS, TILE_JITTER_SHARE,
} from '../js/ui/motion.js';
import { canRefreshList } from '../js/core/projectOpenGesture.js';
import { StencilConnectModal, matchesConnFilter, batchNote } from '../js/ui/connectModal.js';
import { createStubElement, installDom } from './helpers/dom.js';
import { FLIGHTS, moteFrame, alphaAt } from '../js/ui/dustCloud.js';
import { motionSource } from './helpers/motionSource.js';
import { LAYOUT_CSS, COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;

// ── Static markup ───────────────────────────────────────────────────────────
// The connections list is runtime-built; nothing that only render() may decide —
// rows or the "No servers connected." placeholder — may exist statically, or it
// would flash before the first render (and beneath a removal's falling dust).

test('each connect-modal id appears exactly once', () => {
  for (const id of ['connect-modal-overlay', 'connect-close', 'connect-url', 'connect-token',
    'connect-add', 'connect-reconnect', 'connect-list', 'connect-batch-bar', 'connect-filter']) {
    assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
  }
});

test('#connect-list is empty/comment-only in static markup', () => {
  assert.ok(/<div id="connect-list"><!-- filled by JS --><\/div>/.test(markup),
    'empty comment-only #connect-list present');
  assert.strictEqual(count('connect-row'), 0, 'no connection rows statically');
  assert.strictEqual(count('No servers connected'), 0,
    'the empty state is render()’s call — it must never pre-exist the first render');
});

// ── The refresh gate ────────────────────────────────────────────────────────
// The connections list gates its out-of-band re-render (stencil:connections-changed)
// on the SAME helper the projects modal uses: never mid-drag, never while a wipe
// holds — that render is exactly the rebuild that cuts the leave short and pops the
// empty state in beneath the still-falling dust.

test('connections refresh gate: canRefreshList holds off renders mid-wipe', () => {
  assert.strictEqual(canRefreshList({ open: true, dragging: false, removing: false }), true);
  assert.strictEqual(canRefreshList({ open: true, removing: true }), false,
    'a removal in flight defers the re-render to the settle');
  assert.strictEqual(canRefreshList({ open: true, dragging: true }), false);
  assert.strictEqual(canRefreshList({ open: false }), false, 'a closed modal never re-renders');
});

// ── The wipe hold (createListHold) ──────────────────────────────────────────
// One hold per playing leave/materialize; the settle waits out the FULL wipe
// (wipeDurationMs, not the 220ms collapse) and runs the deferred render exactly once.

const stubTimers = () => {
  const queue = [];
  return {
    queue,
    setTimer: (fn, ms) => { queue.push({ fn, ms }); return queue.length; },
    run: () => { for (const t of queue.splice(0)) t.fn(); },
  };
};

test('createListHold: holds until the wipe is over, then settles exactly once', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  assert.strictEqual(hold.holding, false);
  const settle = hold.begin();
  assert.strictEqual(hold.holding, true, 'a wipe in flight gates refreshes');
  assert.strictEqual(settles, 0, 'no settle render before the dust has landed');
  const p = settle();
  assert.strictEqual(t.queue[0].ms, 900, 'waits the FULL wipe, not the short collapse');
  assert.strictEqual(settles, 0, 'starting the settle timer is not settling');
  t.run();
  await p;
  assert.strictEqual(settles, 1);
  assert.strictEqual(hold.holding, false);
});

test('createListHold: overlapping holds keep gating until the LAST one settles', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  const a = hold.begin();
  const b = hold.begin();
  const pa = a();
  t.run();
  await pa;
  assert.strictEqual(hold.holding, true, 'the second wipe still holds');
  const pb = b();
  t.run();
  await pb;
  assert.strictEqual(hold.holding, false);
  assert.strictEqual(settles, 2, 'each hold settles its own deferred render');
});

test('createListHold: finalizeAll settles pending holds NOW; late timers no-op', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  const a = hold.begin();
  hold.begin();
  const pa = a();               // one settle already scheduled…
  hold.finalizeAll();           // …when the modal closes mid-animation
  assert.strictEqual(settles, 2, 'a close finalizes every pending removal immediately');
  assert.strictEqual(hold.holding, false, 'nothing half-removed can reappear on reopen');
  t.run();                      // the late wipe timer fires after the close
  await pa;
  assert.strictEqual(settles, 2, 'a finalized hold’s timer settles nothing twice');
});

// ── No early empty state ────────────────────────────────────────────────────
// While a hold is pending the "No servers connected." placeholder may not appear,
// even with zero connections left — it waits for the settle render.

test('emptyStateVisible: never during a wipe, only once truly settled', () => {
  assert.strictEqual(emptyStateVisible(0, false), true, 'empty + idle → placeholder');
  assert.strictEqual(emptyStateVisible(0, true), false,
    'empty but mid-wipe → the placeholder must wait for the settle render');
  assert.strictEqual(emptyStateVisible(3, false), false);
  assert.strictEqual(emptyStateVisible(3, true), false);
  assert.strictEqual(emptyStateVisible(0), true, 'holding defaults to false');
});

test('the removal timeline never shows the empty state early', async () => {
  // The confirmDisconnect sequence, replayed over the hold: begin → leave (collapse)
  // → disconnect → settle. At every step before the settle the placeholder is gated.
  const t = stubTimers();
  let placeholderShown = false;
  let connections = 1;
  const hold = createListHold({
    settle: () => { placeholderShown = emptyStateVisible(connections, hold.holding); },
    wait: () => 900,
    setTimer: t.setTimer,
  });
  const settle = hold.begin();                       // removal begins, height pinned
  assert.strictEqual(emptyStateVisible(connections, hold.holding), false);
  connections = 0;                                   // disconnect landed mid-wipe
  assert.strictEqual(emptyStateVisible(connections, hold.holding), false,
    'zero rows but dust still falling → no placeholder yet');
  const p = settle();
  t.run();
  await p;
  assert.strictEqual(placeholderShown, true, 'the settle render finally shows it');
});

// ── The gather (materialize = the removal reversed) ─────────────────────────

test('tileMotion reverse: same flight path, inverted sweep', () => {
  const cols = 34;
  const rows = 16;
  const outTop = tileMotion(3, 0, cols, rows);
  const backTop = tileMotion(3, 0, cols, rows, true);
  // The path home is the scatter path played backwards — identical displacement.
  assert.strictEqual(outTop.dx, backTop.dx);
  assert.strictEqual(outTop.dy, backTop.dy);
  assert.strictEqual(outTop.rot, backTop.rot);
  assert.strictEqual(outTop.scale, backTop.scale);
  // Scatter: the top row leaves first. Gather: the first mote out is the LAST one home.
  const outBottom = tileMotion(3, rows - 1, cols, rows);
  const backBottom = tileMotion(3, rows - 1, cols, rows, true);
  assert.ok(outTop.delay < outBottom.delay, 'scatter sweeps top→bottom');
  assert.ok(backTop.delay > backBottom.delay, 'gather sweeps bottom→top');
  // The reversed sweep spans the same window as the forward one (same duration budget):
  // the gather's 0.4 share plus a mote's own jitter, which is a SHARE of the span too —
  // it used to be a flat 60ms, and that literal quietly became wrong the moment the span
  // changed (motion.js TILE_JITTER_SHARE).
  assert.ok(backTop.delay <= DISINTEGRATE_MS * (0.4 + TILE_JITTER_SHARE));
});

test('materialize: expands on the collapse’s own timer; no veil without dust', async () => {
  // No DOM here, so the gather builds no tiles (disintegrate bails) — the row must
  // then expand un-veiled on LEAVE_MS, never hide behind a veil nothing will lift.
  const classes = new Set();
  const el = {
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
    },
  };
  const started = Date.now();
  const p = materialize(el);
  assert.ok(classes.has(MATERIALIZE_CLASS), 'the box-expand class goes on immediately');
  assert.ok(!classes.has(MATERIALIZE_VEIL_CLASS), 'no dust → no veil (nothing would lift it)');
  await p;
  assert.ok(!classes.has(MATERIALIZE_CLASS), 'cleaned up once the expansion is over');
  assert.ok(Date.now() - started >= LEAVE_MS - 20, 'the expansion runs the collapse’s duration');
});

test('materialize: a missing element resolves without touching anything', async () => {
  await materialize(null);   // must not throw — the add never depends on the animation
});

// ── The CSS contract ────────────────────────────────────────────────────────
// The classes motion.js toggles must exist in animations.css with the reversed
// shapes: the expand mirrors rowLeave's collapse, the gather mirrors tileScatter.

test('animations.css: materialize is the leave reversed, veil outranks keyframes', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /\.materializing \{[^}]*animation: rowMaterialize 0\.22s/,
    'the box expands on the collapse’s own 220ms timer');
  assert.match(css, /@keyframes rowMaterialize \{\s*from \{ opacity: 0;[^}]*max-height: 0/,
    'the expansion starts from the collapsed end-state of rowLeave');
  assert.match(css, /\.materialize-veil \{ opacity: 0 !important; \}/,
    'the veil must outrank rowMaterialize’s animated opacity (author !important beats keyframes)');
  // …and it LIFTS under the landing motes, fading up (the transition on the lift alone —
  // on the veil itself it ran the other way too), with the box keyframes leaving opacity
  // to it (a transition never starts on a property an animation is holding).
  assert.match(css, /\.materialize-veil\.materialize-lift \{ opacity: 1 !important; transition: opacity var\(--veil-fade, 0ms\) linear; \}/);
  assert.match(css, /\.materializing\.materialize-veil \{ animation-name: rowMaterializeBox; \}/);
  assert.ok(!/@keyframes rowMaterializeBox \{[^}]*opacity/.test(css), 'the box keyframes carry no alpha');
  assert.match(css, /\.disintegrate-host\.dust-forming \{ animation: dustHostOut var\(--host-ms, var\(--gather-ms, 420ms\)\)/,
    'the forming host lives the whole span');
  // The gather is the scatter reversed, flown on the one canvas (js/ui/dustCloud.js): a
  // grain starts where the scatter would have flung it, invisible, and flies home to
  // identity at full opacity — the tileGather keyframes, as numbers.
  assert.equal(FLIGHTS.gather.from, 'far', 'a gather grain starts where the scatter would have flung it');
  const grain = { x: 10, y: 20, dx: 30, dy: 40, mx: 18, my: 25, r: 3, s: 0.5, a: 1 };
  const flung = moteFrame(grain, 'gather', 0);
  assert.deepEqual([flung.x, flung.y], [40, 60], 'the far end of the throw');
  const home = moteFrame(grain, 'gather', 1);
  assert.deepEqual([home.x, home.y, home.r], [10, 20, 3], 'and flies home to identity');
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 0), 0, 'invisible as it sets off');
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 1), 1, 'full once home');
});

// ── Admin connections: the golden outline + the credential filter ───────────
// A connection whose stored credential can MINT session tokens (credentialKind
// 'admin') is what the invite button needs, so the row says so: the same
// --remote-gold outline server projects wear, plus a three-way view filter.

test('matchesConnFilter: all keeps everything, the other two split on credentialKind', () => {
  const admin = { credentialKind: 'admin' };
  const session = { credentialKind: '' };
  assert.strictEqual(matchesConnFilter(admin, 'all'), true);
  assert.strictEqual(matchesConnFilter(session, 'all'), true);
  assert.strictEqual(matchesConnFilter(null, 'all'), true);
  assert.strictEqual(matchesConnFilter(admin, 'admin'), true);
  assert.strictEqual(matchesConnFilter(session, 'admin'), false);
  assert.strictEqual(matchesConnFilter(null, 'admin'), false, 'an unknown row is not admin');
  assert.strictEqual(matchesConnFilter(admin, 'non-admin'), false);
  assert.strictEqual(matchesConnFilter(session, 'non-admin'), true);
});

test('the filter select is in the static markup, all three states, defaulting to All', () => {
  assert.strictEqual(count('id="connect-filter"'), 1);
  const select = /<select id="connect-filter"[\s\S]*?<\/select>/.exec(markup)?.[0] ?? '';
  for (const opt of ['<option value="all">All</option>', '<option value="admin">Admin</option>',
    '<option value="non-admin">Non-admin</option>']) {
    assert.ok(select.includes(opt), `${opt} present`);
  }
  // First option wins with nothing preselected — All is the default, and the choice
  // is view state: no `selected` attribute, nothing read back from storage.
  assert.match(select, /<select[^>]*>\s*<option value="all">/);
  assert.ok(!select.includes('selected'), 'no filter is persisted/preselected');
});

// The ids wire() reaches for; everything else the modal touches is null-safe.
const WIRE_IDS = [
  'connect-modal-overlay', 'connect-url', 'connect-token', 'connect-add', 'connect-reconnect',
  'connect-list', 'connect-batch-bar', 'connect-batch-count', 'connect-batch-reconnect',
  'connect-batch-disconnect', 'connect-autoconnect', 'connect-sync',
  'connect-filter', 'connect-select-all', 'connect-batch-selected',
];

const conn = (url, kind) => ({
  url, status: 'connected', connected: true, credentialKind: kind,
  mintInvite: async () => `${url}#token=t`,
});

// Wire the REAL modal against the DOM-lite stubs and open it (onOpen renders the list).
// `reduced` false is for the motion tests: everything else runs with reduced motion
// on, so a render is synchronous and the assertions read the settled list.
const openModal = (conns, { reduced = true, mgrExtra = {}, appExtra = {} } = {}) => {
  const notes = [];   // what notify() posted, in order
  const doc = installDom({}, {
    window: createStubElement('window', { matchMedia: () => ({ matches: reduced }) }),
    matchMedia: () => ({ matches: reduced }),   // motion.js reads the bare global
    CSS: { escape: (s) => s },
  });
  const list = createStubElement('div', {
    querySelector: (sel) => {
      const m = /\[data-url="(.*)"\]/.exec(sel);
      return (m && list.children.find((c) => c.dataset?.url === m[1])) || null;
    },
  });
  // innerHTML = '' is how render() clears the list; the stub keeps children in an array.
  Object.defineProperty(list, 'innerHTML', { get: () => '', set: (v) => { if (!v) list.children.length = 0; } });
  // Select all carries its label in a <span> the modal re-titles (Select ↔ Deselect all).
  // Select all carries its label in a <span> the modal re-titles (Select ↔ Deselect all)
  // and a glyph it swaps (check ↔ cross); the stub icon records the swap by class.
  const selectAllLabel = createStubElement('span');
  const selectAllIcon = createStubElement('svg');
  selectAllIcon.classList.add('ic', 'ic-check');
  Object.defineProperty(selectAllIcon, 'outerHTML', {
    set: (html) => { const m = /ic ic-([a-z]+)/.exec(html); selectAllIcon.classList.remove('ic-check', 'ic-x'); if (m) selectAllIcon.classList.add(`ic-${m[1]}`); },
    get: () => '',
  });
  for (const id of WIRE_IDS) {
    doc.register(id, id === 'connect-list' ? list
      : id === 'connect-select-all' ? createStubElement('button', {
        querySelector: (sel) => (sel === 'span' ? selectAllLabel : sel === '.ic' ? selectAllIcon : null) })
      : createStubElement('div'));
  }
  const selectAllGlyph = () => (selectAllIcon.classList.contains('ic-x') ? 'x' : selectAllIcon.classList.contains('ic-check') ? 'check' : null);
  const byUrl = new Map(conns.map((c) => [c.url, c]));
  const urls = conns.map((c) => c.url);
  // utils.notify() posts through #notify-balloon; recording it is how a test reads a toast.
  doc.register('notify-balloon', createStubElement('div', { notify: (m, t) => notes.push([m, t]) }));
  new StencilConnectModal().wire({
    connections: { knownUrls: urls, urls, expiredUrls: [], reconnectable: true,
      get: (u) => byUrl.get(u) ?? null, ...mgrExtra },
    ...appExtra,
  });
  doc.getElementById('connect-modal-overlay').__stencilModal.open();
  return { doc, list, notes, filter: doc.getElementById('connect-filter'), selectAllGlyph,
    modal: doc.getElementById('connect-modal-overlay').__stencilModal };
};

const rows = (list) => list.children.filter((c) => c.classList?.contains('connect-row'));
const rowUrls = (list) => rows(list).map((r) => r.dataset.url);
const hasClass = (el, c) => el.classList.contains(c);
const descendants = (el) => el.children.flatMap((c) => [c, ...(c.children ? descendants(c) : [])]);
const find = (el, cls) => descendants(el).find((c) => c.classList?.contains(cls)) ?? null;

// ── The batch bar is the projects bar's shape ───────────────────────────────
// It stays while the list has rows (it hosts Select all); only the count and the
// selection-only actions come and go with the checked set, so the list never jumps.

test('the batch bar stays while rows exist; count + actions ride the selection', () => {
  const { doc, list } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const bar = doc.getElementById('connect-batch-bar');
  const count = doc.getElementById('connect-batch-count');
  const group = doc.getElementById('connect-batch-selected');
  const selectAll = doc.getElementById('connect-select-all');
  assert.notStrictEqual(bar.style.display, 'none', 'the bar is up with nothing checked…');
  assert.strictEqual(count.style.display, 'none', '…but the count waits for a selection');
  assert.strictEqual(group.style.display, 'none', '…and so do the selection actions');
  assert.notStrictEqual(selectAll.style.display, 'none', 'Select all is what the bar holds');
  assert.strictEqual(selectAll.querySelector('span').textContent, 'Select all');
  const cb = find(rows(list)[0], 'connect-select');
  cb.checked = true;
  cb.dispatch('change');
  assert.notStrictEqual(count.style.display, 'none', 'one checked → the count appears');
  assert.strictEqual(count.textContent, '1 selected');
  assert.notStrictEqual(group.style.display, 'none', '…with the selection actions');
  assert.ok(hasClass(rows(list)[0], 'connect-selected'));
});

test('Select all takes the filtered view; Deselect all clears the lot', () => {
  const { doc, list, filter, selectAllGlyph } = openModal([
    conn('http://adm:1', 'admin'), conn('http://plain:2', ''), conn('http://adm:3', 'admin'),
  ]);
  const selectAll = doc.getElementById('connect-select-all');
  const count = doc.getElementById('connect-batch-count');
  filter.value = 'admin';
  filter.dispatch('change');
  selectAll.dispatch('click');
  assert.strictEqual(count.textContent, '2 selected', 'only the rows on view are swept up');
  assert.strictEqual(selectAll.querySelector('span').textContent, 'Deselect all');
  assert.strictEqual(selectAllGlyph(), 'x', 'Deselect all wears a cross, not the check (user decision)');
  assert.ok(rows(list).every((r) => hasClass(r, 'connect-selected')));
  filter.value = 'all';
  filter.dispatch('change');
  const plain = rows(list).find((r) => r.dataset.url === 'http://plain:2');
  assert.ok(!hasClass(plain, 'connect-selected'), 'the filtered-out row was never checked');
  assert.strictEqual(selectAll.querySelector('span').textContent, 'Select all',
    'not everything on view is checked any more');
  selectAll.dispatch('click');
  assert.strictEqual(count.textContent, '3 selected');
  selectAll.dispatch('click');
  assert.strictEqual(count.textContent, '0 selected', 'Deselect all clears the selection');
  assert.strictEqual(count.style.display, 'none');
  assert.strictEqual(selectAllGlyph(), 'check', '…and the glyph is a check again');
  assert.ok(rows(list).every((r) => !hasClass(r, 'connect-selected')));
});

test('a selection is one visit\u2019s: reopening the modal starts unchecked', () => {
  const { doc, list, modal } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const cb = find(rows(list)[0], 'connect-select');
  cb.checked = true;
  cb.dispatch('change');
  assert.strictEqual(doc.getElementById('connect-batch-count').textContent, '1 selected');
  modal.close();
  modal.open();
  assert.strictEqual(doc.getElementById('connect-batch-count').textContent, '0 selected',
    'nothing carried over (user decision — projects parity)');
  assert.ok(rows(list).every((r) => !hasClass(r, 'connect-selected')));
  assert.ok(rows(list).every((r) => !find(r, 'connect-select').checked));
});

test('with no rows on view the bar goes too', () => {
  const { doc, filter } = openModal([conn('http://plain:2', '')]);
  const bar = doc.getElementById('connect-batch-bar');
  filter.value = 'admin';
  filter.dispatch('change');
  assert.strictEqual(bar.style.display, 'none', 'nothing to select → no bar');
  assert.strictEqual(openModal([]).doc.getElementById('connect-batch-bar').style.display, 'none');
});

test('an admin connection renders golden + badged; a session one does not', () => {
  const { list } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const [admin, plain] = rows(list);
  assert.ok(hasClass(admin, 'connect-admin'), 'the admin row wears the golden-outline class');
  assert.ok(!hasClass(plain, 'connect-admin'), 'a session-token row stays neutral');
  const badge = find(admin, 'connect-admin-badge');
  assert.ok(badge, 'the admin row carries the gold marker');
  assert.match(badge.dataset.title, /mint session tokens/i, 'the marker says what admin means');
  assert.strictEqual(find(plain, 'connect-admin-badge'), null);
});

test('the invite button is admin-only', () => {
  const { list } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const [admin, plain] = rows(list);
  assert.ok(find(admin, 'connect-invite'), 'only an admin credential can mint an invite');
  assert.strictEqual(find(plain, 'connect-invite'), null,
    'a session token would 401 — no button to press');
});

test('the three-way filter shows the right subset of connections', () => {
  const { list, filter } = openModal([
    conn('http://adm:1', 'admin'), conn('http://plain:2', ''), conn('http://adm:3', 'admin'),
  ]);
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://plain:2', 'http://adm:3'], 'All by default');
  filter.value = 'admin';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://adm:3']);
  filter.value = 'non-admin';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://plain:2']);
  filter.value = 'all';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://plain:2', 'http://adm:3']);
});

test('a filter matching nothing shows its own empty state, not "No servers connected."', () => {
  const { list, filter } = openModal([conn('http://plain:2', '')]);
  filter.value = 'admin';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), []);
  const empty = list.children.find((c) => c.classList?.contains('info-empty'));
  assert.ok(empty, 'the filtered-empty list still says something');
  assert.strictEqual(empty.textContent, 'No connections match this filter.');
});

test('with nothing connected the plain empty state is kept', () => {
  const { list } = openModal([]);
  const empty = list.children.find((c) => c.classList?.contains('info-empty'));
  assert.strictEqual(empty.textContent, 'No servers connected.');
});

test('components.css: the admin row reuses the server-projects gold', () => {
  const css = COMPONENTS_CSS;
  assert.match(css, /\.connect-row\.connect-admin \{[^}]*border-color: var\(--remote-gold\)/,
    'the same custom property .project-row.project-remote uses');
  assert.match(css, /\.connect-admin-badge \{[^}]*color: var\(--remote-gold\)/);
  // Declared before the transient row states, so selected/expired still win the border.
  assert.ok(css.indexOf('.connect-row.connect-admin') < css.indexOf('.connect-row.connect-expired'));
});

// ── Sibling of the projects row ─────────────────────────────────────────────
// The connections list borrows the projects modal's row idioms wholesale: the trash
// can for remove, filled row-action buttons that look enabled at rest, .project-row's
// box, and a filter sized to the heading it sits beside.

const cssText = () => COMPONENTS_CSS;
// A rule body by its exact opening selector text (line breaks in the selector list
// make a single regex brittle).
const ruleAfter = (css, selector) => {
  const at = css.indexOf(selector);
  assert.ok(at > -1, `${selector} present`);
  return css.slice(at, css.indexOf('}', at) + 1);
};

// Reconnect/Disconnect sit one level down (.connect-batch-selected), so a direct-child
// padding rule left them at the default 8px 12px — 4px taller than Select all — and the
// bar grew, and the modal with it, every time a selection was made (user report).
test('every button in the batch strip shares the compact padding, so the bar never grows', () => {
  const css = cssText();
  const rule = ruleAfter(css, '.connect-batch-actions button {');
  assert.match(rule, /padding: 6px 10px;/);
  assert.ok(!css.includes('.connect-batch-actions > button {'), 'a descendant rule, not direct children only');
});

test('the row disconnect is a trash button, like the projects modal’s remove', () => {
  const { list } = openModal([conn('http://plain:2', '')]);
  const disc = find(rows(list)[0], 'connect-disconnect');
  assert.ok(disc, 'every row can disconnect');
  assert.ok(disc.innerHTML.includes('ic-trash'), 'trash — this app’s remove glyph everywhere else');
  assert.ok(!disc.innerHTML.includes('ic-x'), 'the ✕ meant "close", not "forget this server"');
  assert.match(disc.dataset.title, /disconnect \(and forget\) this server/i, 'the tooltip says it forgets');
  assert.ok(disc.classList.contains('danger'), 'and it keeps the danger treatment');
});

test('the batch bar disconnects with the same trash; Deselect all is its only clear', () => {
  const bar = /<div class="connect-batch-bar"[\s\S]*?<\/div>/.exec(markup)?.[0] ?? '';
  const btn = (id) => new RegExp(`<button id="${id}"[\\s\\S]*?</button>`).exec(bar)?.[0] ?? '';
  const disc = btn('connect-batch-disconnect');
  assert.ok(disc.includes('ic-trash'), 'the row and the batch bar agree on the glyph');
  assert.ok(!disc.includes('ic-x'));
  assert.ok(disc.includes('class="danger'), 'still the danger colour');
  assert.strictEqual(btn('connect-batch-clear'), '',
    'no separate Clear — it did exactly what Deselect all does (user decision)');
  assert.ok(btn('connect-select-all').includes('ic-check'));
});

test('components.css: a connection row and a project row look like siblings', () => {
  const css = cssText();
  const row = /^\.connect-row \{([^}]*)\}/m.exec(css)?.[1] ?? '';
  const proj = /^\.project-row \{([^}]*)\}/m.exec(css)?.[1] ?? '';
  for (const decl of ['gap: 12px', 'padding: 8px 10px', 'border: 1px solid var(--border-main)',
    'border-radius: 8px', 'margin-bottom: 8px', 'background: var(--input-bg)']) {
    assert.ok(proj.includes(decl), `.project-row sets ${decl}`);
    assert.ok(row.includes(decl), `.connect-row matches it — ${decl}`);
  }
  assert.match(css, /\.connect-row:hover \{ background: var\(--bg-info\); \}/,
    'and the same hover lift .project-row:hover has');
});

test('components.css: an idle row button reads as enabled, not disabled', () => {
  const css = cssText();
  const btn = ruleAfter(css, '.connect-row .connect-reconnect-one,');
  // The projects modal's row action (.project-more) is a plain button: the shared filled
  // chrome from layout.css. These may only re-size it — any resting repaint in the muted
  // palette is what made an enabled control look already greyed.
  for (const muted of ['--disabled-bg', '--disabled-text', '--bg-info', '--text-muted', 'background']) {
    assert.ok(!btn.includes(muted), `nothing mutes them at rest (${muted})`);
  }
  assert.ok(!/border:/.test(btn), 'no bordered ghost variant either');
  assert.ok(btn.includes('padding: 5px 8px'), 'compact is the only difference');
  // Genuinely disabled still looks disabled — via the shared rule, not a local override.
  const layout = LAYOUT_CSS;
  assert.match(layout, /button:disabled,[\s\S]*?background: var\(--disabled-bg\)/);
  assert.ok(!css.includes('.connect-row .connect-disconnect:disabled'),
    'the row buttons defer to it');
});

test('components.css: the credential filter is compact and cannot crowd the heading', () => {
  const css = cssText();
  const rowRule = ruleAfter(css, '.connect-section-row {');
  assert.ok(rowRule.includes('flex-wrap: wrap'),
    'the filter drops under the heading before it can overlap it');
  assert.match(css, /\.connect-section-row \.vs-section \{[^}]*min-width: 0/,
    'and a long heading shrinks rather than shoving the filter out of the modal');
  // customSelect.js hides the native <select> behind an .accent-dd-trigger, so sizing
  // the select alone changed nothing on screen — the trigger is the control.
  const compact = ruleAfter(css, '.connect-section-row .modal-filter,');
  assert.ok(compact.includes('.cs-dd .accent-dd-trigger'), 'the enhanced trigger is sized too');
  assert.ok(compact.includes('font-size: 12px'), 'the .vs-section heading’s scale, not a form field’s');
  assert.ok(compact.includes('padding: 3px 8px'));
  assert.ok(compact.includes('max-width: 100%'), 'never wider than the modal');
});

// ── The filter transition ───────────────────────────────────────────────────
// Filtering used to be one-sided: revealed rows materialized, excluded ones simply
// vanished. Both directions now play the SHARED light filter effect — and neither is
// the disconnect's scatter, because a filtered-out connection was not removed.

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

test('filtering rebuilds the list at once — the excluded rows never play out', () => {
  const { list, filter } = openModal([
    conn('http://adm:1', 'admin'), conn('http://plain:2', ''), conn('http://adm:3', 'admin'),
  ], { reduced: false });
  filter.value = 'admin';
  filter.dispatch('change');
  // The answer is on screen immediately: nothing about what you asked for waits on an
  // exit for a row that was never removed in the first place.
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://adm:3'], 'the new answer, at once');
});

test('filtering plays the rows that are LEFT in, with the light arrival', () => {
  const { list, filter } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')],
    { reduced: false });
  filter.value = 'admin';
  filter.dispatch('change');
  filter.value = 'all';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://plain:2']);
  const revealed = rows(list).find((r) => r.dataset.url === 'http://plain:2');
  assert.ok(hasClass(revealed, FILTER_ENTERING_CLASS), 'the revealed row arrives');
  assert.ok(!hasClass(revealed, 'materializing'), 'the dust gather stays for a real connect');
  const kept = rows(list).find((r) => r.dataset.url === 'http://adm:1');
  assert.ok(hasClass(kept, FILTER_ENTERING_CLASS),
    'and so does the one that was already listed — the filtered SET is what changed');
});

test('under reduced motion the filter still lands the right set, with no classes', () => {
  const { list, filter } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  filter.value = 'admin';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://adm:1'], 'straight to the re-render');
  assert.ok(!hasClass(rows(list)[0], FILTER_ENTERING_CLASS));
});

test('a row filtered out of view stays in a pending batch selection', async () => {
  const { list, filter } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')],
    { reduced: false });
  const cb = find(rows(list).find((r) => r.dataset.url === 'http://plain:2'), 'connect-select');
  cb.checked = true;
  cb.dispatch('change');
  filter.value = 'admin';
  filter.dispatch('change');
  await sleep(FILTER_ENTER_MS + 80);
  assert.deepEqual(rowUrls(list), ['http://adm:1'], 'the non-admin row is out of view…');
  filter.value = 'all';
  filter.dispatch('change');
  const back = find(rows(list).find((r) => r.dataset.url === 'http://plain:2'), 'connect-select');
  assert.strictEqual(back.checked, true, '…but never dropped from the batch');
});

// ── What a connection toast says ────────────────────────────────────────────
// "Reconnected" on its own never said WHICH server signed back in (user report, with a
// picture). A disconnect, meanwhile, says nothing at all here — the row scattering out
// of the list IS the notice, exactly as on the desktop (ConnectDialog posts none).

test('the row Reconnect toast names the server it signed back in', async () => {
  const url = 'http://localhost:8090';
  const done = [];
  const { list, notes } = openModal([conn(url, '')],
    { mgrExtra: { reconnectOne: async (u) => { done.push(u); } } });
  find(rows(list)[0], 'connect-reconnect-one').dispatch('click');
  await sleep(10);
  assert.deepEqual(done, [url], 'the real row button drove the manager');
  assert.deepEqual(notes.at(-1), [`Reconnected to ${url}`, 'ok']);
});

test('Reconnect all names the one server, and counts several', async () => {
  const one = openModal([conn('http://localhost:8090', '')], { mgrExtra: { reconnect: async () => {} } });
  one.doc.getElementById('connect-reconnect').dispatch('click');
  await sleep(10);
  assert.deepEqual(one.notes.at(-1), ['Reconnected to http://localhost:8090', 'ok']);

  const many = openModal([conn('http://a:1', ''), conn('http://b:2', '')],
    { mgrExtra: { reconnect: async () => {} } });
  many.doc.getElementById('connect-reconnect').dispatch('click');
  await sleep(10);
  assert.deepEqual(many.notes.at(-1), ['Reconnected 2 servers', 'ok']);

  assert.strictEqual(batchNote('Reconnected', ['http://x:1']), 'Reconnected to http://x:1');
  assert.strictEqual(batchNote('Reconnected', ['http://a:1', 'http://b:2']), 'Reconnected 2 servers');
});

test('disconnecting posts no toast — the row leaving the list is the notice', async () => {
  const url = 'http://localhost:8090';
  const gone = [];
  const { list, notes } = openModal([conn(url, '')], {
    mgrExtra: { disconnect: (u) => { gone.push(u); } },
    appExtra: { confirm: async () => true },
  });
  find(rows(list)[0], 'connect-disconnect').dispatch('click');
  await sleep(LEAVE_MS + 60);
  assert.deepEqual(gone, [url], 'the server really was forgotten…');
  assert.deepEqual(notes, [], '…and nothing was said about it');
});

// The selection bar is a REVEAL, not a display flip: flipping it took Select all's own
// out-flight off the screen before a frame of it showed (user report). Pinned at the
// source — under this suite's reduced motion revealControls lands on display:none too,
// so the two are indistinguishable from the outside here.
test('the batch bar opens and closes on the shared control flight', () => {
  const src = readFileSync(new URL('../js/ui/connectModal.js', import.meta.url), 'utf8');
  assert.match(src, /revealBar\(batchBar, \(\) => selected\.size > 0 \|\| anyLiveShown\(\)\)/);
  // The pool is the shown set MINUS the rows playing their removal dust, so the bar leaves
  // beside them instead of a flight later (desktop parity: connectDialog's `doomed_`).
  assert.match(src, /const anyLiveShown = \(\) => \{[\s\S]*?!doomed\.has\(u\)\) return true;/);
  assert.ok(!/batchBar\.style\.display\s*=/.test(src), 'nothing flips the bar outright any more');
});

// A refused CREDENTIAL is not a failed connect that leaves nothing behind: the manager
// keeps the server in `_expired`, so a row appears with a Reconnect on it. That row used
// to arrive out of the connections-changed echo with no flight at all — the success path
// rendered and materialized, the failure path only toasted (user report, with a picture).
test('a refused credential arrives as dust, like a successful one', async () => {
  const url = 'http://localhost:8090';
  const { doc, list, notes } = openModal([conn(url, '')], {
    reduced: false,
    mgrExtra: {
      connect: async () => { throw new Error('POST /auth/token: admin token required to issue tokens'); },
      isExpired: () => true,
    },
  });
  doc.getElementById('connect-url').value = url;
  doc.getElementById('connect-add').dispatch('click');
  await sleep(40);
  assert.match(notes.at(-1)?.[0] ?? '', /Could not connect/, 'it still says what went wrong');
  const row = rows(list).find((r) => r.dataset.url === url);
  assert.ok(row && hasClass(row, MATERIALIZE_CLASS), 'and the row it left behind gathers in');
  // The URL is in the list now, so the fields that put it there are done — leaving them
  // typed in invited adding the same server twice (user report).
  assert.equal(doc.getElementById('connect-url').value, '');
  assert.equal(doc.getElementById('connect-token').value, '');
});

// …but a server that is merely unreachable leaves NO row, so nothing is animated.
test('an unreachable server leaves no row and plays nothing', async () => {
  const { doc, list, notes } = openModal([], {
    reduced: false,
    mgrExtra: {
      connect: async () => { throw new Error('failed to fetch'); },
      isExpired: () => false,
    },
  });
  doc.getElementById('connect-url').value = 'http://nope:1';
  doc.getElementById('connect-add').dispatch('click');
  await sleep(40);
  assert.match(notes.at(-1)?.[0] ?? '', /Could not connect/);
  assert.equal(rows(list).length, 0);
  // …and nothing was added, so the text stays put to be corrected.
  assert.equal(doc.getElementById('connect-url').value, 'http://nope:1');
});

// …and the call site asks for that gate: an expired session's token prompt must not
// offer Reconnect with the box empty — the handler ignores an empty answer, so the
// button would close the dialog and do nothing (user report, with a picture).
test('the expired-session prompt refuses an empty token', async () => {
  const url = 'http://localhost:8090';
  let asked = null;
  const { list } = openModal([conn(url, '')], {
    mgrExtra: {
      reconnectOne: async () => { const e = new Error('refused'); e.expired = true; throw e; },
    },
    appExtra: { prompt: async (_msg, opts) => { asked = opts; return null; } },
  });
  find(rows(list)[0], 'connect-reconnect-one').dispatch('click');
  await sleep(20);
  assert.ok(asked, 'the refusal raised the token prompt');
  assert.equal(asked.confirmLabel, 'Reconnect');
  assert.equal(typeof asked.validate, 'function', 'and the prompt is gated');
  assert.ok(asked.validate(''), 'an empty token is refused, with a reason');
  assert.equal(asked.validate('tok'), '', 'a pasted one is accepted');
});

// The status dot carries its OWN tooltip — what the dot means — inside a row label that
// carries the URL. Both must be there, and they must differ, or the dot has nothing of
// its own for the tooltip to take over with (desktop parity: the sentence lives on the
// dot's and the URL's tooltips alike, serverAuth.headless.cpp).
test('the connection row labels the dot and the URL separately', () => {
  const { list } = openModal([conn('http://localhost:8090', '')]);
  const label = find(rows(list)[0], 'connect-url');
  assert.ok(label?.dataset.title, 'the row label says the status and the URL');
  assert.match(label.dataset.title, /http:\/\/localhost:8090/);
  const dotTitle = /class="conn-status[^"]*"\s+data-title="([^"]*)"/.exec(label.innerHTML)?.[1];
  assert.ok(dotTitle, 'and the dot inside it says what the dot means');
  assert.notEqual(dotTitle, label.dataset.title, '…which is not simply the row\'s own text');
});

// The row and the selection bar must leave TOGETHER. The removal used to await the whole
// row flight before disconnecting, so the bar only re-asked a flight later and Select all
// went visibly after the row (user report). The desktop retires the row, disconnects and
// re-asks the bar in ONE turn (ConnectDialog.cpp: rebuildList skips the list while rows
// are doomed but still calls updateBatchBar), and so does this now.
test('removing a connection retires the row and re-asks the bar in the same turn', () => {
  const src = readFileSync(new URL('../js/ui/connectModal.js', import.meta.url), 'utf8');
  const body = src.slice(src.indexOf('const confirmDisconnect'), src.indexOf('// Build the new url order'));
  // The leave is STARTED, not awaited, before the disconnect and the bar update…
  assert.match(body, /const leaving = leaveThenRemove\(/);
  assert.match(body, /doomed\.add\(url\);[\s\S]*const leaving =[\s\S]*mgr\(\)\.disconnect\(url\);[\s\S]*updateBatchBar\(\);[\s\S]*await leaving;/);
  // The row leaves the SELECTION too, not just the shown pool: dooming it alone kept
  // "1 selected" (and the batch buttons with it) on screen until the settle render, so
  // the row went and the buttons followed a whole flight later (user report).
  assert.match(body, /doomed\.add\(url\);\s*\n\s*selected\.delete\(url\);/);
  // …on the app's own CONTROL clock, never the row's 220ms box collapse: handed that, the
  // buttons' motes were a blink under the confirm dialog's close cloud (user report).
  assert.ok(!/updateBatchBar\(\{ ms/.test(body), 'no clock override');
  // …and the url only stops counting as doomed once the SETTLE render has rebuilt the
  // list without it — the row outlives its own box collapse, so an earlier release
  // flashes Select all back on for the rest of the scatter.
  assert.match(body, /await settle\(\);\s*\n(?:[^\n]*\n){0,3}\s*doomed\.delete\(url\);/);
});

// A connections row plays the SAME flight as a project row. It used to run at two thirds
// of the clock on the same throw, which puts its cloud farther out at every instant —
// beside the projects list that read as bigger and wilder, not as brisker (user report).
// A connections row's dust is FINER and TIGHTER than the row default, and brisker:
// the row is short, so the default 7px cells were a mosaic of big dots the specks all but
// filled, and the default throw is proportionally a much bigger cloud over 44px than over
// a 74px project row (user report, with pictures: "smaller circle particles", "huge").
test('the connections list dusts on its own finer grid, smaller throw and brisk clock', () => {
  const src = readFileSync(new URL('../js/ui/connectModal.js', import.meta.url), 'utf8');
  // Every one of the four row flights — two removals, two arrivals — takes the shared
  // list-row grid and grain (motion.js rowDustGrid); the REMOVALS add the brisk clock and
  // the smaller throw (rowLeaveDust)…
  assert.equal((src.match(/rowLeaveDust\([^)]*CONN_DUST_MS\)/g) || []).length, 2);
  assert.match(src, /wait: \(\) => wipeDurationMs\(CONN_DUST_MS\)/, 'the hold waits out that clock');
  // …and the ARRIVALS keep materialize's own defaults — the projects list's filter recipe.
  assert.equal((src.match(/rowDustGrid\(\)/g) || []).length, 2);
  assert.equal((src.match(/materialize\(/g) || []).length, 2, 'and those are the two arrivals');
});

// The arrival is the PROJECTS list's own: filterDust's recipe (the surface gather
// keyframes — visible from the first frame, eased out so a mote covers most of its trip
// early — on half a throw and the filter's short clock). The row gather was tried and read
// wrong every way: motes a whole throw away, hanging through a slow-start curve, and the
// row surfacing inside a cloud plainly bigger than itself (user report, with pictures).
test('materialize gathers a row the way the projects list re-forms one', () => {
  const src = motionSource();
  const body = src.slice(src.indexOf('export function materialize'), src.indexOf('// ── A chat entry ARRIVES'));
  assert.match(body, /dustMs = FILTER_DUST_MS,\s*\n\s*drift = FILTER_DUST_DRIFT/);
  assert.match(body, /gather: true, ms: dustMs, drift, toBody: true, hostClass: 'dust-forming',\s*\n\s*paintTile: speckPainter\(el\),/);
  assert.ok(!/reintegrate\(/.test(body), 'not the row gather');
  // …and nothing brings a curve of its own any more: the row and the surface flights each
  // keep the one shared shape.
  const css = ANIMATIONS_CSS;
  assert.ok(!/--row-ease|--gather-ease/.test(css));
  assert.ok(!/--row-ease|--gather-ease/.test(src));
});

