// The arrival cloud (js/ui/motion.js + animations.css): the entry is veiled until its motes
// land, the cloud is re-anchored and clipped to the scroller, and the bubble hugs its widest line.
import { test } from 'node:test';
import assert from 'node:assert';
import { dustFitsScroller } from '../js/ui/motion.js';
import { shrinkWrapWidth } from '../js/ui/chat/view.js';
import { motionSource } from './helpers/motionSource.js';
import { ANIMATIONS_CSS } from './helpers/css.js';

test('animations.css: an arriving entry is VEILED, never faded up under its own dust', () => {
  const css = ANIMATIONS_CSS;
  const rule = css.slice(css.indexOf('.chat-transcript > .chat-entering'));
  // A veil, not a keyframed fade: the entry is not seen at all until the motes land, so
  // the animation can never play over an already-visible message (the reported bug).
  assert.match(rule, /^\.chat-transcript > \.chat-entering[\s\S]{0,200}?opacity: 0 !important;/);
  assert.match(rule.slice(0, 400), /animation: none !important;/,
    'the dust is a photograph of where the entry IS — nothing may move under it');
  assert.match(rule.slice(0, 400), /transition: none !important;/);
  assert.match(css, /\.ctx-assist-transcript > \.chat-entering/, 'the flyout transcript too');
  // The old fade is gone for good — keeping it would re-introduce exactly the bug.
  assert.ok(!/chatCardEnter/.test(css), 'no fade-up keyframes survive');
  // The entry keeps its HEIGHT while veiled (opacity only, never display/height), so the
  // transcript grows and scrolls to it exactly as it always did.
  assert.ok(!/\.chat-entering[\s\S]{0,200}?(display: none|height: 0)/.test(rule.slice(0, 400)));
});

test('chatIn: veils at once, lifts only when the motes have landed', async () => {
  const { chatIn, CHAT_ENTER_MS, CHAT_ENTERING_CLASS, ITEM_DUST_MS } = await import('../js/ui/motion.js');
  // Both directions ride one clock, ITEM_DUST_MS — deliberately not the connections list's
  // DISINTEGRATE_MS — and as a SHARE of it, so a change to one never has the two meet.
  assert.ok(CHAT_ENTER_MS < ITEM_DUST_MS && CHAT_ENTER_MS >= ITEM_DUST_MS / 2,
    `chat arrival ${CHAT_ENTER_MS}ms of a ${ITEM_DUST_MS}ms message flight`);
  const classes = new Set();
  const el = { classList: {
    add: (...c) => c.forEach((x) => classes.add(x)),
    remove: (...c) => c.forEach((x) => classes.delete(x)),
    contains: (c) => classes.has(c),
  } };
  globalThis.matchMedia = () => ({ matches: false });
  const p = chatIn(el);
  // SYNCHRONOUSLY veiled — before the caller returns, so no frame ever paints the entry
  // ahead of its own dust.
  assert.ok(classes.has(CHAT_ENTERING_CLASS), 'veiled from the first frame');
  await p;
  // No DOM here, so no motes can fly (disintegrate bails) — then the veil must lift at
  // once rather than hiding the entry behind a flight that never happened.
  assert.ok(!classes.has(CHAT_ENTERING_CLASS), 'never left stranded invisible');

  // Reduced motion: no veil at all — the entry is simply THERE.
  globalThis.matchMedia = () => ({ matches: true });
  await chatIn(el);
  assert.ok(!classes.has(CHAT_ENTERING_CLASS));
  // Decoration only: a missing element must never make an append throw.
  await chatIn(null);
  delete globalThis.matchMedia;
});

