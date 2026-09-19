// components.css for the assistant: the install-button guard, #chat-btn's ghost colours and
// connections-row box, and the disabled/resizable/phone/touch rules. From chat-markup.test.js.
import { test } from 'node:test';
import assert from 'node:assert';

import { COMPONENTS_CSS } from './helpers/css.js';

// ── Install button vs chat panel: the CSS guard must exist (e2e found the floating
// "Get Stencil" button intercepting clicks on the docked panel's send button). ──
test('components.css guards the install button against the open chat panel', () => {
  const css = COMPONENTS_CSS;
  assert.ok(/body:has\(stencil-chat-panel\.chat-open\) #install-host \{ z-index: 89999; \}/.test(css),
    'install button drops beneath the open panel');
  assert.ok(css.includes('body:has(stencil-chat-panel.chat-open.chat-dock-right) #install-host'),
    'right dock shifts the install button left by --chat-size');
  assert.ok(css.includes('body:has(stencil-chat-panel.chat-open.chat-dock-bottom) #install-host'),
    'bottom dock lifts the install button by --chat-size');
  // The panel itself stacks below the modals but above the (lowered) install button.
  assert.ok(css.includes('z-index: 90000'), 'panel z-index present');
});

// #chat-btn keeps the Settings-row ghost COLOURS but takes the BOX of the connections row it sits in:
// no padding/font-size override, and an inset-shadow outline so the box never jumps.
test('components.css styles #chat-btn: ghost colours, CONNECTIONS-row geometry', () => {
  const css = COMPONENTS_CSS;
  assert.match(css, /#settings-btn, #visuals-btn, #info-btn, #fullscreen-toggle, #incognito-toggle \{/,
    'the Settings-row ghost group still exists (without #chat-btn, which is not in that row)');
  assert.match(css, /#fullscreen-toggle\.active, #incognito-toggle\.active, #chat-btn\.active \{/,
    'active accent-fill group includes #chat-btn');
  assert.ok(!css.includes('#chat-btn.chat-btn-active'), 'no one-off active rule left behind');

  const start = css.indexOf('\n#chat-btn {');
  assert.ok(start > -1, '#chat-btn has its own idle rule');
  const rule = css.slice(start, css.indexOf('}', start));
  assert.ok(/background:\s*transparent/.test(rule), 'idle is transparent, not an accent square');
  assert.ok(/box-shadow:\s*inset 0 0 0 1px/.test(rule), 'idle outline is an inset shadow (keeps the 40x32 box)');
  assert.ok(!/\bpadding:/.test(rule), 'no padding override — it inherits the toolbar button box its siblings use');
  assert.ok(!/\bfont-size:/.test(rule), 'no font-size override — the glyph matches #connect-btn/#links-btn');
  assert.match(css, /#chat-btn\.active \{ box-shadow: none; \}/, 'the accent fill replaces the idle outline');
});

// ── Disabled + resizable-input + mobile CSS (assertable without a DOM) ──
test('components.css: disabled ghosts, resizable input, phone modal, touch targets', () => {
  const css = COMPONENTS_CSS;
  assert.ok(css.includes('.chat-hbtn:disabled'), 'disabled styling for the compact buttons');
  const inputRule = css.slice(css.indexOf('#chat-input {'), css.indexOf('}', css.indexOf('#chat-input {')));
  assert.ok(inputRule.includes('resize: none'), 'native corner grip is off — the sizer strip owns resizing');
  assert.ok(inputRule.includes('max-height'), 'input growth is clamped');
  assert.ok(css.includes('.chat-input-sizer::before'), 'slider-style input handle has the pill affordance');
  assert.ok(css.includes('#chat-input:focus'), 'prompt textarea has the app focus glow');
  // Phone breakpoint follows the app convention (animations.css: 680px = phones).
  const phone = css.slice(css.indexOf('@media (max-width: 680px)'));
  assert.ok(phone.includes('stencil-chat-panel.chat-open'), 'panel has a phone layout');
  // Phones get an ORDINARY MODAL: a CENTRED card over the dimmed #chat-backdrop,
  // with every placement/resize affordance (incl. the input sizer) hidden.
  assert.ok(/inset: 0 !important/.test(phone) && /margin: auto !important/.test(phone), 'card is centred, not edge-pinned');
  assert.ok(/width: min\(420px, calc\(100vw - 24px\)\) !important/.test(phone), 'card has a bounded, centred width');
  assert.ok(/height: min\(560px, calc\(100dvh - 24px\)\) !important/.test(phone), 'card height follows the dynamic viewport');
  assert.ok(/body:has\(stencil-chat-panel\.chat-open\) #chat-backdrop \{ display: block; \}/.test(phone), 'backdrop shows only under the phone modal');
  assert.ok(phone.includes('stencil-chat-panel .chat-input-sizer { display: none; }') || /chat-input-sizer \{ display: none/.test(phone), 'input sizer hidden in the modal');
  assert.ok(phone.includes('safe-area-inset-bottom'), 'input row respects the safe area');
  // The backdrop is inert everywhere else — the dock/float shapes leave the page usable.
  const backdrop = css.slice(css.indexOf('#chat-backdrop {'), css.indexOf('}', css.indexOf('#chat-backdrop {')));
  assert.ok(backdrop.includes('display: none'), 'backdrop is hidden (and click-through) by default');
  assert.ok(backdrop.includes('position: fixed') && backdrop.includes('inset: 0'), 'backdrop covers the viewport');
  // A dock band on a phone-width viewport overflows the document and makes the
  // browser widen the layout viewport — so the reflow padding is min-width gated.
  const reflow = css.slice(css.indexOf('body:not(.fullscreen-mode):has(stencil-chat-panel.chat-open:not(.chat-closing).chat-dock-left)') - 400);
  assert.ok(/@media \(min-width: 681px\) \{[\s\S]{0,400}?padding-left: calc\(var\(--chat-size/.test(reflow),
    'dock reflow padding never applies at phone widths');
  // Touch: ≥40px targets + hidden resize handles (installButton.js media convention).
  const touch = css.slice(css.indexOf('@media (hover: none) and (pointer: coarse)'));
  assert.ok(/\.chat-hbtn, \.chat-abtn \{ width: 40px; height: 40px; \}/.test(touch), '40px coarse-pointer targets');
  assert.ok(touch.includes('.chat-resizer { display: none; }'), 'resize handles hide on touch');
});
