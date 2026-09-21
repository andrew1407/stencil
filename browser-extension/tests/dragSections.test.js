// Tests for src/lib/dragSections.js — the SPRING-LOADED rules that unfold a collapsed
// drop target while a drag is live (a `display:none` section body can never accept a
// drop) and fold it back if the drag ends somewhere else. Nothing opens up front: the
// section the POINTER dwells on opens, and only that one. Pure decision + bookkeeping;
// every DOM action and both clocks are injected, so this runs under plain `node --test`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  ASSISTANT_SECTION, SEARCH_SECTION, RESTORE_DELAY_MS, SPRING_DWELL_MS, DRAG_KINDS,
  sectionForDragPoint, createDragSectionOpener,
} from '../src/lib/drop/dragSections.js';

// A stub surface: `collapsed` is the live state, every expand/collapse is recorded, and
// timers are held so a test decides whether the dwell elapses.
const stubSurface = (collapsed = {}, sections = [ASSISTANT_SECTION, SEARCH_SECTION]) => {
  const state = { ...collapsed };
  const calls = [];
  const opened = [];
  const timers = [];
  const opener = createDragSectionOpener({
    sections,
    isCollapsed: (id) => !!state[id],
    expand: (id) => { state[id] = false; calls.push(['expand', id]); },
    collapse: (id) => { state[id] = true; calls.push(['collapse', id]); },
    onOpen: (id) => opened.push(id),
    timer: (fn, ms) => { timers.push({ fn, ms }); return timers.length; },
    clearTimer: (h) => { if (timers[h - 1]) timers[h - 1].cancelled = true; },
  });
  // Fire every timer armed so far — cancelled ones included, since the controller's own
  // staleness guards must make those no-ops exactly as a racing real clock would.
  const tick = () => { for (const t of timers) if (!t.fired) { t.fired = true; t.fn(); } };
  return { opener, state, calls, opened, timers, tick };
};

// ── The decision: which section is under the pointer ──

test('sectionForDragPoint springs the hovered collapsed section, and nothing else', () => {
  const ctx = { collapsed: { [ASSISTANT_SECTION]: true }, sections: [ASSISTANT_SECTION, SEARCH_SECTION] };
  assert.equal(sectionForDragPoint('internal', ASSISTANT_SECTION, ctx), ASSISTANT_SECTION);
  assert.equal(sectionForDragPoint('files', ASSISTANT_SECTION, ctx), ASSISTANT_SECTION);
  assert.equal(sectionForDragPoint('url', ASSISTANT_SECTION, ctx), ASSISTANT_SECTION);
  // Over no section, or over one that isn't collapsed → nothing springs.
  assert.equal(sectionForDragPoint('files', '', ctx), '');
  assert.equal(sectionForDragPoint('files', SEARCH_SECTION, ctx), '');
});

test('sectionForDragPoint: the payload gates it, the surface bounds it', () => {
  const collapsed = { [ASSISTANT_SECTION]: true, [SEARCH_SECTION]: true };
  const both = { collapsed, sections: [ASSISTANT_SECTION, SEARCH_SECTION] };
  assert.equal(sectionForDragPoint('text', ASSISTANT_SECTION, both), '', 'a non-image drag springs nothing');
  assert.equal(sectionForDragPoint('', ASSISTANT_SECTION, both), '');
  // An INTERNAL row drag springs the list too, where the list is a drop target…
  assert.equal(sectionForDragPoint('internal', SEARCH_SECTION, both), SEARCH_SECTION);
  // …and on a surface without drag-to-pin (the popup) it springs only the chat.
  const chatOnly = { collapsed, sections: [ASSISTANT_SECTION] };
  assert.equal(sectionForDragPoint('internal', SEARCH_SECTION, chatOnly), '');
  assert.equal(sectionForDragPoint('files', SEARCH_SECTION, chatOnly), '');
  assert.equal(sectionForDragPoint('files', ASSISTANT_SECTION, chatOnly), ASSISTANT_SECTION);
  assert.deepEqual(DRAG_KINDS, ['internal', 'files', 'url', 'external']);
});

// ── Springing open ──

test('dwelling on a collapsed header springs ONLY that section', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true, [SEARCH_SECTION]: true });
  s.opener.pointerOver('files', ASSISTANT_SECTION);
  assert.equal(s.timers[0].ms, SPRING_DWELL_MS);
  assert.deepEqual(s.calls, [], 'nothing opens before the dwell elapses');
  s.tick();
  assert.deepEqual(s.calls, [['expand', ASSISTANT_SECTION]]);
  assert.equal(s.state[SEARCH_SECTION], true, 'the section the pointer never visited stays folded');
  assert.deepEqual(s.opened, [ASSISTANT_SECTION]);
  assert.deepEqual(s.opener.pendingRestore(), [ASSISTANT_SECTION]);
});

test('sweeping across a header without dwelling springs nothing', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true, [SEARCH_SECTION]: true });
  s.opener.pointerOver('files', SEARCH_SECTION);   // passing over…
  s.opener.pointerOver('files', '');               // …and straight off it again
  assert.equal(s.opener.armedSection(), '');
  s.tick();                                        // the stale dwell still fires
  assert.deepEqual(s.calls, [], 'a cancelled dwell must not open anything');
  assert.equal(s.state[SEARCH_SECTION], true);
});

