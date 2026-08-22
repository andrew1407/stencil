import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

// layout() transitively imports every ui component, including the connect modal.
import { layout } from '../js/ui/layout.js';
import {
  createListHold, emptyStateVisible, tileMotion, materialize,
  MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, LEAVE_MS, DISINTEGRATE_MS,
} from '../js/ui/motion.js';
import { canRefreshList } from '../js/ui/projectsModal.js';
import { StencilConnectModal, matchesConnFilter } from '../js/ui/connectModal.js';
import { createStubElement, installDom } from './helpers/dom.js';

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
  // The reversed sweep spans the same window as the forward one (same duration budget).
  assert.ok(backTop.delay <= DISINTEGRATE_MS * 0.4 + 60);
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
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  assert.match(css, /\.materializing \{[^}]*animation: rowMaterialize 0\.22s/,
    'the box expands on the collapse’s own 220ms timer');
  assert.match(css, /@keyframes rowMaterialize \{\s*from \{ opacity: 0;[^}]*max-height: 0/,
    'the expansion starts from the collapsed end-state of rowLeave');
  assert.match(css, /\.materialize-veil \{ opacity: 0 !important; \}/,
    'the veil must outrank rowMaterialize’s animated opacity (author !important beats keyframes)');
  assert.match(css, /\.reintegrate-tile \{\s*animation: tileGather/,
    'gather tiles override the scatter animation on the shared tile class');
  assert.match(css, /@keyframes tileGather \{\s*0%\s+\{ opacity: 0;\s*transform: translate\(var\(--dx/,
    'a gather tile starts where the scatter would have flung it');
  assert.match(css, /@keyframes tileGather \{[\s\S]*?100% \{ opacity: 1; transform: none; \}/,
    'and flies home to identity');
  // The gather rides the same fixed layer, so reduced motion hides it with the scatter.
  assert.ok(css.indexOf('.reintegrate-tile') > css.indexOf('.disintegrate-tile'),
    'declared after .disintegrate-tile so the gather animation wins');
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
  'connect-batch-disconnect', 'connect-batch-clear', 'connect-autoconnect', 'connect-sync',
  'connect-filter',
];

const conn = (url, kind) => ({
  url, status: 'connected', connected: true, credentialKind: kind,
  mintInvite: async () => `${url}#token=t`,
});

// Wire the REAL modal against the DOM-lite stubs and open it (onOpen renders the list).
const openModal = (conns) => {
  const doc = installDom({}, {
    window: createStubElement('window', { matchMedia: () => ({ matches: true }) }),
    matchMedia: () => ({ matches: true }),   // motion.js reads the bare global
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
  for (const id of WIRE_IDS) doc.register(id, id === 'connect-list' ? list : createStubElement('div'));
  const byUrl = new Map(conns.map((c) => [c.url, c]));
  const urls = conns.map((c) => c.url);
  new StencilConnectModal().wire({
    connections: { knownUrls: urls, urls, expiredUrls: [], reconnectable: true, get: (u) => byUrl.get(u) ?? null },
  });
  doc.getElementById('connect-modal-overlay').__stencilModal.open();
  return { doc, list, filter: doc.getElementById('connect-filter') };
};

const rows = (list) => list.children.filter((c) => c.classList?.contains('connect-row'));
const rowUrls = (list) => rows(list).map((r) => r.dataset.url);
const hasClass = (el, c) => el.classList.contains(c);
const descendants = (el) => el.children.flatMap((c) => [c, ...(c.children ? descendants(c) : [])]);
const find = (el, cls) => descendants(el).find((c) => c.classList?.contains(cls)) ?? null;

test('an admin connection renders golden + badged; a session one does not', () => {
  const { list } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const [admin, plain] = rows(list);
  assert.ok(hasClass(admin, 'connect-admin'), 'the admin row wears the golden-outline class');
  assert.ok(!hasClass(plain, 'connect-admin'), 'a session-token row stays neutral');
  const badge = find(admin, 'connect-admin-badge');
  assert.ok(badge, 'the admin row carries the gold marker');
  assert.match(badge.title, /mint session tokens/i, 'the marker says what admin means');
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
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
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

const cssText = () => readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
// A rule body by its exact opening selector text (line breaks in the selector list
// make a single regex brittle).
const ruleAfter = (css, selector) => {
  const at = css.indexOf(selector);
  assert.ok(at > -1, `${selector} present`);
  return css.slice(at, css.indexOf('}', at) + 1);
};

test('the row disconnect is a trash button, like the projects modal’s remove', () => {
  const { list } = openModal([conn('http://plain:2', '')]);
  const disc = find(rows(list)[0], 'connect-disconnect');
  assert.ok(disc, 'every row can disconnect');
  assert.ok(disc.innerHTML.includes('ic-trash'), 'trash — this app’s remove glyph everywhere else');
  assert.ok(!disc.innerHTML.includes('ic-x'), 'the ✕ meant "close", not "forget this server"');
  assert.match(disc.title, /disconnect \(and forget\) this server/i, 'the tooltip says it forgets');
  assert.ok(disc.classList.contains('danger'), 'and it keeps the danger treatment');
});

test('the batch bar disconnects with the same trash, and still clears with an ✕', () => {
  const bar = /<div class="connect-batch-bar"[\s\S]*?<\/div>/.exec(markup)?.[0] ?? '';
  const btn = (id) => new RegExp(`<button id="${id}"[\\s\\S]*?</button>`).exec(bar)?.[0] ?? '';
  const disc = btn('connect-batch-disconnect');
  assert.ok(disc.includes('ic-trash'), 'the row and the batch bar agree on the glyph');
  assert.ok(!disc.includes('ic-x'));
  assert.ok(disc.includes('class="danger'), 'still the danger colour');
  assert.ok(btn('connect-batch-clear').includes('ic-x'),
    'clearing a selection is not a removal — ✕, exactly like projects-batch-clear');
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
  const layout = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
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
