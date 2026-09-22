// The "?" bubble's "Incognito — not saved" badge comes and goes in the SELECTED motion mode
// (js/ui/toolbar/toolbar.js through motion.js revealControls), the desktop's twin of
// MainWindow::refreshStatusHintVisibility. Pinned: a standing element, a cloud in the particle
// modes, the slot alone in `slide`, and a plain show/hide under `none` or reduced motion.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom } from '../../helpers/dom.js';

const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};
class StubObserver {
  constructor(fn) { this.fn = fn; StubObserver.all.push(this); }
  observe() {}
  disconnect() {}
}
StubObserver.all = [];
globalThis.MutationObserver = StubObserver;
globalThis.customElements = { define: () => {}, get: () => undefined };
globalThis.HTMLElement = class {};

let reduced = false;
const doc = installDom({ autoCreateById: true }, {
  matchMedia: (q) => ({ matches: q.includes('reduced-motion') ? reduced : false }),
  requestAnimationFrame: () => 1,
  cancelAnimationFrame: () => {},
  getComputedStyle: () => ({ getPropertyValue: () => '#7c3aed' }),
  window: { addEventListener: () => {}, removeEventListener: () => {}, dispatchEvent: () => {} },
});

const { StencilToolbar } = await import('../../../js/ui/toolbar/toolbar.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
const { settleMark, MARK_FORMING_CLASS, MARK_LEAVING_CLASS } =
  await import('../../../js/ui/motion.js');

const SLOT_CLASS = 'reveal-group-transition';

// The real wiring: a collapsed toolbar with an image open, so the bubble is live. The stub
// document is one per FILE, so each rig starts the bubble and its observers over.
const rig = () => {
  doc.getElementById('image-info').dataset.size = 'Image Size: 800 × 600 px';
  doc.body.classes.add('controls-collapsed');
  doc.body.classes.delete('incognito-mode');
  StubObserver.all.length = 0;
  const popup = doc.getElementById('hints-popup');
  popup.children.length = 0;
  popup.getBoundingClientRect = () => ({ left: 0, top: 0, width: 220, height: 44 });
  const bar = Object.create(StencilToolbar.prototype);
  bar.querySelector = () => null;
  bar.querySelectorAll = () => [];
  bar.wire({});
  const badge = popup.children.find((c) => c.className === 'hints-incognito');
  badge.getBoundingClientRect = () => ({ left: 0, top: 22, width: 186, height: 18 });
  const setIncognito = (on) => {
    doc.body.classList.toggle('incognito-mode', on);
    for (const o of StubObserver.all) o.fn();
  };
  return { popup, badge, setIncognito };
};

test('the badge stands in the bubble, hidden, so it has a box to fly', () => {
  const { popup, badge, setIncognito } = rig();
  assert.ok(badge, 'the bubble carries the incognito line from the first refresh');
  assert.equal(badge.style.display, 'none', 'hidden until the mode is on, never absent');
  // The size is a text node of its own — rewriting it must not take the badge with it.
  assert.equal(popup.children[0].nodeValue, 'Image Size: 800 × 600 px');
  assert.match(badge.innerHTML, /Incognito — not saved/);
  setMotionPrefs({ mode: 'none' });
  setIncognito(true);
  setIncognito(false);
  assert.equal(popup.children.find((c) => c.className === 'hints-incognito'), badge,
    'the same node survives a toggle — a rebuilt one could never be photographed');
  settleMark(badge);
});

test('in every particle mode the badge arrives and leaves as a cloud, over a sliding slot', () => {
  for (const mode of ['particles', 'water', 'fire']) {
    const { badge, setIncognito } = rig();
    setMotionPrefs({ mode });

    setIncognito(true);
    assert.equal(badge.style.display, 'flex', `${mode}: the badge takes its slot at once`);
    assert.ok(badge.__dustHost, `${mode}: no cloud gathered over the arriving badge`);
    assert.ok(badge.classes.has(MARK_FORMING_CLASS), `${mode}: the badge is not veiled behind its motes`);
    assert.ok(badge.classes.has(SLOT_CLASS), `${mode}: the slot did not open with the dust`);
    assert.equal(badge.style.maxWidth, '0px', 'the slot starts closed, not at a flash of full width');
    settleMark(badge);

    setIncognito(false);
    assert.ok(badge.__dustHost, `${mode}: the leaving badge did not come apart`);
    assert.ok(badge.classes.has(MARK_LEAVING_CLASS), `${mode}: the real badge outlived its motes`);
    assert.ok(badge.classes.has(SLOT_CLASS), `${mode}: the slot did not close behind it`);
    assert.equal(badge.style.display, 'flex', 'the badge holds its slot until the collapse ends');
    settleMark(badge);
  }
});

test('`slide` drops the particles and leaves the slot as the whole flight', () => {
  const { badge, setIncognito } = rig();
  setMotionPrefs({ mode: 'slide' });

  setIncognito(true);
  assert.equal(badge.style.display, 'flex');
  assert.ok(!badge.__dustHost, 'slide flies no particles');
  assert.ok(!badge.classes.has(MARK_FORMING_CLASS), 'and so there is nothing to veil the badge behind');
  assert.ok(badge.classes.has(SLOT_CLASS), 'the slot is what moves in slide');
  assert.equal(badge.style.maxWidth, '0px');
  setIncognito(false);
  assert.ok(!badge.__dustHost);
  assert.ok(badge.classes.has(SLOT_CLASS), 'and it closes the same way');
  settleMark(badge);
});

test('`none` and the OS preference show and hide it outright — no cloud, no slot, no veil', () => {
  for (const [what, mode, os] of [['none', 'none', false], ['reduced motion', 'particles', true]]) {
    const { badge, setIncognito } = rig();
    setMotionPrefs({ mode });
    reduced = os;

    setIncognito(true);
    assert.equal(badge.style.display, 'flex', `${what}: the badge must still appear`);
    assert.ok(!badge.__dustHost, `${what}: something moved`);
    assert.deepEqual([...badge.classes], ['hints-incognito'], `${what}: a motion class was left on`);
    assert.ok(!badge.style.maxWidth, `${what}: the slot was animated anyway`);

    setIncognito(false);
    assert.equal(badge.style.display, 'none', `${what}: the badge must go at once`);
    assert.ok(!badge.__dustHost);
    assert.deepEqual([...badge.classes], ['hints-incognito']);
    reduced = false;
    settleMark(badge);
  }
});

test('the badge is wired to the shared reveal, not to a per-mode effect of its own', () => {
  const src = readFileSync(new URL('../../../js/ui/toolbar/toolbar.js', import.meta.url), 'utf8');
  assert.match(src, /revealControls\(incognitoLine, live && incognito, 'flex'\);/);
  assert.match(src, /import \{[^}]*\brevealControls\b[^}]*\} from '\.\.\/motion\.js';/);
  // No mode of its own: every branch belongs to the shared helper.
  assert.ok(!/motionMode|dustEnabled|particleStyle/.test(src),
    'the toolbar must not fork on the motion mode itself');
  assert.match(src, /incognitoLine\.style\.display = 'none';/,
    'the standing badge starts hidden, so the first reveal is a real arrival');
});
