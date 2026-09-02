// Tests for src/lib/chatUi.js — the assistant transcript's two widgets:
// dismissible error/notice entries (they used to stack up forever) and the
// empty-state suggestion chips (which PREFILL the input, never send). Driven with a
// stub document, like the other DOM-adjacent extension suites.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  AUTO_DISMISS_MS, SUGGESTIONS, makeDismissible, renderSuggestions,
  shrinkWrapWidth, applyShrinkWrap, bindShrinkWrapResize,
} from '../src/lib/chatUi.js';

// ── Stub DOM ──
const stubEl = (tag = 'div') => {
  const el = {
    tag, className: '', textContent: '', title: '', type: '',
    children: [], attrs: {}, dataset: {}, removed: false, isConnected: true,
    handlers: {},
    appendChild: (c) => { el.children.push(c); return c; },
    addEventListener: (t, fn) => { (el.handlers[t] || (el.handlers[t] = [])).push(fn); },
    setAttribute: (k, v) => { el.attrs[k] = v; },
    remove: () => { el.removed = true; el.isConnected = false; },
    click: () => { for (const fn of el.handlers.click || []) fn({ stopPropagation: () => { el.stopped = true; } }); },
  };
  return el;
};
const stubDoc = () => ({ createElement: (tag) => stubEl(tag) });

// ── makeDismissible ──

test('a dismissible entry gets a labelled × that removes it', () => {
  const doc = stubDoc();
  const card = stubEl();
  let dismissed = 0;
  const { button } = makeDismissible(card, { doc, onDismiss: () => { dismissed++; } });

  assert.equal(card.children.length, 1);
  assert.equal(button.className, 'x-dismiss');
  assert.equal(button.textContent, '×');
  assert.equal(button.type, 'button');
  assert.equal(button.attrs['aria-label'], 'Dismiss');
  assert.equal(card.removed, false);

  button.click();
  assert.equal(card.removed, true);
  assert.equal(dismissed, 1);
  assert.equal(card.stopped, undefined);   // stopPropagation is called on the BUTTON's event
});

test('dismissing twice removes once', () => {
  const doc = stubDoc();
  const card = stubEl();
  let dismissed = 0;
  const { button, dismiss } = makeDismissible(card, { doc, onDismiss: () => { dismissed++; } });
  button.click();
  dismiss();
  button.click();
  assert.equal(dismissed, 1);
});

test('an attach error auto-dismisses after the timeout', () => {
  const doc = stubDoc();
  const card = stubEl();
  let fire = null;
  let delay = 0;
  makeDismissible(card, { doc, autoMs: AUTO_DISMISS_MS, timer: (fn, ms) => { fire = fn; delay = ms; } });
  assert.equal(delay, AUTO_DISMISS_MS);
  assert.equal(card.removed, false);
  fire();
  assert.equal(card.removed, true);
});

test('auto-dismiss is a no-op once the entry is already gone', () => {
  const doc = stubDoc();
  const card = stubEl();
  let fire = null;
  let dismissed = 0;
  makeDismissible(card, { doc, autoMs: 8000, timer: (fn) => { fire = fn; }, onDismiss: () => { dismissed++; } });
  card.remove();          // e.g. the transcript was cleared
  fire();
  assert.equal(dismissed, 0);
});

test('no timer is armed without autoMs (× only)', () => {
  const doc = stubDoc();
  let armed = false;
  makeDismissible(stubEl(), { doc, timer: () => { armed = true; } });
  assert.equal(armed, false);
});

// ── Suggestion chips ──

test('the extension ships suggestions for ITS profile (§8: focus / open / attach)', () => {
  assert.equal(SUGGESTIONS.length, 4);
  for (const s of SUGGESTIONS) {
    assert.ok(s.label && s.prompt, 'each chip has a short label and a full prompt');
    assert.ok(s.label.length <= 40);
  }
  const prompts = SUGGESTIONS.map((s) => s.prompt.toLowerCase()).join(' | ');
  assert.match(prompts, /cat/);
  assert.match(prompts, /largest image/);
  assert.match(prompts, /editor/);
  assert.match(prompts, /chart/);
});

