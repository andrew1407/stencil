// The transcript's smooth fade (js/ui/motion.js + components.css): the chat takes the gradient
// other reveal targets grain, and the mask reaches the chrome a row paints outside its box.
import { test } from 'node:test';
import assert from 'node:assert';
import { REVEAL_FEATHER } from '../../js/ui/motion.js';
import { motionSource } from '../helpers/motionSource.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from '../helpers/css.js';
import { chatViewSource } from '../helpers/chatViewSource.js';
import { projectsModalSource } from '../helpers/projectsModalSource.js';

test('the chat transcript takes the SMOOTH fade; other reveal targets keep the grain', () => {
  const view = chatViewSource();
  assert.match(view, /observeReveal\(transcript, '\[data-row\]', \{ smooth: true \}\)/,
    'text rows opt out of the dot grain');
  const projects = projectsModalSource();
  assert.match(projects, /observeReveal\(list, '\.project-row'\)/, 'project rows are untouched');
  const motion = motionSource();
  const apply = motion.slice(motion.indexOf('const apply = ()'), motion.indexOf('const schedule'));
  assert.match(apply, /if \(!smooth\) row\.el\.style\.setProperty\('--dissolve'/, 'no grain ramp on text');
  assert.match(apply, /smooth \? REVEAL_SMOOTH_FEATHER : REVEAL_FEATHER/, 'a fixed band, not 10%');
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
  assert.match(apply, /classList\.toggle\(REVEAL_NO_TRIGGER_CLASS, hide\)/);
});

test('the trigger reads --visible-bottom, and the observer publishes it for EVERY row', () => {
  const css = COMPONENTS_CSS;
  const btn = /\.chat-row-menu-btn \{([\s\S]*?)\n\}/.exec(css)[1];
  assert.match(btn, /bottom: calc\(var\(--visible-bottom, 0px\) \+ var\(--row-menu-lift, 0px\)\)/,
    'anchored to the visible slice, plus the jump-pill clearance lift');
  assert.match(btn, /width: 21px; height: 21px/, 'the size the geometry above assumes');
  // Hover reveals the trigger, so the rule hangs off the ROW — what the pointer is over — and
  // only the hovered trigger itself goes fully opaque.
  assert.match(css, /\.chat-msg:hover \.chat-row-menu-btn \{ opacity: \.7; \}/);
  assert.match(css, /\.chat-msg:hover \.chat-row-menu-btn:hover \{ opacity: 1; \}/);
  const motion = motionSource();
  const apply = motion.slice(motion.indexOf('const apply = ()'), motion.indexOf('const schedule'));
  const published = apply.indexOf("setProperty('--visible-bottom'");
  assert.ok(published > 0, 'apply() publishes it');
  assert.ok(published < apply.indexOf('if (!masked) continue;'),
    'BEFORE the masked-only bailout — an unmasked row needs it too');
  assert.ok(apply.includes('if (vb !== row.visBottom)'), 'written only when it moves');
  // One scroll listener, not two: it rides the reveal observer already bound per
  // transcript (view.js), so nothing else has to watch the scroller.
  const view = chatViewSource();
  assert.ok(view.includes("observeReveal(transcript, '[data-row]'"));
  assert.ok(!view.includes("transcript.addEventListener('scroll'"),
    'the renderer adds no scroll listener of its own');
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
