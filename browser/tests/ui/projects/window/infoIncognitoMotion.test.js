// The status line's "Incognito — not saved" badge comes and goes in the SELECTED motion mode
// (js/ui/projects/window/projectTitle.js through motion.js revealControls), the twin of the "?"
// bubble's line. Pinned: a standing pair, a cloud in the particle modes, the slot alone in
// `slide`, and a plain show/hide under `none` or reduced motion.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../../helpers/dom.js';

const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};

let reduced = false;
const doc = installDom({}, {
  matchMedia: (q) => ({ matches: q.includes('reduced-motion') ? reduced : false }),
  requestAnimationFrame: () => 1,
  cancelAnimationFrame: () => {},
  getComputedStyle: () => ({ getPropertyValue: () => '#7c3aed' }),
  location: { hash: '', pathname: '/app', search: '' },
  history: { replaceState: () => {} },
});

const { DrawingApp } = await import('../../../../js/core/drawingApp.js');
const { setMotionPrefs } = await import('../../../../js/ui/motion/motionPrefs.js');
const { settleMark, MARK_FORMING_CLASS, MARK_LEAVING_CLASS } =
  await import('../../../../js/ui/motion.js');

const SLOT_CLASS = 'reveal-group-transition';

// The real wiring: the app's own updateInfo over a fresh #image-info, driven by the one state
// flag the incognito toggle flips. A box big enough to grid is what makes a flight possible.
const rig = () => {
  const info = createStubElement('span', { id: 'image-info' });
  doc.register('image-info', info);
  const app = {
    image: {}, canvas: { width: 800, height: 600 },
    activeIsBlank: () => false, storage: { incognito: false },
  };
  const setIncognito = (on) => {
    app.storage.incognito = on;
    DrawingApp.prototype.updateInfo.call(app);
  };
  setIncognito(false);
  const { sep, tag } = info.__infoParts;
  tag.getBoundingClientRect = () => ({ left: 0, top: 22, width: 152, height: 18 });
  sep.getBoundingClientRect = () => ({ left: 0, top: 22, width: 9, height: 18 });
  return { info, sep, tag, setIncognito };
};

test('the badge stands in the info line, hidden, so it has a box to fly', () => {
  const { info, sep, tag, setIncognito } = rig();
  assert.equal(tag.className, 'info-incognito');
  assert.equal(tag.style.display, 'none', 'hidden until the mode is on, never absent');
  assert.equal(sep.style.display, 'none', 'and the divider only exists with the tag');
  // The size is a text node of its own — rewriting it must not take the pair with it.
  assert.equal(info.children[0].nodeValue, 'Image Size: 800 × 600 px');
  assert.equal(info.dataset.size, 'Image Size: 800 × 600 px');
  assert.match(tag.innerHTML, /Incognito — not saved/);
  setMotionPrefs({ mode: 'none' });
  setIncognito(true);
  setIncognito(false);
  assert.equal(info.__infoParts.tag, tag,
    'the same node survives a toggle — a rebuilt one could never be photographed');
  settleMark(tag);
});

test('in every particle mode the badge arrives and leaves as a cloud, over a sliding slot', () => {
  for (const mode of ['particles', 'water', 'fire']) {
    const { sep, tag, setIncognito } = rig();
    setMotionPrefs({ mode });

    setIncognito(true);
    assert.equal(tag.style.display, 'inline-flex', `${mode}: the badge takes its slot at once`);
    assert.equal(sep.style.display, 'inline', `${mode}: the divider comes with it`);
    assert.ok(tag.__dustHost, `${mode}: no cloud gathered over the arriving badge`);
    assert.ok(tag.classes.has(MARK_FORMING_CLASS), `${mode}: the badge is not veiled behind its motes`);
    assert.ok(tag.classes.has(SLOT_CLASS), `${mode}: the slot did not open with the dust`);
    assert.equal(tag.style.maxWidth, '0px', 'the slot starts closed, not at a flash of full width');
    settleMark(tag);
    settleMark(sep);

    setIncognito(false);
    assert.ok(tag.__dustHost, `${mode}: the leaving badge did not come apart`);
    assert.ok(tag.classes.has(MARK_LEAVING_CLASS), `${mode}: the real badge outlived its motes`);
    assert.ok(tag.classes.has(SLOT_CLASS), `${mode}: the slot did not close behind it`);
    assert.equal(tag.style.display, 'inline-flex', 'the badge holds its slot until the collapse ends');
    settleMark(tag);
    settleMark(sep);
  }
});

test('`slide` drops the particles and leaves the slot as the whole flight', () => {
  const { sep, tag, setIncognito } = rig();
  setMotionPrefs({ mode: 'slide' });

  setIncognito(true);
  assert.equal(tag.style.display, 'inline-flex');
  assert.ok(!tag.__dustHost, 'slide flies no particles');
  assert.ok(!tag.classes.has(MARK_FORMING_CLASS), 'and so there is nothing to veil the badge behind');
  assert.ok(tag.classes.has(SLOT_CLASS), 'the slot is what moves in slide');
  assert.equal(tag.style.maxWidth, '0px');
  setIncognito(false);
  assert.ok(!tag.__dustHost);
  assert.ok(tag.classes.has(SLOT_CLASS), 'and it closes the same way');
  assert.ok(sep.classes.has(SLOT_CLASS), 'the divider rides the same slot');
  settleMark(tag);
  settleMark(sep);
});

test('`none` and the OS preference show and hide it outright — no cloud, no slot, no veil', () => {
  for (const [what, mode, os] of [['none', 'none', false], ['reduced motion', 'particles', true]]) {
    const { sep, tag, setIncognito } = rig();
    setMotionPrefs({ mode });
    reduced = os;

    setIncognito(true);
    assert.equal(tag.style.display, 'inline-flex', `${what}: the badge must still appear`);
    assert.ok(!tag.__dustHost, `${what}: something moved`);
    assert.deepEqual([...tag.classes], ['info-incognito'], `${what}: a motion class was left on`);
    assert.ok(!tag.style.maxWidth, `${what}: the slot was animated anyway`);

    setIncognito(false);
    assert.equal(tag.style.display, 'none', `${what}: the badge must go at once`);
    assert.equal(sep.style.display, 'none', `${what}: the divider outlived the tag`);
    assert.ok(!tag.__dustHost);
    assert.deepEqual([...tag.classes], ['info-incognito']);
    reduced = false;
    settleMark(tag);
    settleMark(sep);
  }
});
