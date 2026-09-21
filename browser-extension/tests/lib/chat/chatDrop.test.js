// Tests for the assistant section's drag-and-drop layer (src/lib/drop.js):
// payload classification (which must REUSE lib/dragUrl.js extractDraggedUrl for
// cross-page image drags) and the drop-highlight wiring — driven with stub
// DataTransfer objects and stub elements, no real DnD.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DRAG_TYPES, isDropCandidate, isVideoFile, classifyDrop, wireDropTarget, leftTarget } from '../../../src/lib/chat/drop.js';

// ── Test doubles ──
const stubDataTransfer = ({ types = [], data = {}, files = [] } = {}) => ({
  types,
  files,
  dropEffect: '',
  getData: (t) => data[t] || '',
});

const stubEvent = (dt) => {
  const e = { prevented: false, relatedTarget: null, dataTransfer: dt };
  e.preventDefault = () => { e.prevented = true; };
  return e;
};

// `contains` mirrors the real Node API: children of the target report true, so a
// dragleave between the transcript's own messages doesn't clear the cue.
const inside = { __inside: true };
const outside = { __inside: false };
const stubRoot = () => {
  const listeners = {};
  return {
    addEventListener: (type, fn) => { (listeners[type] ||= []).push(fn); },
    dispatch: (type, e) => { for (const fn of listeners[type] || []) fn(e); },
    contains: (n) => !!(n && n.__inside),
  };
};

const stubHighlight = () => {
  const classes = new Set();
  const toggles = [];
  return {
    classes, toggles,
    classList: { toggle: (c, on) => { toggles.push(on); if (on) classes.add(c); else classes.delete(c); } },
  };
};

// ── isDropCandidate ──
test('isDropCandidate accepts files and any URL-bearing text payload', () => {
  assert.equal(isDropCandidate(['Files']), true);
  assert.equal(isDropCandidate(['text/uri-list']), true);
  assert.equal(isDropCandidate(['text/html', 'text/plain']), true);
  assert.equal(isDropCandidate(['text/x-moz-url']), true);
  assert.equal(isDropCandidate(['application/x-something']), false);
  assert.equal(isDropCandidate([]), false);
  assert.equal(isDropCandidate(null), false);
  // DataTransfer.types is array-like, not a real Array — an iterable works too.
  assert.equal(isDropCandidate(new Set(['text/plain'])), true);
  assert.deepEqual(DRAG_TYPES.slice(0, 1), ['Files']);
});

// ── isVideoFile (mirror of the browser helper) ──
test('isVideoFile keys on MIME, falling back to the extension', () => {
  assert.equal(isVideoFile({ type: 'video/mp4', name: 'a.mp4' }), true);
  assert.equal(isVideoFile({ type: '', name: 'clip.webm' }), true);
  assert.equal(isVideoFile({ type: 'image/png', name: 'a.png' }), false);
  assert.equal(isVideoFile({ type: '', name: 'notes.txt' }), false);
  assert.equal(isVideoFile(null), false);
});

// ── classifyDrop ──
test('classifyDrop: local files win over any text payload', () => {
  const files = [{ name: 'a.png', type: 'image/png' }];
  const dt = stubDataTransfer({ types: ['Files', 'text/uri-list'], files, data: { 'text/uri-list': 'https://x/y.png' } });
  assert.deepEqual(classifyDrop(dt), { kind: 'files', files });
});

test('classifyDrop reuses extractDraggedUrl: the <img> in text/html beats the link in text/uri-list', () => {
  // The dragUrl.js linked-image case: an image wrapped in a link puts the LINK in
  // text/uri-list but the real image src in text/html — we must take the image.
  const dt = stubDataTransfer({
    types: ['text/uri-list', 'text/html'],
    data: {
      'text/uri-list': 'https://github.com/andrew1407/stencil/blob/main/bot/assets/icon.png',
      'text/html': '<a href="/andrew1407/stencil/blob/main/bot/assets/icon.png"><img src="https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png"></a>',
    },
  });
  assert.deepEqual(classifyDrop(dt), { kind: 'url', url: 'https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png' });
});

test('classifyDrop falls through uri-list → plain text, and rejects junk', () => {
  assert.deepEqual(
    classifyDrop(stubDataTransfer({ types: ['text/uri-list'], data: { 'text/uri-list': '# comment\nhttps://a/b.png' } })),
    { kind: 'url', url: 'https://a/b.png' });
  assert.deepEqual(
    classifyDrop(stubDataTransfer({ types: ['text/plain'], data: { 'text/plain': 'https://a/c.jpg' } })),
    { kind: 'url', url: 'https://a/c.jpg' });
  assert.equal(classifyDrop(stubDataTransfer({ types: ['text/plain'], data: { 'text/plain': 'just words' } })), null);
  assert.equal(classifyDrop(null), null);
});

