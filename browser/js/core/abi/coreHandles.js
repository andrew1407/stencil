// The core's state/ group: classes that own state live in wasm memory behind an opaque
// int handle (core/wasmStateApi.cpp). Each wrapper mirrors its JS twin's API exactly, so
// browser/tests/wasm/wasm-parity-state.test.js drives both through one script.

import { encodeLines, decodeLines } from '../line/linesCodec.js';
import { buildProjectRules, projectRuleExports } from '../project/meta/projectRules.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const { HOLD_DRAW } = constants;
const F64 = 8;
const I32 = 4;

// In core/state/holdDraw.hpp order (0 = None → no event).
const HOLD_ACTIONS = [null, 'armed', 'abort', 'start', 'drop', 'preview', 'commit'];
// The actions that carry coordinates; the rest are bare {type} objects, as in holdDraw.js.
const HOLD_XY = new Set(['start', 'drop', 'preview']);
const HOLD_STATES = ['idle', 'armed', 'drawing', 'aborted'];

// Instances own a core handle plus a 2-double out slot, released by destroy().
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

    constructor({ holdDelay = HOLD_DRAW.delayMs, moveTolerance = HOLD_DRAW.moveTolerancePx,
      rearmDistance = HOLD_DRAW.rearmDistancePx } = {}) {
      this.#handle = c.create(holdDelay, moveTolerance, rearmDistance);
      this.#out = mod._malloc(2 * F64);
    }

// Idempotent; the instance is unusable after.
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

    #event(code) {
      const type = HOLD_ACTIONS[code];
      if (!type) return null;
      if (!HOLD_XY.has(type)) return { type };
      return { type, x: mod.getValue(this.#out, 'double'), y: mod.getValue(this.#out + F64, 'double') };
    }
  };
};

