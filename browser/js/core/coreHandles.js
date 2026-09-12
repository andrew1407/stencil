// ── The core's state/ group: handle classes + project rules ─────
// stencilCore.js wraps the core's pure functions; the classes in core/state/ own
// state, so they live in wasm memory behind an opaque int handle (create/destroy
// plus operations) exported by core/wasmStateApi.cpp. Each wrapper mirrors its JS
// twin's API exactly, so browser/tests/wasm-parity-state.test.js can drive both
// through the same script and pin them op-for-op. The group's scalar-only rules
// (projectRules.js) ride along here, so stencilCore.js has one entry point.

import { encodeLines, decodeLines } from './linesCodec.js';
import { buildProjectRules, projectRuleExports } from './projectRules.js';

const F64 = 8;
const I32 = 4;

// HoldAction codes, in core/state/holdDraw.hpp order (0 = None → no event).
const HOLD_ACTIONS = [null, 'armed', 'abort', 'start', 'drop', 'preview', 'commit'];
// The actions that carry coordinates; the rest are bare {type} objects, as in holdDraw.js.
const HOLD_XY = new Set(['start', 'drop', 'preview']);
// HoldState codes.
const HOLD_STATES = ['idle', 'armed', 'drawing', 'aborted'];

// Build the wasm-backed twin of holdDraw.js's HoldDrawController. Instances own a
// core handle plus a 2-double out slot, released by destroy().
const holdDrawClass = (mod) => {
  const c = {
    create: mod.cwrap('stencil_holdDraw_create', 'number', ['number', 'number', 'number']),
    destroy: mod.cwrap('stencil_holdDraw_destroy', null, ['number']),
    state: mod.cwrap('stencil_holdDraw_state', 'number', ['number']),
    getDelay: mod.cwrap('stencil_holdDraw_holdDelay', 'number', ['number']),
    setDelay: mod.cwrap('stencil_holdDraw_setHoldDelay', null, ['number', 'number']),
    cancel: mod.cwrap('stencil_holdDraw_cancel', null, ['number']),
    down: mod.cwrap('stencil_holdDraw_pointerDown', 'number', ['number', 'number', 'number', 'number', 'number']),
    move: mod.cwrap('stencil_holdDraw_pointerMove', 'number', ['number', 'number', 'number', 'number', 'number']),
    tick: mod.cwrap('stencil_holdDraw_tick', 'number', ['number', 'number', 'number']),
    up: mod.cwrap('stencil_holdDraw_pointerUp', 'number', ['number', 'number']),
  };

  return class HoldDrawController {
    #handle;
    #out;

    constructor({ holdDelay = 500, moveTolerance = 6, rearmDistance = 10 } = {}) {
      this.#handle = c.create(holdDelay, moveTolerance, rearmDistance);
      this.#out = mod._malloc(2 * F64);
    }

    // Release the core handle and its out slot. Idempotent; the instance is unusable after.
    destroy() {
      if (!this.#handle) return;
      c.destroy(this.#handle);
      mod._free(this.#out);
      this.#handle = 0;
    }

    get state() { return HOLD_STATES[c.state(this.#live())]; }
    get active() { return this.state === 'drawing'; }
    get engaged() { const s = this.state; return s === 'armed' || s === 'drawing'; }
    get holdDelay() { return c.getDelay(this.#live()); }

    setHoldDelay(ms) {
      const n = Number(ms);
      if (Number.isFinite(n) && n >= 0) c.setDelay(this.#live(), n);
    }

    cancel() { c.cancel(this.#live()); }

    pointerDown(x, y, t) { return this.#event(c.down(this.#live(), x, y, t, this.#out)); }
    pointerMove(x, y, t) { return this.#event(c.move(this.#live(), x, y, t, this.#out)); }
    tick(t) { return this.#event(c.tick(this.#live(), t, this.#out)); }
    pointerUp(t) { return this.#event(c.up(this.#live(), t, this.#out)); }

    #live() {
      if (!this.#handle) throw new Error('hold-draw controller destroyed');
      return this.#handle;
    }

    // Decode an action code into the exact object shape holdDraw.js returns.
    #event(code) {
      const type = HOLD_ACTIONS[code];
      if (!type) return null;
      if (!HOLD_XY.has(type)) return { type };
      return { type, x: mod.getValue(this.#out, 'double'), y: mod.getValue(this.#out + F64, 'double') };
    }
  };
};

// Build the wasm-backed twin of historyStack.js's HistoryStack. Snapshots cross as the
// flat (nums, text) pair from linesCodec.js, in both directions.
const historyClass = (mod) => {
  const NUM = ['number', 'number', 'number', 'number', 'number', 'number', 'number'];
  const c = {
    create: mod.cwrap('stencil_history_create', 'number', []),
    destroy: mod.cwrap('stencil_history_destroy', null, ['number']),
    reset: mod.cwrap('stencil_history_reset', null, NUM),
    push: mod.cwrap('stencil_history_push', null, ['number', 'number', 'number', 'number', 'number']),
    canUndo: mod.cwrap('stencil_history_canUndo', 'number', ['number']),
    canRedo: mod.cwrap('stencil_history_canRedo', 'number', ['number']),
    step: mod.cwrap('stencil_history_step', 'number', ['number']),
    size: mod.cwrap('stencil_history_size', 'number', ['number']),
    undo: mod.cwrap('stencil_history_undo', 'number', ['number', 'number']),
    redo: mod.cwrap('stencil_history_redo', 'number', ['number', 'number']),
    read: mod.cwrap('stencil_history_readResult', null, ['number', 'number', 'number']),
  };

  // Copy an encoded snapshot onto the heap, run `use(numsPtr, numsLen, textPtr, textLen)`,
  // free after. A zero-length buffer still gets a byte, so the pointer is never null.
  const withSnapshot = (lines, use) => {
    const { nums, text } = encodeLines(lines);
    const numsPtr = mod._malloc(Math.max(1, nums.length * F64));
    const textPtr = mod._malloc(Math.max(1, text.length));
    try {
      new Float64Array(mod.HEAPF64.buffer, numsPtr, nums.length).set(nums);
      mod.HEAPU8.set(text, textPtr);
      return use(numsPtr, nums.length, textPtr, text.length);
    } finally {
      mod._free(numsPtr);
      mod._free(textPtr);
    }
  };

  return class HistoryStack {
    #handle;
    #sizes;

    constructor() {
      this.#handle = c.create();
      this.#sizes = mod._malloc(2 * I32);
    }

    // Release the core handle and its out slot. Idempotent.
    destroy() {
      if (!this.#handle) return;
      c.destroy(this.#handle);
      mod._free(this.#sizes);
      this.#handle = 0;
    }

    get historyStep() { return c.step(this.#live()); }
    get size() { return c.size(this.#live()); }

    reset(lines, baseStep) {
      const has = baseStep === undefined ? 0 : 1;
      withSnapshot(lines, (n, nl, t, tl) => c.reset(this.#live(), has, has ? baseStep : 0, n, nl, t, tl));
    }

    push(lines) {
      withSnapshot(lines, (n, nl, t, tl) => c.push(this.#live(), n, nl, t, tl));
    }

    canUndo() { return c.canUndo(this.#live()) === 1; }
    canRedo() { return c.canRedo(this.#live()) === 1; }
    undo() { return this.#result(c.undo(this.#live(), this.#sizes)); }
    redo() { return this.#result(c.redo(this.#live(), this.#sizes)); }

    #live() {
      if (!this.#handle) throw new Error('history stack destroyed');
      return this.#handle;
    }

    // 0 from undo/redo is the JS null; otherwise read the snapshot the sizes describe.
    #result(ok) {
      if (!ok) return null;
      const numsLen = mod.getValue(this.#sizes, 'i32');
      const textLen = mod.getValue(this.#sizes + I32, 'i32');
      const numsPtr = mod._malloc(Math.max(1, numsLen * F64));
      const textPtr = mod._malloc(Math.max(1, textLen));
      try {
        c.read(this.#handle, numsPtr, textPtr);
        return decodeLines(new Float64Array(mod.HEAPF64.buffer, numsPtr, numsLen),
                           mod.HEAPU8.subarray(textPtr, textPtr + textLen));
      } finally {
        mod._free(numsPtr);
        mod._free(textPtr);
      }
    }
  };
};

// Everything the core's state/ group installs, keyed like the pure ops.
export const buildStateOps = (mod) => ({
  HoldDrawController: holdDrawClass(mod),
  HistoryStack: historyClass(mod),
  ...buildProjectRules(mod),
});

// The C exports these ops cwrap — checked before any wrapper is installed.
export const stateExports = [
  'stencil_holdDraw_create', 'stencil_holdDraw_destroy', 'stencil_holdDraw_state',
  'stencil_holdDraw_holdDelay', 'stencil_holdDraw_setHoldDelay', 'stencil_holdDraw_cancel',
  'stencil_holdDraw_pointerDown', 'stencil_holdDraw_pointerMove', 'stencil_holdDraw_tick',
  'stencil_holdDraw_pointerUp',
  'stencil_history_create', 'stencil_history_destroy', 'stencil_history_reset',
  'stencil_history_push', 'stencil_history_canUndo', 'stencil_history_canRedo',
  'stencil_history_step', 'stencil_history_size', 'stencil_history_undo',
  'stencil_history_redo', 'stencil_history_readResult', ...projectRuleExports,
];