test('a flying cloud is re-anchored to its entry, and dropped if the entry leaves', () => {
  // The layer is position:fixed at the box measured when it launched, but the transcript
  // scrolls and later turns append rows, so a stale cloud paints over what moved in.
  const src = motionSource();
  assert.match(src, /const trackDust = \(el, ms, onDrop = \(\) => \{\}\) => \{/);
  assert.match(src, /retargetDust\(el, r\);/, 'the cloud follows its entry per frame');
  assert.ok(/dustFitsScroller\(el\)/.test(src),
    'and is dropped outright once the entry is no longer wholly in the scroller');
  // A subject that RESIZES mid-flight cannot be re-photographed, so the stale cloud is dropped
  // rather than shown at the wrong size.
  assert.match(src, /const resized = !r \|\| !shot/);
  assert.match(src, /if \(!dustFitsScroller\(el, el\.parentElement, r, s\) \|\| resized\) \{\s*\n\s*cancelDust\(el\); live = false; onDrop\(\); return;/);
  // Fonts first: a webfont landing after the photograph re-wraps the entry and widens it.
  assert.match(src, /globalThis\.document\?\.fonts\?\.ready/);
  // …armed for the flight and stopped with it, so nothing keeps ticking after the cut.
  assert.match(src, /const stop = trackDust\(el, CHAT_ENTER_MS, handOver\);/);
  assert.match(src, /stop\(\);\s*\n\s*handOver\(\);/);
  // An element torn out mid-flight (the transcript cleared, a row replaced) measures as
  // a zero box, so the same check reaps its orphaned cloud within a frame.
  assert.match(src, /export function retargetDust\(el, r = null\) \{/);
});

test('a flying cloud is clipped to its scroller, so no mote lands on the composer', () => {
  // The tiles translate freely out of an `overflow: visible` host, so the clip is against the
  // host's OWN border box and the insets are signed: negative EXPANDS it.
  const src = motionSource();
  assert.match(src, /const clipDustToScroller = \(el, scroller = el\?\.parentElement, r = null, s = null\) => \{/);
  assert.match(src, /host\.style\.clipPath =/);
  assert.match(src, /inset\(\$\{px\(s\.top - r\.top\)\} \$\{px\(r\.right - s\.right\)\} \$\{px\(r\.bottom - s\.bottom\)\} \$\{px\(s\.left - r\.left\)\}\)/);
  // Applied before the first painted frame, then kept in step per frame (both boxes move).
  assert.match(src, /clipDustToScroller\(el\);   \/\/ before the first frame paints, not after it/);
  assert.match(src, /retargetDust\(el, r\);\s*\n\s*clipDustToScroller\(el, el\.parentElement, r, s\);/);
});

test('dropping the cloud hands the entry over in the SAME frame', () => {
  // The veil is lifted by a timer at the end of the FULL flight, so a cancel must lift it too:
  // killing only the motes leaves the message invisible until that timer fires.
  const src = motionSource();
  assert.match(src, /const trackDust = \(el, ms, onDrop = \(\) => \{\}\) => \{/);
  assert.match(src, /cancelDust\(el\); live = false; onDrop\(\); return;/);
  assert.match(src, /const stop = trackDust\(el, CHAT_ENTER_MS, handOver\);/);
  // …and exactly once, whichever path gets there first (a drop, or the flight ending).
  assert.match(src, /if \(handedOver\) return;/);
});

// A wrapped bubble hugs its own longest line, not the 88% max-width cap
// (js/ui/view.js applyShrinkWrap).
test('shrinkWrapWidth: one line already hugs its content — nothing to pin', () => {
  assert.strictEqual(shrinkWrapWidth([193.4]), null);
  assert.strictEqual(shrinkWrapWidth([]), null);
  assert.strictEqual(shrinkWrapWidth(null), null);
});

test('shrinkWrapWidth: two+ lines pin to the WIDEST one, rounded up', () => {
  assert.strictEqual(shrinkWrapWidth([193.4, 182.1]), 194);
  assert.strictEqual(shrinkWrapWidth([120, 300, 45]), 300);
  // A degenerate (zero-width) measurement declines rather than pinning a collapsed box.
  assert.strictEqual(shrinkWrapWidth([0, 0]), null);
});
