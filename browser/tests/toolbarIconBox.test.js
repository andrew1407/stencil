// The Settings section's ghost icon buttons share the toolbar's standard icon box.
//
// They carried `font-size: 18px; padding: 4px 10px` from the days when the glyph was an
// emoji, so with a 16px SVG they measured 38x26 next to every other .btn-icon's 40x32 —
// visibly smaller marks in the same row. Their 1px border is why the padding is 7/11
// rather than the 8/12 borderless buttons use: 16 + 14 + 2 = 32, 16 + 22 + 2 = 40.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';

const read = (f) => (f === 'components.css' ? COMPONENTS_CSS
  : f === 'animations.css' ? ANIMATIONS_CSS
  : readFileSync(new URL(`../css/${f}`, import.meta.url), 'utf8'));
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

// The FLOAT chat is its own little window, so it flies out of the toolbar icon and shrinks
// back into it — the same modalFromIcon/modalToIcon motion the modals use — instead of the
// old pop-in-place. chatPanel.js feeds the keyframes the icon→panel delta.
test('the floating chat panel animates from the toolbar icon', () => {
  const css = ANIMATIONS_CSS;
  const open = css.match(/stencil-chat-panel\.chat-open\.chat-dock-float\s*\{([^}]*)\}/)?.[1] || '';
  const close = css.match(/stencil-chat-panel\.chat-open\.chat-closing\.chat-dock-float\s*\{([^}]*)\}/)?.[1] || '';
  assert.match(open, /modalFromIcon/, `float open is "${open.trim()}"`);
  assert.match(close, /modalToIcon/, `float close is "${close.trim()}"`);

  const js = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  // The keyframes read these four; all of them have to be set, or the flight silently
  // falls back to the keyframes' plain-pop defaults.
  for (const v of ['--modal-dx', '--modal-dy', '--modal-sx', '--modal-sy'])
    assert.ok(js.includes(v), `chatPanel.js never sets ${v}`);
  // …and the close timer has to outlast the longer float flight, or the panel is torn
  // out of the DOM mid-motion. Docked shares the same 340ms now (the dust flight needs
  // it as much as the float shape does — see CLOSE_MS in chatPanel.js).
  const closeMs = js.match(/const CLOSE_MS = ([^;]+);/)?.[1] || '';
  assert.match(closeMs, /^340$/, `the close timer does not match modalToIcon: "${closeMs}"`);
});

// Boot starts the editor blank by calling storage.newTemporary(), which also plays the
// CLEAR animation — dust plus a `canvas-clearing` hold that hides the empty-state card.
// On a fresh page there is nothing to clear, so the "＋ Blank image" button blinked off
// and back on during load. The animation now runs only when an image was actually there.
// Float → mini (compact) chat: the outgoing shape used to be re-pointed at the compact
// rect while it was still on screen, so the float blinked out with no exit at all — and
// the eager first click of the double-click had its close CANCELLED by the second. The
// swap now waits for the close animation, then plays the compact panel's own entrance.
test('the float → mini chat swap waits for the close animation', () => {
  const js = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
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
  const src = readFileSync(new URL('../js/core/storage.js', import.meta.url), 'utf8');
  const at = src.indexOf('  newTemporary({');
  const body = src.slice(at, at + 3000);
  const guard = body.match(/const hadImage = ([^;]+);/)?.[1];
  assert.ok(guard, 'newTemporary no longer records whether an image was present');
  assert.match(guard, /this\.app\.image/, `the guard reads ${guard}, not the image`);
  // Both halves of the clear motion sit behind it: the dust and the empty-state hold.
  // The hold is behind ghostOut's own verdict too — under reduced motion no dust falls,
  // and holding the emptied editor back anyway just blanks it for a second.
  const cond = body.match(/if \(ctx && hadImage && (.+)\) \{([\s\S]*?)\n    \}/);
  assert.ok(cond, 'the dust is no longer guarded');
  assert.match(cond[1], /ghostOut\(this\.app\.canvas\)/, 'the hold waits on the dust actually playing');
  assert.match(cond[2], /canvas-clearing/, 'the empty-state hold is no longer guarded');
});

// Regression: clearing an image used to leave the canvas at its old (possibly zoomed)
// backing-store size and the viewport at its old scroll offset. The idle "+ Blank image"
// card is position:absolute; inset:0 inside that SAME scrolled box, so it rendered at the
// stale offset instead of centred — invisible or off past the fold — until the next zoom
// or scroll touched it (user report: a "clank"/ghost image, scroll and zoom left over
// after clearing).
test('newTemporary resets the canvas size/zoom and the viewport scroll, not just the pixels', () => {
  const src = readFileSync(new URL('../js/core/storage.js', import.meta.url), 'utf8');
  const at = src.indexOf('  newTemporary({');
  const body = src.slice(at, at + 3000);
  assert.match(body, /this\.app\.canvas\.width = 0/, 'the backing store keeps its old (zoomed) footprint');
  assert.match(body, /this\.app\.canvas\.height = 0/, 'the backing store keeps its old (zoomed) footprint');
  assert.match(body, /this\.app\.canvas\.style\.width = ''/, 'a stale inline CSS width survives the clear');
  assert.match(body, /this\.app\.canvas\.style\.height = ''/, 'a stale inline CSS height survives the clear');
  assert.match(body, /this\.app\.scale = 1/, 'the zoom level is never reset on clear');
  const scroll = body.match(/const vp = document\.getElementById\('canvas-viewport'\);\s*\n\s*if \(vp\) \{ ([^}]+) \}/)?.[1];
  assert.ok(scroll, 'the viewport scroll position is never reset on clear');
  assert.match(scroll, /vp\.scrollLeft = 0/);
  assert.match(scroll, /vp\.scrollTop = 0/);
});

// Collapsed to its rail, the points/lines panel shows one chevron. As a `display: block`
// button its height came from the 16px font's line box plus the inline SVG's baseline gap
// — 24x25 with the glyph riding high and dead space beneath it inside the rail. An
// explicit flex square takes text metrics out of the box entirely.
test('the points-panel chevron is one centred square in both states', () => {
  const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
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
