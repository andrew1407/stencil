import { test } from 'node:test';
import assert from 'node:assert';
import { HistoryStack } from '../js/core/historyStack.js';

test('fresh stack cannot undo at base', () => {
    const h = new HistoryStack();
    assert.strictEqual(h.canUndo(), false);
    assert.strictEqual(h.canRedo(), false);
});

test('push deep-copies the snapshot (mutating original does not change stored)', () => {
    const h = new HistoryStack();
    const lines = [{ points: [{ x: 1, y: 2 }] }];
    h.push(lines);             // step 0
    h.push(lines);             // step 1 (so undo() returns the step-0 snapshot)
    lines[0].points[0].x = 999;
    const restored = h.undo(); // back to step 0
    assert.strictEqual(restored[0].points[0].x, 1);
});

test('undo returns prior snapshot; redo returns next; canRedo false at top', () => {
    const h = new HistoryStack();
    h.push([{ id: 'a' }]); // step 0
    h.push([{ id: 'b' }]); // step 1
    assert.strictEqual(h.canRedo(), false);
    const u = h.undo();    // step 0
    assert.deepStrictEqual(u, [{ id: 'a' }]);
    assert.strictEqual(h.canRedo(), true);
    const r = h.redo();    // step 1
    assert.deepStrictEqual(r, [{ id: 'b' }]);
    assert.strictEqual(h.canRedo(), false);
});

test('push after undo truncates the redo branch', () => {
    const h = new HistoryStack();
    h.push([{ id: 'a' }]); // 0
    h.push([{ id: 'b' }]); // 1
    h.push([{ id: 'c' }]); // 2
    h.undo();              // 1
    h.undo();              // 0
    h.push([{ id: 'd' }]); // 1 (truncates b,c... here from slice(0,1))
    assert.strictEqual(h.canRedo(), false);
    assert.strictEqual(h.historyStep, 1);
    assert.deepStrictEqual(h.history[1], [{ id: 'd' }]);
});

test('step-to-empty semantics: undo at step 0 → empty lines, step -1', () => {
    const h = new HistoryStack();
    h.push([{ id: 'a' }]); // step 0
    const u = h.undo();    // step 0 → [], step -1
    assert.deepStrictEqual(u, []);
    assert.strictEqual(h.historyStep, -1);
    assert.strictEqual(h.undo(), null); // nothing further
});

test('reset initializes from base lines', () => {
    const h = new HistoryStack();
    h.reset([{ id: 'x' }]); // lines.length > 0 → step 0
    assert.strictEqual(h.historyStep, 0);
    assert.strictEqual(h.canUndo(), true);
    h.reset([]);            // empty → step -1
    assert.strictEqual(h.historyStep, -1);
    assert.strictEqual(h.canUndo(), false);
});

test('explicit baseStep=0 on empty lines is a phantom-undo trap; callers must pass -1', () => {
    // The project-restore path (storage.js) forces an explicit baseStep. Passing 0 with no
    // lines seeds a phantom snapshot, so a brand-new/blank project shows an enabled Undo.
    const trap = new HistoryStack();
    trap.reset([], 0);
    assert.strictEqual(trap.canUndo(), true);  // the bug: undo available on a blank
    // The fix: guard the step (lines.length ? 0 : -1), so empty lines stay non-undoable.
    const fixed = new HistoryStack();
    fixed.reset([], [].length ? 0 : -1);
    assert.strictEqual(fixed.canUndo(), false);
    assert.strictEqual(fixed.canRedo(), false);
});

test('reset with empty lines leaves NO redo (no stray redo step after a blank)', () => {
    const h = new HistoryStack();
    h.push([{ id: 'a' }]);  // simulate prior edits
    h.reset([]);            // e.g. creating a blank image / fresh load
    assert.strictEqual(h.canUndo(), false);
    assert.strictEqual(h.canRedo(), false); // was true: phantom empty snapshot
    assert.strictEqual(h.redo(), null);
    // A real edit after reset still undoes back to empty and redoes forward.
    h.push([{ id: 'b' }]);
    assert.strictEqual(h.canUndo(), true);
    assert.deepStrictEqual(h.undo(), []);
    assert.strictEqual(h.canRedo(), true);
    assert.deepStrictEqual(h.redo(), [{ id: 'b' }]);
});
