// The Settings section's ghost icon buttons share the toolbar's standard icon box. Their 1px border
// is why the padding is 7/11 rather than the 8/12 the borderless buttons use:
// 16 + 14 + 2 = 32, 16 + 22 + 2 = 40.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { LAYOUT_CSS, COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';

const SHEETS = { 'layout.css': LAYOUT_CSS, 'components.css': COMPONENTS_CSS, 'animations.css': ANIMATIONS_CSS };
const read = (f) => SHEETS[f] ?? readFileSync(new URL(`../css/${f}`, import.meta.url), 'utf8');
// The declaration block for a selector, as written.
const ruleFor = (css, selector) => {
  const at = css.indexOf(selector + ' {');
  assert.ok(at >= 0, `rule not found: ${selector}`);
  return css.slice(at, css.indexOf('}', at));
};
const paddingOf = (block) => block.match(/padding:\s*([^;]+);/)?.[1].trim();

// The bordered ghost buttons: Settings' gear/palette/help/fullscreen/incognito, plus the
// theme toggle, which is the same treatment declared in its own file.
const GHOSTS = [
  ['components.css', '#settings-btn, #visuals-btn, #info-btn, #fullscreen-toggle, #incognito-toggle'],
  ['layout.css', '#theme-toggle'],
];

test('the bordered ghost buttons box to the same 40x32 as the other icon buttons', () => {
  for (const [file, selector] of GHOSTS) {
    const pad = paddingOf(ruleFor(read(file), selector));
    assert.equal(pad, '7px 11px',
                 `${selector} padding is ${pad} — with its 1px border that is not the 40x32 box`);
  }
});

test('the ghost buttons all agree with each other', () => {
  const pads = GHOSTS.map(([file, sel]) => paddingOf(ruleFor(read(file), sel)));
  assert.equal(new Set(pads).size, 1, `the ghost rows disagree: ${pads.join(' vs ')}`);
});

// The FLOAT chat is its own little window, so it flies out of the toolbar icon and shrinks back into
// it — the modals' own modalFromIcon/modalToIcon; chatPanel.js feeds it the icon→panel delta.
test('the floating chat panel animates from the toolbar icon', () => {
  const css = ANIMATIONS_CSS;
  const open = css.match(/stencil-chat-panel\.chat-open\.chat-dock-float\s*\{([^}]*)\}/)?.[1] || '';
  const close = css.match(/stencil-chat-panel\.chat-open\.chat-closing\.chat-dock-float\s*\{([^}]*)\}/)?.[1] || '';
  assert.match(open, /modalFromIcon/, `float open is "${open.trim()}"`);
  assert.match(close, /modalToIcon/, `float close is "${close.trim()}"`);

  const js = readFileSync(new URL('../js/ui/chat/chatPanel.js', import.meta.url), 'utf8');
  // The keyframes read these four; all of them have to be set, or the flight silently
  // falls back to the keyframes' plain-pop defaults.
  for (const v of ['--modal-dx', '--modal-dy', '--modal-sx', '--modal-sy'])
    assert.ok(js.includes(v), `chatPanel.js never sets ${v}`);
  // The close timer has to outlast the longer float flight, or the panel is torn out of the DOM
  // mid-motion, so docked shares the same 510ms (CLOSE_MS in chatPanel.js).
  const closeMs = js.match(/const CLOSE_MS = ([^;]+);/)?.[1] || '';
  assert.match(closeMs, /^510$/, `the close timer does not match modalToIcon: "${closeMs}"`);
});

// The clear animation runs only when an image was actually there, and a float → compact swap waits
// for the close animation before playing the compact panel's own entrance.
test('the float → mini chat swap waits for the close animation', () => {
  const js = readFileSync(new URL('../js/ui/chat/chatPanel.js', import.meta.url), 'utf8');
  const at = js.indexOf('const openCompact = (convert = false) => {');
  assert.ok(at > 0, 'openCompact is gone');
  const body = js.slice(at, at + 1400);
  // Mid-close (the eager click) is ridden out, not cancelled…
  assert.match(body, /chat-closing'\)\) \{ afterClose = showCompact; return; \}/,
    'a compact gesture cancels the in-flight close again');
  // …and an open panel is closed FIRST, with the sequel queued behind it.
  assert.match(body, /setOpen\(false\);\s*\n\s*afterClose = showCompact;/,
    'the outgoing shape no longer plays its exit before the swap');
  // The sequel fires from the close timer itself, after .chat-open comes off — one
  // panel on screen at a time.
  assert.match(js, /const next = afterClose;\s*\n\s*afterClose = null;\s*\n\s*next\?\.\(\);/);
  // Same host, same controller: the swap only re-shapes, it never rebuilds the panel or
  // the conversation (no re-render/clear of the transcript on this path).
  const swap = js.slice(at, js.indexOf('showCompact();\n    };', at));
  assert.ok(!/clearSharedConversation|innerHTML|remove\(\)/.test(swap),
    'the shape swap must not tear down the conversation');
});

