// The scroll-reveal mask (js/ui/motion.js): it softens only the edge the scroller is really
// clipping, the wipe reads those feathers, and a repaint keeps the classes motion.js owns.
import { test } from 'node:test';
import assert from 'node:assert';
import { revealFeather, REVEAL_FEATHER } from '../js/ui/motion.js';
import { motionSource } from './helpers/motionSource.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { chatViewSource } from './helpers/chatViewSource.js';
import { makeEl, stubDom, rowsOf } from './helpers/chatTranscriptRig.js';

// ── The scroll-reveal mask must only sand the edge that is actually cut ─────
test('revealFeather softens only the edge the scroller is really clipping', () => {
  const H = 500;
  // Fully visible: nothing to soften at either end.
  assert.deepStrictEqual(revealFeather(20, 200, H), { in: '0%', out: '0%' });
  // Clipped at the TOP only — the bottom is on screen, so it stays hard, or the sanded edge
  // becomes a speckled second copy of the last line and swallows the "…" trigger.
  assert.deepStrictEqual(revealFeather(-120, 26, H), { in: REVEAL_FEATHER, out: '0%' });
  // Clipped at the BOTTOM only.
  assert.deepStrictEqual(revealFeather(400, 600, H), { in: '0%', out: REVEAL_FEATHER });
  // Taller than the scroller: both ends are genuinely cut.
  assert.deepStrictEqual(revealFeather(-50, 900, H), { in: REVEAL_FEATHER, out: REVEAL_FEATHER });
  // Flush with an edge is NOT clipped (half-pixel slack).
  assert.deepStrictEqual(revealFeather(0, H, H), { in: '0%', out: '0%' });
  assert.deepStrictEqual(revealFeather(-0.4, H + 0.4, H), { in: '0%', out: '0%' });
});

test('the mask wipe reads those feathers, and the observer publishes them', () => {
  const css = ANIMATIONS_CSS;
  const rest = css.slice(css.indexOf('.reveal-item.reveal-masked {'),
    css.indexOf('\n}', css.indexOf('.reveal-item.reveal-masked {')));
  // Both spellings of the mask (the -webkit- one included) take the widths as vars.
  assert.strictEqual(rest.split('calc(var(--vis-start, 0%) + var(--fade-in, 10%))').length - 1, 2);
  assert.strictEqual(rest.split('calc(var(--vis-end, 100%) - var(--fade-out, 10%))').length - 1, 2);
  // A hard-coded 10% either side is exactly the bug — it faded edges nothing was cutting.
  assert.ok(!/\+ 10%\)/.test(rest) && !/- 10%\)/.test(rest), 'no unconditional feather is left');
  const base = css.slice(css.indexOf('.reveal-item {'), css.indexOf('\n}', css.indexOf('.reveal-item {')));
  assert.ok(/--fade-in: 10%/.test(base) && /--fade-out: 10%/.test(base), 'the entering state softens both ways');
  const motion = motionSource();
  assert.ok(motion.includes("row.el.style.setProperty('--fade-in', fade.in);"));
  assert.ok(motion.includes("row.el.style.setProperty('--fade-out', fade.out);"));
});

test('a repaint keeps the motion classes motion.js owns, mask state included', async () => {
  stubDom();
  const { renderChatLog } = await import('../js/ui/chat/view.js?render-motion');
  const view = chatViewSource();
  // Taken from motion.js's constants — a retyped list is what dropped .reveal-masked,
  // and the observer's "changed?" cache then never put it back.
  assert.ok(view.includes('const MOTION_CLASSES = [REVEAL_ITEM_CLASS, REVEAL_IN_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS,\n  REVEAL_SMOOTH_CLASS, REVEAL_NO_TRIGGER_CLASS, CHAT_ENTERING_CLASS];'));
  const transcript = makeEl();
  const log = [{ id: 1, role: 'user', text: 'hi' }];
  renderChatLog(transcript, log, {});
  const row = rowsOf(transcript)[0];
  for (const c of ['reveal-item', 'reveal-masked', 'reveal-entering', 'reveal-smooth', 'reveal-no-trigger',
                   'chat-entering']) row.classList.add(c);
  log[0].text = 'hi there';
  renderChatLog(transcript, log, {});
  // The observer writes the smooth-fade and no-trigger flags only when they CHANGE, so a
  // repaint that dropped one left the row grainy until the next threshold crossing.
  for (const c of ['reveal-item', 'reveal-masked', 'reveal-entering', 'reveal-smooth', 'reveal-no-trigger']) {
    assert.ok(row.classList.contains(c), `${c} survives the repaint`);
  }
  // One turn appends two rows (the message, then the pending "…"), so the second append
  // repaints the first: the arrival veil has to survive that repaint.
  assert.ok(row.classList.contains('chat-entering'), 'the arrival veil survives the repaint');
  assert.ok(row.classList.contains('chat-msg-user'), 'and the row still carries its own classes');
});
