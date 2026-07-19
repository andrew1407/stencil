// Unit tests for the pure live-file-sync classifier (js/core/stencilSync.js).
// The controller itself is DOM/File-System-Access-bound; this pins the branch logic that
// decides apply-vs-prompt when the linked .stencil changes.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { classifyFileChange } from '../js/core/stencilSync.js';

test('classifyFileChange: nothing changed → none', () => {
  assert.equal(classifyFileChange('A', 'A', 'A'), 'none');
});

test('classifyFileChange: only the editor changed → local (autosave pushes it)', () => {
  assert.equal(classifyFileChange('A', 'B', 'A'), 'local');
});

test('classifyFileChange: only the file changed → external (apply in place)', () => {
  assert.equal(classifyFileChange('A', 'A', 'C'), 'external');
});

test('classifyFileChange: BOTH changed since baseline → conflict (prompt)', () => {
  assert.equal(classifyFileChange('A', 'B', 'C'), 'conflict');
});

test('classifyFileChange: same edit on both sides is still a conflict (independent changes)', () => {
  // editor and file both moved to "B" independently; baseline was "A" → we can't tell they agree
  // by content alone here (the caller re-serializes), so both-changed = conflict is the safe call.
  assert.equal(classifyFileChange('A', 'B', 'B'), 'conflict');
});