test('newTemporary only animates when there was an image to clear', () => {
  const src = readFileSync(new URL('../js/core/storage/storage.js', import.meta.url), 'utf8');
  const at = src.indexOf('  newTemporary({');
  const body = src.slice(at, at + 3000);
  const guard = body.match(/const hadImage = ([^;]+);/)?.[1];
  assert.ok(guard, 'newTemporary no longer records whether an image was present');
  assert.match(guard, /this\.app\.image/, `the guard reads ${guard}, not the image`);
  // Both halves of the clear motion — the dust and the empty-state hold — sit behind ghostOut's own
  // verdict: with no dust under reduced motion, holding the emptied editor back only blanks it.
  const cond = body.match(/if \(ctx && hadImage && (.+)\) \{([\s\S]*?)\n    \}/);
  assert.ok(cond, 'the dust is no longer guarded');
  assert.match(cond[1], /ghostOut\(this\.app\.canvas\)/, 'the hold waits on the dust actually playing');
  assert.match(cond[2], /canvas-clearing/, 'the empty-state hold is no longer guarded');
});

// Clearing resets the canvas backing store and the viewport scroll: the idle "+ Blank image" card is
// position:absolute inset:0 in that same box and renders at the stale offset (user report).
test('newTemporary resets the canvas size/zoom and the viewport scroll, not just the pixels', () => {
  const src = readFileSync(new URL('../js/core/storage/storage.js', import.meta.url), 'utf8');
  const at = src.indexOf('  newTemporary({');
  const body = src.slice(at, at + 3000);
  assert.match(body, /this\.app\.canvas\.width = 0/, 'the backing store keeps its old (zoomed) footprint');
  assert.match(body, /this\.app\.canvas\.height = 0/, 'the backing store keeps its old (zoomed) footprint');
  assert.match(body, /this\.app\.canvas\.style\.width = ''/, 'a stale inline CSS width survives the clear');
  assert.match(body, /this\.app\.canvas\.style\.height = ''/, 'a stale inline CSS height survives the clear');
  assert.match(body, /this\.app\.scale = 1/, 'the zoom level is never reset on clear');
  assert.match(body, /resetViewportScroll\(\);/, 'the viewport scroll position is never reset on clear');
  // …and that helper (ui/layoutControls.js) is what actually puts it back to the corner.
  const controls = readFileSync(new URL('../js/ui/layoutControls.js', import.meta.url), 'utf8');
  assert.match(controls, /export const resetViewportScroll = \(\) => scrollViewportTo\(0, 0\);/);
  const scroll = controls.match(/export const scrollViewportTo[\s\S]{0,200}if \(vp\) \{ ([^}]+) \}/)?.[1];
  assert.ok(scroll, 'scrollViewportTo must touch the viewport');
  assert.match(scroll, /vp\.scrollLeft = left \|\| 0/);
  assert.match(scroll, /vp\.scrollTop = top \|\| 0/);
});

// The collapsed rail's chevron is an explicit flex square: as a `display: block` button its height
// came from the 16px font's line box plus the inline SVG's baseline gap — 24x25, glyph riding high.
test('the points-panel chevron is one centred square in both states', () => {
  const css = LAYOUT_CSS;
  const base = css.match(/\n#toggle-coord-panel \{([^}]*)\}/)?.[1] || '';
  assert.ok(base, 'the chevron rule is gone');
  // The box has to come from the RULE, not from font metrics — a 16px glyph in a text
  // line box gave a 30x22 pill open and a 28x28 square collapsed: same control, two shapes.
  assert.match(base, /display:\s*flex/, `still laid out as ${base.match(/display:\s*\w+/)?.[0]}`);
  assert.match(base, /align-items:\s*center/);
  assert.match(base, /justify-content:\s*center/);
  assert.match(base, /padding:\s*0/);
  assert.match(base, /line-height:\s*1\b/);
  const w = base.match(/width:\s*(\d+)px/)?.[1];
  const h = base.match(/height:\s*(\d+)px/)?.[1];
  assert.ok(w && h, 'the chevron has no explicit box, so its height follows the font');
  assert.equal(w, h, `the chevron is ${w}x${h}, not square`);
  // Collapsing may drop the outline, but it must NOT re-open the box.
  const rail = css.match(/\.coordinates-panel\.coord-collapsed #toggle-coord-panel \{([^}]*)\}/)?.[1] || '';
  assert.ok(rail, 'the collapsed rule is gone');
  for (const prop of ['width', 'height', 'padding', 'font-size', 'display'])
    assert.ok(!new RegExp(`${prop}\\s*:`).test(rail),
              `the collapsed rule re-declares ${prop}, so the two states can drift apart`);
});
