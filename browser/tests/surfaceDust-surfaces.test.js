// The surface-dust CSS contract and each surface's origin point: modals, the chat panel, the
// context menu and the ⋯/… overflow menus. Split from surfaceDust.test.js.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { motionSource } from './helpers/motionSource.js';

import {
  tileNoise, SURFACE_IN_MS, SURFACE_OUT_MS,
} from '../js/ui/motion.js';
import { FLIGHTS, alphaAt } from '../js/ui/dust/cloud.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { chatViewSource } from './helpers/chatViewSource.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';
import { modalShellSource } from './helpers/modalShellSource.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const cloudJs = read('../js/ui/dust/cloud.js');
const animCss = ANIMATIONS_CSS;
const baseJs = modalShellSource();
const chatPanelJs = read('../js/ui/chat/panel.js');
const confirmJs = read('../js/ui/modal/confirmModal.js');
const ctxJs = contextMenuSource();
const rowMenuJs = read('../js/ui/projects/window/projectRowMenu.js');
const chatViewJs = chatViewSource();
const llmSettingsJs = read('../js/ui/llmSettings/modal.js');
const motionJs = motionSource();

// ── 5. The CSS contract ─────────────────────────────────────────────────────

test('the surface flights are the row’s scatter, re-timed and re-aimed', () => {
  // Same grain, same waypoint arithmetic as a row's (cloud.js FLIGHTS): a gather
  // starts at the far end and flies home, a scatter the other way.
  assert.equal(FLIGHTS.surfaceGather.from, 'far');
  assert.equal(FLIGHTS.surfaceScatter.from, 'home');
  // A surface's motes are visible from the first frame — they ARE the window.
  assert.equal(alphaAt(FLIGHTS.surfaceGather.alpha, 0), 0.55);
  assert.equal(alphaAt(FLIGHTS.surfaceScatter.alpha, 0), 1);
  // …and their ease-out covers most of the trip early, so the bend sits early too.
  assert.ok(FLIGHTS.surfaceGather.split <= 0.2 && FLIGHTS.surfaceScatter.split <= 0.2);
  assert.ok(FLIGHTS.surfaceGather.rest(0.3) > 0.8, 'ease-out: most of the trip in the first third');
  // Both ride the surface's own clock (motion.js writes --dust-ms / --gather-ms on the
  // host for its cross-fades, and each grain's duration inline).
  assert.match(motionJs, /const gatherMs = toward \|\| !gather \? span : Math\.round\(span \* TILE_GATHER_SHARE\);/);
  assert.match(motionJs, /host\.style\.setProperty\('--gather-ms', `\$\{gatherMs\}ms`\);/);
});

