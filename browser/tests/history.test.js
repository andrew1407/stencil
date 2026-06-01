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
