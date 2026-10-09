// The arrival cloud (js/ui/motion.js + animations.css): the entry is veiled until its motes
// land, the cloud is re-anchored and clipped to the scroller, and the bubble hugs its widest line.
import { test } from 'node:test';
import assert from 'node:assert';
import { shrinkWrapWidth } from '../../../js/ui/chat/view.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { installDustPage, rect } from '../../helpers/dustRig.js';

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
  const { chatIn, CHAT_ENTER_MS, CHAT_ENTERING_CLASS, ITEM_DUST_MS } = await import('../../../js/ui/motion.js');
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

// A launched arrival: an entry at `box()` inside a 400px scroller, flown two frames on.
const launch = async (t, box, extra) => {
  const page = installDustPage(t, extra);
  const { chatIn, CHAT_ENTER_MS, CHAT_ENTERING_CLASS } = await import('../../../js/ui/motion.js');
  const el = page.entry(box, SCROLLER);
  const landed = chatIn(el);
  await Promise.resolve();
  page.frame();
  page.frame();
  return { page, el, landed, host: page.clouds()[0], CHAT_ENTER_MS, veiled: () => el.classList.contains(CHAT_ENTERING_CLASS) };
};
const SCROLLER = rect(0, 100, 400, 400);

test('a flying cloud is re-anchored to its entry, and dropped if the entry leaves', async (t) => {
  // The layer is position:fixed at the box measured when it launched, but the transcript
  // scrolls and later turns append rows, so a stale cloud paints over what moved in.
  let box = rect(20, 300, 200, 40);
  const { page, el, host, veiled } = await launch(t, () => box);
  assert.ok(host && el.__dustHost === host, 'the entry flies');
  box = rect(20, 260, 200, 40);
  page.frame();
  assert.deepStrictEqual([host.style.left, host.style.top], ['20px', '260px'], 'the cloud follows its entry per frame');
  // Dropped outright once the entry is no longer wholly in the scroller.
  box = rect(20, 480, 200, 40);
  page.frame();
  assert.deepStrictEqual([page.clouds().length, el.__dustHost, veiled()], [0, null, false]);
  assert.strictEqual(page.pendingFrames(), 0, 'and nothing keeps ticking after the cut');
  // A subject that RESIZES mid-flight cannot be re-photographed, so the stale cloud is dropped.
  let grown = rect(20, 300, 200, 40);
  const again = await launch(t, () => grown);
  grown = rect(20, 300, 200, 64);
  again.page.frame();
  assert.deepStrictEqual([again.page.clouds().length, again.veiled()], [0, false], 'resized ⇒ dropped');
});

test('fonts first, and the tracking stops with the flight', async (t) => {
  // A webfont landing after the photograph re-wraps the entry and widens it.
  const page = installDustPage(t);
  let fontsLoaded;
  page.doc.fonts = { ready: new Promise((r) => { fontsLoaded = r; }) };
  const { chatIn, CHAT_ENTER_MS, CHAT_ENTERING_CLASS } = await import('../../../js/ui/motion.js');
  const el = page.entry(() => rect(20, 300, 200, 40), SCROLLER);
  chatIn(el);
  page.frame(); page.frame();
  assert.strictEqual(el.__dustHost, undefined, 'no photograph before the fonts are in');
  fontsLoaded();
  await page.doc.fonts.ready;
  page.frame(); page.frame();
  assert.ok(el.__dustHost, 'then it flies');
  // …armed for the flight and stopped with it, so nothing keeps ticking after the cut.
  assert.strictEqual(page.pendingFrames(), 1, 'tracked per frame while in the air');
  page.fire(CHAT_ENTER_MS);
  assert.strictEqual(page.pendingFrames(), 0, 'no frame is left queued once the flight ends');
  assert.ok(!el.classList.contains(CHAT_ENTERING_CLASS));
});

test('a flying cloud is clipped to its scroller, so no mote lands on the composer', async (t) => {
  // The motes translate freely out of an `overflow: visible` host, so the clip is against the
  // host's OWN border box and the insets are signed: negative EXPANDS it.
  let box = rect(20, 300, 200, 40);
  const { page, host } = await launch(t, () => box);
  assert.strictEqual(host.style.clipPath, 'inset(-200px -180px -160px -20px)', 'before the first frame paints');
  box = rect(20, 250, 200, 40);
  page.frame();
  assert.strictEqual(host.style.clipPath, 'inset(-150px -180px -210px -20px)', 'kept in step per frame');
});

test('dropping the cloud hands the entry over in the SAME frame', async (t) => {
  // The veil is lifted by a timer at the end of the FULL flight, so a cancel must lift it too:
  // killing only the motes leaves the message invisible until that timer fires.
  let box = rect(20, 300, 200, 40);
  const { page, el, landed, veiled, CHAT_ENTER_MS } = await launch(t, () => box);
  box = rect(20, 20, 200, 40);
  page.frame();
  assert.strictEqual(veiled(), false, 'unveiled by the drop, not by the timer');
  await landed;
  // …and exactly once, whichever path gets there first: the old flight's timer must not
  // strip the veil of the entry's next arrival.
  box = rect(20, 300, 200, 40);
  const { chatIn } = await import('../../../js/ui/motion.js');
  chatIn(el);
  page.fire(CHAT_ENTER_MS);
  assert.strictEqual(veiled(), true, 'the spent flight hands over nothing twice');
});

// A wrapped bubble hugs its own longest line, not the 88% max-width cap
// (js/ui/chat/view.js applyShrinkWrap).
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