// Snapshots cross as the flat (nums, text) pair from linesCodec.js, in both directions; a
// memento's view as [x, y, width, height, quarters] beside it (width 0 = no crop yet).
const historyClass = (mod) => {
  const NUM = 'number', STR = 'string';
  const c = {
    create: mod.cwrap('stencil_history_create', NUM, []),
    destroy: mod.cwrap('stencil_history_destroy', null, [NUM]),
    reset: mod.cwrap('stencil_history_resetMemento', null, [NUM, NUM, NUM, NUM, NUM, NUM, NUM, NUM, STR, STR]),
    push: mod.cwrap('stencil_history_pushMemento', null, [NUM, NUM, NUM, NUM, NUM, NUM, STR, STR]),
    canUndo: mod.cwrap('stencil_history_canUndo', NUM, [NUM]),
    canRedo: mod.cwrap('stencil_history_canRedo', NUM, [NUM]),
    step: mod.cwrap('stencil_history_step', NUM, [NUM]),
    size: mod.cwrap('stencil_history_size', NUM, [NUM]),
    undo: mod.cwrap('stencil_history_undo', NUM, [NUM, NUM]),
    redo: mod.cwrap('stencil_history_redo', NUM, [NUM, NUM]),
    read: mod.cwrap('stencil_history_readResult', null, [NUM, NUM, NUM]),
    readView: mod.cwrap('stencil_history_readView', NUM, [NUM, NUM]),
    readFilter: mod.cwrap('stencil_history_readFilter', STR, [NUM, NUM]),
  };

// A zero-length buffer still gets a byte, so the pointer is never null; a Lines array has no view.
  const withSnapshot = (snapshot, use) => {
    const isMemento = !Array.isArray(snapshot);
    const { nums, text } = encodeLines(isMemento ? snapshot.lines : snapshot);
    const numsPtr = mod._malloc(Math.max(1, nums.length * F64));
    const textPtr = mod._malloc(Math.max(1, text.length));
    const viewPtr = isMemento ? mod._malloc(5 * F64) : 0;
    try {
      new Float64Array(mod.HEAPF64.buffer, numsPtr, nums.length).set(nums);
      mod.HEAPU8.set(text, textPtr);
      if (isMemento) {
        const r = snapshot.cropRect;
        const view = r ? [r.x, r.y, r.width, r.height] : [0, 0, 0, 0];
        new Float64Array(mod.HEAPF64.buffer, viewPtr, 5).set([...view, snapshot.rotationQuarters ?? 0]);
      }
      return use(numsPtr, nums.length, textPtr, text.length, viewPtr,
        isMemento ? (snapshot.filter ?? '') : '', isMemento ? (snapshot.filterColor ?? '') : '');
    } finally {
      mod._free(numsPtr);
      mod._free(textPtr);
      if (viewPtr) mod._free(viewPtr);
    }
  };

  return class HistoryStack {
    #handle;
    #sizes;

    constructor() {
      this.#handle = c.create();
      this.#sizes = mod._malloc(2 * I32);
    }

    destroy() {
      if (!this.#handle) return;
      c.destroy(this.#handle);
      mod._free(this.#sizes);
      this.#handle = 0;
    }

    get historyStep() { return c.step(this.#live()); }
    get size() { return c.size(this.#live()); }

    reset(snapshot, baseStep) {
      const has = baseStep === undefined ? 0 : 1;
      withSnapshot(snapshot, (...buf) => c.reset(this.#live(), has, has ? baseStep : 0, ...buf));
    }

    push(snapshot) {
      withSnapshot(snapshot, (...buf) => c.push(this.#live(), ...buf));
    }

    canUndo() { return c.canUndo(this.#live()) === 1; }
    canRedo() { return c.canRedo(this.#live()) === 1; }
    undo() { return this.#result(c.undo(this.#live(), this.#sizes)); }
    redo() { return this.#result(c.redo(this.#live(), this.#sizes)); }

    #live() {
      if (!this.#handle) throw new Error('history stack destroyed');
      return this.#handle;
    }

// 0 from undo/redo is the JS null; a step with a view comes back a memento, else Lines.
    #result(ok) {
      if (!ok) return null;
      const numsLen = mod.getValue(this.#sizes, 'i32');
      const textLen = mod.getValue(this.#sizes + I32, 'i32');
      const numsPtr = mod._malloc(Math.max(1, numsLen * F64));
      const textPtr = mod._malloc(Math.max(1, textLen));
      const viewPtr = mod._malloc(5 * F64);
      try {
        c.read(this.#handle, numsPtr, textPtr);
        const lines = decodeLines(new Float64Array(mod.HEAPF64.buffer, numsPtr, numsLen),
                                  mod.HEAPU8.subarray(textPtr, textPtr + textLen));
        if (!c.readView(this.#handle, viewPtr)) return lines;
        const [x, y, width, height, rotationQuarters] = new Float64Array(mod.HEAPF64.buffer, viewPtr, 5);
        const m = { lines, cropRect: width > 0 ? { x, y, width, height } : null, rotationQuarters };
        const filter = c.readFilter(this.#handle, 0), filterColor = c.readFilter(this.#handle, 1);
        if (filter) m.filter = filter;
        if (filterColor) m.filterColor = filterColor;
        return m;
      } finally {
        mod._free(numsPtr);
        mod._free(textPtr);
        mod._free(viewPtr);
      }
    }
  };
};

export const buildStateOps = (mod) => ({
  HoldDrawController: holdDrawClass(mod),
  HistoryStack: historyClass(mod),
  ...buildProjectRules(mod),
});

// Checked before any wrapper is installed.
export const stateExports = [
  'stencil_holdDraw_create', 'stencil_holdDraw_destroy', 'stencil_holdDraw_state',
  'stencil_holdDraw_holdDelay', 'stencil_holdDraw_setHoldDelay', 'stencil_holdDraw_cancel',
  'stencil_holdDraw_pointerDown', 'stencil_holdDraw_pointerMove', 'stencil_holdDraw_tick',
  'stencil_holdDraw_pointerUp',
  'stencil_history_create', 'stencil_history_destroy', 'stencil_history_reset',
  'stencil_history_push', 'stencil_history_canUndo', 'stencil_history_canRedo',
  'stencil_history_step', 'stencil_history_size', 'stencil_history_undo',
  'stencil_history_redo', 'stencil_history_readResult', 'stencil_history_resetMemento',
  'stencil_history_pushMemento', 'stencil_history_readView', 'stencil_history_readFilter',
  ...projectRuleExports,
];
