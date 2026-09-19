// The wiring around the message menu that a stub DOM cannot reach — popup/assistant.js's
// contextmenu hook, the real clipboard and send loop, and the CSS the trigger and bubbles lean on.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { animationsCss, assistantSrc, popupCss, themeCss } from './helpers/sources.js';
import { readFileSync } from 'node:fs';

// ── assistant.js wiring (source assertions — the stub DOM cannot reach these) ──
const assistant = assistantSrc();
const css = popupCss();

test('the transcript wires the menu on right-click, skipping notes and typing rows', () => {
  assert.match(assistant, /transcriptEl\.addEventListener\('contextmenu'/);
  assert.match(assistant, /closest\?\.\('\.msg'\)/);
  assert.match(assistant, /classList\.contains\('note'\) \|\| el\.classList\.contains\('typing-row'\)/);
});

test('copy uses the real clipboard', () => {
  assert.match(assistant, /navigator\.clipboard\?\.writeText\(t\.text\)/);
});

test('insert appends into the composer; resend rides the normal send loop', () => {
  assert.match(assistant, /insert: \(t\) => appendToPrompt\(inputEl, t\.text\)/);
  assert.match(assistant, /if \(!state\.busy\) send\(t\.resendText, t\.attachments\)/);
});

test('the user turn records its original text + attachments for resend', () => {
  assert.match(assistant, /msgMeta\.set\(userEl, \{ text: text \|\| '\(attached images\)', resendText: text, attachments \}\)/);
});

test('the menu dismisses on outside press and transcript scroll; Escape is its own', () => {
  assert.match(assistant, /msgMenu\.isOpen\(\) && !msgMenu\.el\.contains\(e\.target\)\) msgMenu\.close\(\)/);
  // Escape moved INTO chatMsgMenu.js (open-scoped); no lingering document listener here.
  assert.doesNotMatch(assistant, /'Escape'\) msgMenu\.close\(\)/);
  assert.match(assistant, /transcriptEl\.addEventListener\('scroll', \(\) => msgMenu\.close\(\)\)/);
});

