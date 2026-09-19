// ── Arithmetic in numeric inputs ────────────────────────────────────────────
// Numeric fields take an expression: "45 + 9" → 54, or "* 9" on a current 3 → 27. Since
// `<input type="number">` discards non-numeric text, enhance() flips the element to type="text"
// at runtime and re-adds arrow stepping and clamping; the markup keeps type="number".
// Operators match core/parse/formulaParser (** right-associative, no eval).

import { evalNumericExpression } from './numericExpr.js';
export { evalNumericExpression };

// ── DOM wiring ──────────────────────────────────────────────────────────────

const num = (v, fallback) => (v === '' || v == null || Number.isNaN(Number(v)) ? fallback : Number(v));

// Typing pauses this long and the field commits itself; Enter/blur commit at once. Long enough
// to think mid-expression without the field applying a half-written value.
export const COMMIT_DEBOUNCE_MS = 1200;

// Round to the precision implied by `step` so 0.1-step fields don't accumulate
// binary-float dust ("29.700000000000003") after a few keyboard steps.
const quantize = (value, step) => {
  const decimals = (String(step).split('.')[1] || '').length;
  return decimals ? Number(value.toFixed(decimals)) : value;
};

const clamp = (value, el) => {
  const lo = num(el.getAttribute('min'), -Infinity);
  const hi = num(el.getAttribute('max'), Infinity);
  return Math.min(hi, Math.max(lo, value));
};

/**
 * Upgrade one numeric input in place. Idempotent — re-enhancing is a no-op.
 * Commits on Enter and on blur; Escape restores the last committed value.
 * A cleared or half-written field is left alone until editing ends, so the
 * text you are still typing is never replaced mid-edit.
 */
export const enhanceNumericInput = (el) => {
  if (!el || el.dataset.numericEnhanced) return;
  el.dataset.numericEnhanced = '1';
  // Remember what the field was, so `type` reads and CSS keep working, then hand
  // the element a text type so arithmetic survives being typed.
  el.dataset.numericStep = el.getAttribute('step') || '1';
  el.type = 'text';
  el.inputMode = 'decimal';
  el.autocomplete = 'off';

  let committed = el.value;
  let timer = null;
  let emitting = false;   // true only while commit() re-emits the settled value
  const cancelPending = () => { if (timer) { clearTimeout(timer); timer = null; } };

  // `final` says editing ended (Enter / blur / arrow step). The idle timer passes false: a
  // half-written field is left as typed; only a real end falls back to the last good value.
  const commit = ({ final = true } = {}) => {
    cancelPending();
    const step = el.dataset.numericStep;
    const value = evalNumericExpression(el.value, num(committed, 0));
    if (value == null) {
      if (final) el.value = committed;                     // unparseable → put it back
      return;
    }
    el.value = String(quantize(clamp(value, el), step));
    if (el.value === committed) return;                     // nothing actually changed
    committed = el.value;
    // Same events the native control fired, so every existing listener still works.
    emitting = true;
    try {
      el.dispatchEvent(new Event('input', { bubbles: true }));
      el.dispatchEvent(new Event('change', { bubbles: true }));
    } finally {
      emitting = false;
    }
  };

  const step = (dir) => {
    const s = Number(el.dataset.numericStep) || 1;
    const base = evalNumericExpression(el.value, num(committed, 0));
    const next = quantize(clamp((base == null ? num(committed, 0) : base) + dir * s, el), el.dataset.numericStep);
    el.value = String(next);
    commit();
  };

  // Raw keystrokes never reach the app's binders — they assume a parseable number, and "9 +" would
  // read as NaN and clamp the field. Each keystroke restarts the idle timer instead.
  el.addEventListener('input', (e) => {
    if (emitting) return;                 // our own settled value — let it through
    e.stopImmediatePropagation();
    cancelPending();
    timer = setTimeout(() => commit({ final: false }), COMMIT_DEBOUNCE_MS);
  }, true);

  el.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowUp') { e.preventDefault(); step(1); }
    else if (e.key === 'ArrowDown') { e.preventDefault(); step(-1); }
    // Enter cancels the pending timer and applies immediately (commit does the cancel).
    else if (e.key === 'Enter') { e.preventDefault(); commit(); }
    else if (e.key === 'Escape') { cancelPending(); el.value = committed; el.blur(); }
  });
  el.addEventListener('blur', () => commit());
  // Something else (the app syncing state back) wrote a clean value in — adopt it
  // as the new baseline so Escape/relative edits work from what's on screen.
  el.addEventListener('change', () => {
    if (document.activeElement !== el && evalNumericExpression(el.value) != null) committed = el.value;
  });
};

/** Enhance every numeric input under `root` (safe to call repeatedly). */
export const enhanceNumericInputs = (root = document) => {
  if (!root || typeof root.querySelectorAll !== 'function') return;
  for (const el of root.querySelectorAll('input[type="number"]')) enhanceNumericInput(el);
};

/**
 * Install a one-time observer so inputs rendered later (modals, panels, the
 * context menu) are upgraded as they appear. Returns the MutationObserver.
 */
export const watchNumericInputs = (root = document.body) => {
  enhanceNumericInputs(document);
  const obs = new MutationObserver((records) => {
    for (const rec of records)
      for (const node of rec.addedNodes) {
        if (node.nodeType !== 1) continue;
        if (node.matches?.('input[type="number"]')) enhanceNumericInput(node);
        else enhanceNumericInputs(node);
      }
  });
  obs.observe(root, { childList: true, subtree: true });
  return obs;
};