test('moving from one collapsed header to another re-arms for the new one only', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true, [SEARCH_SECTION]: true });
  s.opener.pointerOver('files', SEARCH_SECTION);
  s.opener.pointerOver('files', ASSISTANT_SECTION);
  assert.equal(s.opener.armedSection(), ASSISTANT_SECTION);
  s.tick();
  assert.deepEqual(s.calls, [['expand', ASSISTANT_SECTION]]);
});

test('the dwell is not restarted by the dragover stream over the same section', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true });
  for (let i = 0; i < 6; i++) s.opener.pointerOver('internal', ASSISTANT_SECTION);
  assert.equal(s.timers.length, 1, 'one dwell timer, not one per dragover');
  s.tick();
  assert.deepEqual(s.calls, [['expand', ASSISTANT_SECTION]]);
});

test('a non-image drag, or a section already open, springs nothing', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: false });
  s.opener.pointerOver('files', ASSISTANT_SECTION);   // already open
  s.opener.pointerOver('text', SEARCH_SECTION);       // not an image/video payload
  assert.equal(s.timers.length, 0);
  s.tick();
  assert.deepEqual(s.calls, []);
});

// ── Folding back ──

test('a drag that ends elsewhere folds back exactly what it sprang open', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true, [SEARCH_SECTION]: false });
  s.opener.pointerOver('files', ASSISTANT_SECTION);
  s.tick();
  assert.equal(s.state[ASSISTANT_SECTION], false);
  s.opener.end();
  assert.equal(s.state[ASSISTANT_SECTION], true, 'sprang open → folded back');
  assert.equal(s.state[SEARCH_SECTION], false, 'was already open → left alone');
  assert.deepEqual(s.opener.pendingRestore(), []);
  s.opener.end();                       // a second end() is a no-op
  assert.deepEqual(s.calls.filter((c) => c[0] === 'collapse'), [['collapse', ASSISTANT_SECTION]]);
});

test('a drop INTO a sprung section keeps it open', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true, [SEARCH_SECTION]: true });
  s.opener.pointerOver('files', SEARCH_SECTION);
  s.tick();
  s.opener.pointerOver('files', ASSISTANT_SECTION);
  s.tick();
  s.opener.dropIn(ASSISTANT_SECTION);
  s.opener.end();
  assert.equal(s.state[ASSISTANT_SECTION], false, 'they dropped there — it stays');
  assert.equal(s.state[SEARCH_SECTION], true, 'the other one folds back');
});

test('a manual toggle mid-drag cancels the fold-back for that section', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true, [SEARCH_SECTION]: true });
  s.opener.pointerOver('files', SEARCH_SECTION);
  s.tick();
  s.opener.pointerOver('files', ASSISTANT_SECTION);
  s.tick();
  s.opener.manualToggle(ASSISTANT_SECTION);   // the user took charge of this one
  s.opener.end();
  assert.deepEqual(s.calls.filter((c) => c[0] === 'collapse'), [['collapse', SEARCH_SECTION]]);
});

test('leaving the window only SCHEDULES the fold-back, and coming back cancels it', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true });
  s.opener.pointerOver('internal', ASSISTANT_SECTION);
  s.tick();
  assert.equal(s.state[ASSISTANT_SECTION], false);

  s.opener.scheduleEnd();
  const restoreTimer = s.timers[s.timers.length - 1];
  assert.equal(restoreTimer.ms, RESTORE_DELAY_MS);
  assert.equal(s.state[ASSISTANT_SECTION], false, 'nothing collapses mid-drag');

  s.opener.pointerOver('internal', '');   // the drag came back into the surface
  assert.equal(restoreTimer.cancelled, true);
  restoreTimer.fired = true;
  restoreTimer.fn();                      // a stale timer that still fires…
  assert.equal(s.state[ASSISTANT_SECTION], false, '…must not fold it back');
});

test('the scheduled fold-back runs when the drag never returns', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true });
  s.opener.pointerOver('internal', ASSISTANT_SECTION);
  s.tick();
  s.opener.scheduleEnd();
  s.opener.scheduleEnd();                 // repeated dragleaves arm one timer only
  assert.equal(s.timers.filter((t) => t.ms === RESTORE_DELAY_MS).length, 1);
  s.tick();
  assert.equal(s.state[ASSISTANT_SECTION], true);
  assert.deepEqual(s.opener.pendingRestore(), []);
});

test('leaving the window kills a dwell that had not sprung yet', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: true });
  s.opener.pointerOver('internal', ASSISTANT_SECTION);
  s.opener.scheduleEnd();
  assert.equal(s.opener.armedSection(), '');
  s.tick();
  assert.deepEqual(s.calls, []);
});

test('nothing sprang open → no restore timer is armed at all', () => {
  const s = stubSurface({ [ASSISTANT_SECTION]: false });
  s.opener.pointerOver('files', ASSISTANT_SECTION);
  s.opener.scheduleEnd();
  assert.equal(s.timers.length, 0);
});
