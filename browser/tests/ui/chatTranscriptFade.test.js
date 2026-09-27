// The transcript's smooth fade (js/ui/motion.js + components.css): the chat takes the gradient
// other reveal targets grain, and the mask reaches the chrome a row paints outside its box.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  observeReveal, REVEAL_SMOOTH_FEATHER, REVEAL_SMOOTH_CLASS, REVEAL_NO_TRIGGER_CLASS, REVEAL_ITEM_CLASS,
} from '../../js/ui/motion.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from '../helpers/css.js';
import { mountProjectsModal } from '../helpers/projectsModalRig.js';
import { makeEl, stubDom, rowsOf } from '../helpers/chatTranscriptRig.js';
import { revealScroller } from '../helpers/revealRig.js';

// A painted transcript whose reveal observer really runs: laid-out rows, and the
// MutationObserver's callback handed back to the test to fire.
const paintedTranscript = async (t, tag, log) => {
  stubDom();
  const make = document.createElement;
  document.createElement = (tag) => Object.assign(make(tag), { getBoundingClientRect: () => ({ top: 0, height: 20 }) });
  const observers = [];
  globalThis.MutationObserver = class { constructor(cb) { observers.push(cb); } observe() {} disconnect() {} };
  globalThis.requestAnimationFrame = () => 0;
  t.after(() => { delete globalThis.MutationObserver; delete globalThis.requestAnimationFrame; });
  const { renderChatLog } = await import(`../../js/ui/chat/view.js?fade-${tag}`);
  const transcript = Object.assign(makeEl(), { getBoundingClientRect: () => ({ top: 0 }), clientHeight: 400 });
  const paint = () => { renderChatLog(transcript, log, {}); observers.forEach((cb) => cb()); };
  paint();
  return { transcript, paint };
};