test('clicking a chip PREFILLS the prompt and sends nothing', () => {
  const doc = stubDoc();
  const picked = [];
  const wrap = renderSuggestions(doc, (p) => picked.push(p));

  assert.equal(wrap.className, 'chat-empty');
  assert.equal(wrap.children.length, SUGGESTIONS.length);
  const [first] = wrap.children;
  assert.equal(first.className, 'chat-suggest');
  assert.equal(first.textContent, SUGGESTIONS[0].label);
  // No tooltip on a chip (browser parity: chatEmptyState builds these bare) — hovering
  // one popped a bubble restating the label in longer words, over its neighbours.
  assert.equal(first.attrs['data-title'], undefined);
  assert.equal(first.attrs.title, undefined, 'and certainly not the native tooltip');
  assert.equal(first.dataset.prompt, SUGGESTIONS[0].prompt, 'the prompt still rides the chip');

  first.click();
  wrap.children[2].click();
  assert.deepEqual(picked, [SUGGESTIONS[0].prompt, SUGGESTIONS[2].prompt]);
});

test('renderSuggestions takes a custom chip list', () => {
  const doc = stubDoc();
  const wrap = renderSuggestions(doc, () => {}, [{ label: 'a', prompt: 'A?' }]);
  assert.equal(wrap.children.length, 1);
  assert.equal(wrap.children[0].textContent, 'a');
});

// ── shrinkWrapWidth / applyShrinkWrap / bindShrinkWrapResize ─────────────────
// Browser js/ui/chatView.js parity: a wrapped bubble hugs its own longest line, not
// the max-width cap it never searches a narrower box against (user report: "message
// width is adjusted wrong").
test('shrinkWrapWidth: one line already hugs its content — nothing to pin', () => {
  assert.equal(shrinkWrapWidth([142.5]), null);
  assert.equal(shrinkWrapWidth([]), null);
  assert.equal(shrinkWrapWidth(null), null);
});

test('shrinkWrapWidth: two+ lines pin to the WIDEST one, rounded up', () => {
  assert.equal(shrinkWrapWidth([142.5, 118.2]), 143);
  assert.equal(shrinkWrapWidth([90, 210, 30]), 210);
  assert.equal(shrinkWrapWidth([0, 0]), null);
});

test('applyShrinkWrap: a no-op without Range — never throws on the stub tree', () => {
  const doc = stubDoc();   // no createRange, like node --test's real stub
  const el = stubEl();
  el.style = {};
  el.firstChild = { nodeType: 3 };
  assert.doesNotThrow(() => applyShrinkWrap(el, doc));
  assert.equal(el.style.maxWidth, undefined);
});

test('applyShrinkWrap: pins to the widest line, floored up by a wider CTA sibling', () => {
  const rectsFor = new Map();   // node -> array of {width} rects
  const doc = {
    createRange: () => ({
      selectNodeContents(node) { this._node = node; },
      getClientRects: function () { return rectsFor.get(this._node) || []; },
    }),
  };
  const text = { nodeType: 3 };
  rectsFor.set(text, [{ width: 142.4 }, { width: 118.9 }]);   // two wrapped lines
  const cta = { getBoundingClientRect: () => ({ width: 210 }) };   // wider than either line
  const el = { style: {}, firstChild: text, children: [cta] };
  applyShrinkWrap(el, doc);
  assert.equal(el.style.maxWidth, '210px', 'the CTA sets the floor, not the text');

  // A narrower CTA never shrinks the pin below the text's own widest line.
  const narrowCta = { getBoundingClientRect: () => ({ width: 60 }) };
  const el2 = { style: {}, firstChild: text, children: [narrowCta] };
  applyShrinkWrap(el2, doc);
  assert.equal(el2.style.maxWidth, '143px');
});

test('bindShrinkWrapResize: binds once per transcript, no ResizeObserver global — a safe no-op', () => {
  const transcript = { querySelectorAll: () => { throw new Error('must not run without RO'); } };
  assert.doesNotThrow(() => bindShrinkWrapResize(transcript));
  assert.equal(transcript._shrinkWrapBound, undefined, 'never marked bound if it never bound');
});
