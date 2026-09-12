// ── Handle-backed wrappers over the core's stateful classes ─────
// stencilCore.js wraps the core's pure functions; the classes in core/state/ own
// state, so they live in wasm memory behind an opaque int handle (create/destroy
// plus operations) exported by core/wasmStateApi.cpp. Each wrapper mirrors its JS
// twin's API exactly, so browser/tests/wasm-parity-state.test.js can drive both
// through the same script and pin them op-for-op.

const F64 = 8;

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

// The stateful classes this core installs, keyed like the pure ops.
export const buildHandleClasses = (mod) => ({ HoldDrawController: holdDrawClass(mod) });

// The C exports the handle classes cwrap — checked before any wrapper is installed.
export const handleExports = [
  'stencil_holdDraw_create', 'stencil_holdDraw_destroy', 'stencil_holdDraw_state',
  'stencil_holdDraw_holdDelay', 'stencil_holdDraw_setHoldDelay', 'stencil_holdDraw_cancel',
  'stencil_holdDraw_pointerDown', 'stencil_holdDraw_pointerMove', 'stencil_holdDraw_tick',
  'stencil_holdDraw_pointerUp',
];