test('the chat transcript takes the SMOOTH fade; other reveal targets keep the grain', async (t) => {
  // Mounted first: its globals are put back after the transcript's, whichever order the hooks run.
  const projects = await mountProjectsModal(t, { metas: [{ id: 'a', name: 'Alpha', updatedAt: 1 }] });
  projects.change('projects-filter', 'local');
  projects.scan();
  const [project] = projects.rows();
  assert.ok(project.classList.contains(REVEAL_ITEM_CLASS) && !project.classList.contains(REVEAL_SMOOTH_CLASS),
    'project rows are untouched');
  const { transcript } = await paintedTranscript(t, 'smooth', [{ id: 1, role: 'user', text: 'hi' }]);
  assert.ok(rowsOf(transcript)[0].classList.contains(REVEAL_SMOOTH_CLASS), 'text rows opt out of the dot grain');
  // No grain ramp on text, and a fixed band rather than 10%.
  const { root, els: [grain], frame } = revealScroller([[-120, 146]]);
  t.after(() => { delete globalThis.requestAnimationFrame; });
  observeReveal(root, '.project-row');
  const text = revealScroller([[-120, 146]]);
  observeReveal(text.root, '[data-row]', { smooth: true });
  frame();
  text.frame();
  const dissolves = (row) => row.writes.filter(([k]) => k === '--dissolve').length;
  assert.deepStrictEqual([dissolves(grain), dissolves(text.els[0])], [2, 1], 'only the grain row is ramped per frame');
  assert.strictEqual(text.els[0].prop('--fade-in'), REVEAL_SMOOTH_FEATHER);
  // A row that cannot place its trigger hides it outright.
  const cut = revealScroller([[-100, 110]]);
  observeReveal(cut.root, '[data-row]', { smooth: true });
  cut.frame();
  assert.ok(cut.els[0].classes.has(REVEAL_NO_TRIGGER_CLASS), 'a 10px slice has no room for the trigger');
  // The mask itself: one linear layer, no radial grain tiles, and it still reaches the
  // chrome the row paints outside its box.
  const css = ANIMATIONS_CSS;
  const smooth = css.slice(css.indexOf('.reveal-item.reveal-smooth.reveal-masked {'),
    css.indexOf('.reveal-item.reveal-entering'));
  assert.ok(!smooth.includes('radial-gradient'), 'no dot grain on text rows');
  assert.ok(!smooth.includes('mask-composite'), 'nothing to compose — one layer');
  assert.match(smooth, /mask-size: 300% 100%/);
  assert.match(smooth, /mask-clip: no-clip/);
  // …while the grainy original is still there for everything else.
  assert.match(css, /\.reveal-item\.reveal-masked \{[\s\S]*?radial-gradient/);
  // A row that cannot place its trigger hides it outright.
  const comp = COMPONENTS_CSS;
  assert.match(comp, /\.chat-msg\.reveal-no-trigger \.chat-row-menu-btn \{ display: none; \}/);
});

test('the trigger reads --visible-bottom, and the observer publishes it for EVERY row', async (t) => {
  const css = COMPONENTS_CSS;
  const btn = /\.chat-row-menu-btn \{([\s\S]*?)\n\}/.exec(css)[1];
  assert.match(btn, /bottom: calc\(var\(--visible-bottom, 0px\) \+ var\(--row-menu-lift, 0px\)\)/,
    'anchored to the visible slice, plus the jump-pill clearance lift');
  assert.match(btn, /width: 21px; height: 21px/, 'the size the geometry above assumes');
  // Hover reveals the trigger, so the rule hangs off the ROW — what the pointer is over — and
  // only the hovered trigger itself goes fully opaque.
  assert.match(css, /\.chat-msg:hover \.chat-row-menu-btn \{ opacity: \.7; \}/);
  assert.match(css, /\.chat-msg:hover \.chat-row-menu-btn:hover \{ opacity: 1; \}/);
  // Published for an UNMASKED row too (before the masked-only bailout), and only when it moves.
  const { root, els: [whole], listeners, frame } = revealScroller([[100, 60]]);
  t.after(() => { delete globalThis.requestAnimationFrame; });
  observeReveal(root, '[data-row]', { smooth: true });
  frame();
  const published = () => whole.writes.filter(([k]) => k === '--visible-bottom');
  assert.deepStrictEqual(published(), [['--visible-bottom', '0px']], 'a wholly visible row still gets it');
  listeners.scroll.forEach((fn) => fn());
  frame();
  assert.strictEqual(published().length, 1, 'written only when it moves');
  // One scroll listener, not two: it rides the reveal observer already bound per transcript,
  // so nothing else has to watch the scroller.
  const { transcript, paint } = await paintedTranscript(t, 'scroll', [{ id: 1, role: 'user', text: 'hi' }]);
  paint();
  assert.strictEqual(transcript._l.scroll.length, 1, 'the renderer adds no scroll listener of its own');
});

test('the reveal mask reaches the chrome a row paints OUTSIDE its box', () => {
  const css = ANIMATIONS_CSS;
  const rest = css.slice(css.indexOf('.reveal-item.reveal-masked {'),
    css.indexOf('\n}', css.indexOf('.reveal-item.reveal-masked {')));
  // The "…" trigger sits BESIDE the bubble, outside the masked box, so clipping the mask to
  // that box leaves it invisible and un-hittable; both spellings are pinned.
  assert.strictEqual(rest.split('mask-clip: no-clip').length - 1, 2, 'the mask does not clip to the box');
  // …and the wipe layer is stretched past the box, or the area outside it would be
  // covered only by the (nearly transparent) dot grain.
  assert.strictEqual(rest.split('mask-size: 4px 4px, 7px 7px, 11px 11px, 300% 100%').length - 1, 2);
  assert.strictEqual(rest.split('mask-position: 0 0, 2px 3px, 5px 1px, center top').length - 1, 2,
    'centred horizontally, top-anchored — the `to bottom` gradient is unchanged');
});

test('the row text node is what CSS and the renderer agree on', () => {
  const css = COMPONENTS_CSS;
  assert.match(css, /\.chat-msg-text \{ min-width: 0; \}/);
});