// ── wireDropTarget: drop-highlight markup + dispatch (stub elements, no DnD) ──
test('drag-over of a candidate highlights; leaving the window clears it', () => {
  const root = stubRoot();
  const highlight = stubHighlight();
  wireDropTarget(root, { highlight, onDrop: () => {} });

  const enter = stubEvent(stubDataTransfer({ types: ['text/uri-list'] }));
  root.dispatch('dragenter', enter);
  assert.equal(enter.prevented, true);
  assert.ok(highlight.classes.has('drop-over'));
  assert.equal(enter.dataTransfer.dropEffect, 'copy');

  // dragleave into a child (relatedTarget still inside the section) keeps it…
  const toChild = stubEvent(stubDataTransfer());
  toChild.relatedTarget = inside;
  root.dispatch('dragleave', toChild);
  assert.ok(highlight.classes.has('drop-over'));

  // …moving out to a sibling (inside the window, outside the section) clears it —
  // the old `relatedTarget === null` test left the cue stuck on.
  const toSibling = stubEvent(stubDataTransfer());
  toSibling.relatedTarget = outside;
  root.dispatch('dragleave', toSibling);
  assert.ok(!highlight.classes.has('drop-over'));

  // …and leaving the window (relatedTarget null) clears it too.
  root.dispatch('dragenter', stubEvent(stubDataTransfer({ types: ['text/uri-list'] })));
  assert.ok(highlight.classes.has('drop-over'));
  root.dispatch('dragleave', stubEvent(stubDataTransfer()));
  assert.ok(!highlight.classes.has('drop-over'));
});

test('leftTarget: null relatedTarget always counts as leaving', () => {
  const root = stubRoot();
  assert.equal(leftTarget(root, null), true);
  assert.equal(leftTarget(root, inside), false);
  assert.equal(leftTarget(root, outside), true);
  // No contains() (a detached stub) → treat an internal move as staying.
  assert.equal(leftTarget({}, outside), false);
});

test('the cue class is only toggled on a CHANGE (dragover fires continuously)', () => {
  const root = stubRoot();
  const highlight = stubHighlight();
  const { isOver } = wireDropTarget(root, { highlight, onDrop: () => {} });

  for (let i = 0; i < 5; i++) root.dispatch('dragover', stubEvent(stubDataTransfer({ types: ['text/uri-list'] })));
  assert.deepEqual(highlight.toggles, [true], 'no re-toggling under the cursor (that was the flicker)');
  assert.equal(isOver(), true);

  root.dispatch('dragleave', stubEvent(stubDataTransfer()));
  root.dispatch('dragleave', stubEvent(stubDataTransfer()));
  assert.deepEqual(highlight.toggles, [true, false]);
  assert.equal(isOver(), false);
});

test('a drag that ends elsewhere (or is cancelled) never leaves the cue painted', () => {
  const root = stubRoot();
  const highlight = stubHighlight();
  wireDropTarget(root, { highlight, onDrop: () => {} });
  root.dispatch('dragover', stubEvent(stubDataTransfer({ types: ['Files'] })));
  assert.ok(highlight.classes.has('drop-over'));
  root.dispatch('dragend', stubEvent(stubDataTransfer()));
  assert.ok(!highlight.classes.has('drop-over'));
});

test('a non-candidate drag is ignored (no highlight, browser default kept)', () => {
  const root = stubRoot();
  const highlight = stubHighlight();
  wireDropTarget(root, { highlight, onDrop: () => {} });
  const e = stubEvent(stubDataTransfer({ types: ['application/x-other'] }));
  root.dispatch('dragenter', e);
  root.dispatch('dragover', e);
  assert.equal(e.prevented, false);
  assert.ok(!highlight.classes.has('drop-over'));
});

test('drop delivers the classified payload and clears the highlight', () => {
  const root = stubRoot();
  const highlight = stubHighlight();
  const dropped = [];
  wireDropTarget(root, { highlight, onDrop: (p) => dropped.push(p) });

  root.dispatch('dragover', stubEvent(stubDataTransfer({ types: ['text/plain'] })));
  assert.ok(highlight.classes.has('drop-over'));

  const e = stubEvent(stubDataTransfer({ types: ['text/plain'], data: { 'text/plain': 'https://a/pic.png' } }));
  root.dispatch('drop', e);
  assert.equal(e.prevented, true);
  assert.ok(!highlight.classes.has('drop-over'));
  assert.deepEqual(dropped, [{ kind: 'url', url: 'https://a/pic.png' }]);
});

test('an unusable drop clears the highlight but fires nothing', () => {
  const root = stubRoot();
  const highlight = stubHighlight();
  const dropped = [];
  wireDropTarget(root, { highlight, onDrop: (p) => dropped.push(p) });
  root.dispatch('dragover', stubEvent(stubDataTransfer({ types: ['text/plain'] })));
  const e = stubEvent(stubDataTransfer({ types: ['text/plain'], data: { 'text/plain': 'nothing urlish' } }));
  root.dispatch('drop', e);
  assert.equal(e.prevented, false);
  assert.deepEqual(dropped, []);
  assert.ok(!highlight.classes.has('drop-over'));
});