test('message text is selectable; the menu itself is not', () => {
  assert.match(css, /#sec-assistant \.msg \{[^}]*user-select: text/);
  assert.match(css, /\.chat-msg-menu \{[^}]*user-select: none/);
});

test('the message menu hugs its content; other action menus keep their floor', () => {
  assert.match(css, /\.chat-msg-menu \{[^}]*min-width: 0/);
  assert.match(css, /\.chat-msg-menu \{[^}]*width: max-content/);
  assert.match(css, /\.action-menu \{[^}]*min-width: 168px/);
});

test('every bubble (never notes) grows the hover trigger, wired through openMenuAt', () => {
  assert.match(assistant, /if \(cls !== 'note'\) state\.addMsgMenuBtn\(el\);/);
  assert.match(assistant, /createMsgMenuButton\(\{/);
  assert.match(assistant, /openMenuAt\(el, e\.clientX, e\.clientY\)/);   // right-click, same path
  assert.match(assistant, /openMenuAt\(el, r\.left, r\.bottom \+ 2\)/);  // "⋯", anchored at its rect
});

test('the shared pop keyframe lives on .action-menu and respects reduced motion', () => {
  assert.match(css, /\.action-menu \{[^}]*animation: action-menu-pop \.13s ease-out/);
  assert.match(css, /@keyframes action-menu-pop \{\s*from \{ transform: scale\(\.62\); opacity: 0; \}/);
  assert.match(css, /@media \(prefers-reduced-motion: reduce\) \{\s*\.action-menu \{ animation: none; \}/);
});

const actionMenu = readFileSync(new URL('../src/lib/actionMenu.js', import.meta.url), 'utf8');
const theme = themeCss();
const anim = animationsCss();

test('the shared action menu places via the origin helper; its Escape is armed per open', () => {
  assert.match(actionMenu, /menuEl\.style\.transformOrigin = menuTransformOrigin\(\{/);
  assert.match(actionMenu, /const armEscape = \(\) => doc\.addEventListener\('keydown', onEscape, true\)/);
  assert.match(actionMenu, /doc\.removeEventListener\('keydown', onEscape, true\)/);   // in close()
  assert.match(actionMenu, /e\.preventDefault\(\);\s*e\.stopPropagation\(\);\s*close\(\)/);  // stops the surface's own Escape
});

test('the trigger is an absolute ghost, revealed only for hover-capable pointers', () => {
  assert.match(css, /\.msg-menu-btn \{[^}]*position: absolute/);
  assert.match(css, /\.msg-menu-btn \{[^}]*opacity: 0/);
  assert.match(css, /\.msg-menu-btn \{[^}]*user-select: none/);
  assert.match(css, /#sec-assistant \.msg \{[^}]*position: relative/);   // the button's anchor
  assert.match(css, /@media \(hover: hover\) \{\s*#sec-assistant \.msg:hover \.msg-menu-btn \{[^}]*opacity: \.7/);
  assert.match(css, /#sec-assistant \.msg:hover \.msg-menu-btn:hover \{[^}]*opacity: 1/);
  assert.match(css, /\.msg-menu-btn\.left \{ left: -24px; \}/);   // beside the bubble, no overlap
  assert.match(css, /\.msg-menu-btn\.right \{ right: -24px; \}/);
  // A circle, like the browser's and the desktop's — not the 6px rounded square it was.
  assert.match(css, /\.msg-menu-btn \{[^}]*border-radius: 50%/);
  // Hittable at rest, and the gap to the bubble bridged: crossing it must not un-hover
  // the row and take the button away mid-reach.
  const btnRule = /\.msg-menu-btn \{([^}]*)\n\}/.exec(css)[1].replace(/\/\*[\s\S]*?\*\//g, '');
  assert.ok(!btnRule.includes('pointer-events'), 'no pointer-events: none to fall through');
  assert.match(css, /\.msg-menu-btn::before \{[^}]*position: absolute/);
  assert.match(css, /\.msg-menu-btn\.left::before \{ left: 100%; \}/);
  assert.match(css, /\.msg-menu-btn\.right::before \{ right: 100%; \}/);
  // The leave grace (desktop scheduleHide parity), cancelled on the way in.
  assert.match(css, /\.msg-menu-btn \{[^}]*transition: opacity \.12s ease \.22s/);
  assert.match(css, /#sec-assistant \.msg:hover \.msg-menu-btn \{[^}]*transition-delay: 0s/);
});

test('the failed-turn bubble has a danger tone to reach for', () => {
  // --danger is USED by #sec-assistant .msg.error, so leaving it undefined makes every one of
  // those declarations invalid at computed-value time.
  assert.match(theme, /:root, :root\[data-theme="light"\] \{[\s\S]*?--danger: #d6293e;[\s\S]*?\n\}/);
  assert.match(theme, /:root\[data-theme="dark"\] \{[\s\S]*?--danger: #f0697a;[\s\S]*?\n\}/);
  // Browser .chat-msg-error parity, tail included. --msg-border/--msg-fill, not the raw
  // color-mix() inline: the tail reuses the SAME two values.
  const err = /#sec-assistant \.msg\.error \{([\s\S]*?)\n\}/.exec(css)[1];
  assert.match(err, /color: var\(--danger\)/);
  assert.match(err, /--msg-border: color-mix\(in srgb, var\(--danger\) 45%, transparent\)/);
  assert.match(err, /border: 1px solid var\(--msg-border\)/);
  assert.match(err, /--msg-fill: color-mix\(in srgb, var\(--danger\) 10%, var\(--panel-2\)\)/);
  assert.match(err, /background: var\(--msg-fill\)/);
});

test('the reveal mask reaches the "…" the row paints outside its box', () => {
  const masked = anim.slice(anim.indexOf('.reveal-item.reveal-masked {'),
    anim.indexOf('\n}', anim.indexOf('.reveal-item.reveal-masked {')));
  // Clipped to the row's box, the wipe layer erased the trigger beside the bubble
  // entirely — invisible and un-hittable on any row the scroller cuts.
  assert.match(masked, /mask-size: 4px 4px, 7px 7px, 11px 11px, 300% 100%/);
  assert.match(masked, /mask-position: 0 0, 2px 3px, 5px 1px, center top/);
  assert.match(masked, /\n {4}mask-clip: no-clip/);
  assert.match(masked, /-webkit-mask-clip: no-clip/);
});