test('the surface waits behind its dust, and hands over to it on the way out', () => {
  assert.match(animCss, /@keyframes surfaceForm\s+\{ 0%, \d+% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/);
  assert.match(animCss, /@keyframes surfaceLeave \{ 0% \{ opacity: 1; \} \d+%, 100% \{ opacity: 0; \} \}/);
  assert.match(animCss, /\.dust-driven\.surface-forming \{ animation: surfaceForm var\(--dust-ms, \d+ms\) linear both !important; \}/);
  assert.match(animCss, /\.dust-driven\.surface-leaving \{ animation: surfaceLeave var\(--dust-ms, \d+ms\) linear both !important; \}/);
  // The cloud cross-fades with the surface at each hand-over, so a gather cannot end on
  // a flat slab of the panel's colour and a scatter cannot start on a hard cut.
  assert.match(animCss, /\.disintegrate-host\.dust-leaving \{ animation: dustHostIn /);
  assert.match(animCss, /\.disintegrate-host\.dust-forming \{ animation: dustHostOut /);
});

test('the layer can never take a click or hold focus, and never moves the page', () => {
  const host = animCss.match(/\.disintegrate-host \{([\s\S]*?)\n\}/)[1];
  assert.match(host, /position: fixed;/, 'out of flow — no reflow, ever');
  assert.match(host, /pointer-events: none;/);
  // The motes are pixels on ONE canvas inside the layer (cloud.js) — no node per
  // grain, nothing with text or a tabindex, so there is nothing focusable at all.
  assert.match(motionJs, /startCloud\(host, motes, \{ flight: kind, span, colours: fills, origin/);
  assert.match(cloudJs, /canvas\.style\.cssText = `position:absolute;[\s\S]{0,200}?pointer-events:none;`/);
  assert.match(cloudJs, /host\.appendChild\(canvas\);/);
  assert.ok(!/disintegrate-tile/.test(motionJs), 'no node per grain any more');
  // The layer never waits on the paint: a document without a 2D canvas (tests) still
  // gets the bookkeeping, and the loop is stopped before the layer goes.
  assert.match(cloudJs, /const ctx = canvas\.getContext\?\.\('2d'\);\s*\n\s*if \(!ctx\) return noop;/);
  assert.match(motionJs, /el\.__dustHost\?\.__stop\?\.\(\);/);
});

test('reduced motion: no cloud, no veil — the surface simply is, or is not', () => {
  assert.match(animCss, /@media \(prefers-reduced-motion: reduce\) \{\s*\n\s*\.disintegrate-host \{ display: none; \}/);
  assert.match(animCss, /\.dust-driven, \.dust-driven\.surface-forming, \.dust-driven\.surface-leaving \{ animation: none !important; \}/);
});

// ── 6. Per surface: the origin is still the icon ────────────────────────────

test('modals: the dust point IS the icon centre setOriginVars already measured', () => {
  assert.match(baseJs, /originPoint = \{ x: cx, y: cy \};/);
  // The very same cx/cy that feed --modal-dx/dy, so the flight cannot drift from the
  // old one: an on-screen opener's centre, or above the box when it is unreachable.
  assert.match(baseJs, /const cx = onScreen \? a\.left \+ a\.width \/ 2 : b\.left \+ b\.width \/ 2;/);
  assert.match(baseJs, /const cy = onScreen \? a\.top \+ a\.height \/ 2 : -Math\.max\(48, b\.height \* 0\.3\);/);
  // Both shapes of open play it, and the close plays it measured while still open.
  assert.match(baseJs, /overlay\.classList\.add\('modal-open'\);[\s\S]{0,200}if \(!reducedMotion\(\) && setOriginVars\(\)\) playDust\(true\);/);
  assert.match(baseJs, /overlay\.classList\.add\('modal-open', 'modal-popover'\);[\s\S]*?if \(!reducedMotion\(\) && setOriginVars\(\)\) playDust\(true\);/);
  assert.match(baseJs, /overlay\.classList\.add\('modal-closing'\);\s*\n\s*playDust\(false\);/);
  // The window still goes away on its own clock — the close never waits on the effect.
  assert.match(baseJs, /const MODAL_CLOSE_MS = SURFACE_OUT_MS;/);
  assert.match(baseJs, /flight\.settle\(\);\s*\/\/ nothing plays/);
  // …and the CONFIRM dialog, which has no opener icon at all, plays the very same
  // flight out of the gesture that raised it (ui/gesturePoint.js).
  assert.match(confirmJs, /createModalFlight\(overlay, \(\) => overlay\.querySelector\('\.app-modal'\)\)/);
  assert.match(confirmJs, /openAnchor = gestureAnchorRect\(\);[\s\S]{0,200}if \(!flight\.reducedMotion\(\) && flight\.setOrigin\(openAnchor\)\) flight\.playDust\(true\);/);
  assert.match(confirmJs, /if \(animate\) flight\.playClosing\(\);/);
  // The close reuses the anchor the dialog opened with, not a fresh gestureAnchorRect(), unless the
  // caller named a closeAnchor — a context-menu row is gone by the time it lands.
  assert.match(confirmJs, /flight\.setOrigin\(rectOf\(closeAnchorEl\) \|\| openAnchor\);\s*\n\s*overlay\.classList\.remove\('modal-open'\);/);
  assert.ok(!/const settle = \(val\) => \{[\s\S]{0,200}gestureAnchorRect\(\)/.test(confirmJs),
    'settle() must not recompute the gesture point — it would anchor on the button that just closed it');
});

test('the chat panel keeps its dock edge, and a float keeps its icon', () => {
  assert.match(chatPanelJs, /if \(host\.classList\.contains\('chat-dock-float'\)\) \{[\s\S]*?anchorBtn\(\)/,
    'a float flies out of the toolbar icon, like a modal');
  assert.match(chatPanelJs, /return dockAwayPoint\(r, chatDock\.mode\(\)\) \|\| \{ x: r\.left \+ r\.width \/ 2, y: -Math\.max\(48, r\.height \* 0\.3\) \};/);
  // Opened AFTER the class, or the panel is display:none and measures nothing.
  assert.match(chatPanelJs, /host\.classList\.toggle\('chat-open', on\);[\s\S]{0,160}if \(on\) playDust\(true\);/);
  // Closed BEFORE it leaves the screen, and on the same clock the class swap uses.
  assert.match(chatPanelJs, /playDust\(false\);\s*\/\/ …measured while it is still on screen\s*\n\s*host\.classList\.add\('chat-closing'\);/);
  assert.match(chatPanelJs, /ms: enter \? 630 : CLOSE_MS/);
});

test('the context menu forms out of the very click it was opened at', () => {
  assert.match(ctxJs, /const openAt = \(x, y\) => \{\s*\n\s*openPoint = \{ x, y \};/);
  assert.match(ctxJs, /if \(!motionReduced\(\)\) surfaceIn\(menu, openPoint, \{ ms: SURFACE_MENU_IN_MS \}\);/);
  // Measured while still open, and only when it IS — closeMenu is also the idle teardown.
  assert.match(ctxJs, /if \(menu\.classList\.contains\('ctx-open'\) && !motionReduced\(\)\) surfaceOut\(menu, openPoint, \{ ms: SURFACE_MENU_OUT_MS \}\);/);
  assert.ok(ctxJs.indexOf('surfaceOut(menu, openPoint)') < ctxJs.indexOf("menu.classList.remove('ctx-open')"));
  // The pop it replaces set a transform, which would have made the menu the containing
  // block for its position:fixed flyouts. The dust drives opacity only.
  assert.ok(!/surface-forming[\s\S]*transform/.test(animCss.slice(animCss.indexOf('@keyframes surfaceForm'),
    animCss.indexOf('@keyframes surfaceLeave'))), 'surfaceForm animates opacity alone');
});

test('the ⋯ overflow menus grow out of the button (or the right-click) that opened them', () => {
  // Projects (ui/projectRowMenu.js): the cursor for a right-click, else the "⋯" centre — and
  // back into it; the node still goes NOW, the layer owns its own lifetime.
  assert.match(rowMenuJs, /menuPoint = point \|\| rectCenter\(anchor\);/);
  assert.match(rowMenuJs, /surfaceIn\(menu, menuPoint, \{ ms: SURFACE_MENU_IN_MS \}\);/);
  assert.match(rowMenuJs, /surfaceOut\(openMenu, menuPoint, \{ ms: SURFACE_MENU_OUT_MS \}\);\s*\n\s*openMenu\.remove\(\);/);
  // The chat bubble's "⋯" is cursor-anchored, same open point both ways.
  assert.match(chatViewJs, /surfaceIn\(menu, \{ x, y \}, \{ ms: SURFACE_MENU_IN_MS \}\);/);
  assert.match(chatViewJs, /surfaceOut\(menu, \{ x, y \}, \{ ms: SURFACE_MENU_OUT_MS \}\);\s*\n\s*menu\.remove\(\);/);
});

test('the composer "…" menu forms out of, and pours back into, its own trigger', () => {
  const wire = chatViewJs.slice(chatViewJs.indexOf('export const wireChatMoreMenu'));
  // The point is the trigger's centre, measured live (the composer moves with the dock).
  assert.match(wire, /const dustPoint = \(\) => rectCenter\(btn\);/);
  assert.match(wire, /surfaceIn\(menu, dustPoint\(\), \{ ms: SURFACE_MENU_IN_MS \}\)/);
  assert.match(wire, /surfaceOut\(menu, dustPoint\(\), \{ ms: SURFACE_MENU_OUT_MS \}\)/);
  // Only a real change flies: re-closing a closed menu must not raise a second cloud.
  assert.match(wire, /const changed = on === menu\.hidden;/);
  // `hidden` is display:none, so the box must still be up when the flight is played,
  // and must go straight after — the cloud is a copy on <body> with its own life.
  const open = wire.indexOf('menu.hidden = false;');
  const out = wire.indexOf('surfaceOut(menu, dustPoint()');
  const hide = wire.indexOf('if (!on) menu.hidden = true;');
  assert.ok(open < wire.indexOf('surfaceIn(menu, dustPoint()'), 'unhidden before it forms');
  assert.ok(out < hide, 'measured while still up, hidden immediately after');
});

// The gear that raises the settings window sits INSIDE that menu, which closes as it is clicked,
// so the flight is anchored on the "…" — measuring the gear by then gives 0x0.
test('the assistant settings window flies to the "…", not to the gear that vanished', () => {
  assert.match(llmSettingsJs, /originEl: \(\) => visibleChatMoreBtn\(\)/);
  const pick = chatViewJs.slice(chatViewJs.indexOf('export const visibleChatMoreBtn'));
  // The composer the user last opened wins, then whichever surface is actually up.
  assert.match(pick, /shownBtn\(lastMoreBtn\)/);
  assert.match(pick, /shownBtn\(doc\.getElementById\('chat-more-btn'\)\)/);
  assert.match(pick, /shownBtn\(doc\.getElementById\('ctx-assist-more-btn'\)\)/);
  // A hidden trigger measures 0x0 and must NOT be offered — null is the honest answer,
  // and base.js then falls from above (its `onScreen` guard).
  assert.match(chatViewJs, /const shownBtn = \(el\) => \{[\s\S]{0,200}r\.width > 0 && r\.height > 0 \? el : null;/);
  assert.match(pick, /\|\| null;/);
  // The shell resolves it at BOTH edges, because close() re-measures before it plays.
  assert.match(baseJs, /const defaultOrigin = \(\) => \(originFor \? originFor\(\) : openBtn\);/);
  assert.match(baseJs, /: \(from === null \? null : defaultOrigin\(\)\);/);
});

test('a FOLD is the exception: it leaves slower than it arrives', async () => {
  const { FOLD_DUST_OUT_MS } = await import('../js/ui/motion.js');
  assert.ok(FOLD_DUST_OUT_MS > SURFACE_OUT_MS,
    'a fold has no icon to shrink into — the fold IS the close, so it may not be brisk');
  const token = (name) => Number(/(\d+)ms/.exec(new RegExp(`--${name}:\\s*([^;]+);`).exec(animCss)[1])[1]);
  // The sand and the CSS fold must scale together, or one outlives the other.
  assert.equal((FOLD_DUST_OUT_MS / SURFACE_OUT_MS).toFixed(2),
               (token('fold-out-ms') / token('fold-ms')).toFixed(2));
});

test('a surface forms slower than it leaves — arriving is the half you watch', () => {
  assert.ok(SURFACE_IN_MS > SURFACE_OUT_MS, 'the gather is the slower half');
  assert.ok(SURFACE_IN_MS >= 560 && SURFACE_IN_MS <= 800, 'slow enough to read as sand gathering, brisk enough not to wait on');
  // tileNoise is the shared hash — the surface flight is the row's, not a second system.
  assert.equal(typeof tileNoise(1, 2), 'number');
  assert.equal(tileNoise(1, 2), tileNoise(1, 2));
});
