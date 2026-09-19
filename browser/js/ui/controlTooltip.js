// Control tooltip: shows a control's `data-tip` or `data-title` on hover after a short delay.
// The app authors NO native `title` — the browser's own popup is slow, skips disabled controls
// and would double this one — so this is the only tooltip. tooltip.js is the canvas readout.
// tipContent.js parses the composed title into the desktop app's tooltip shape; this only
// positions and shows it.

import { renderTip, parseTip } from './tipContent.js';
import { surfaceIn, surfaceOut, settleSurface, rectCenter,
         TIP_DUST_IN_MS, TIP_DUST_OUT_MS, TIP_SHOW_DELAY_MS } from './motion.js';
import { comboMatchesEvent, eventCombo, parseCombo } from './comboMatch.js';
export { parseCombo, eventCombo, comboMatchesEvent };

// Long enough that flicking across the bar shows nothing, short enough that pausing on ONE
// control reads as responsive (shared home: motion.js; desktop SnappyTooltipStyle matches).
const SHOW_DELAY_MS = TIP_SHOW_DELAY_MS;
const SHAKE_CLASS = 'key-shake';
const SHAKE_MS = 340;       // the keycap nudge — one shot, matched to the CSS keyframes
// The tooltip is sand too (motion.js surfaceIn/surfaceOut), on the shared short tip
// clock — a toolbar sweep re-points it many times a second.
const TIP_IN_MS = TIP_DUST_IN_MS;
const TIP_OUT_MS = TIP_DUST_OUT_MS;
// While the motes gather the box may not move: the cloud was measured where the tip was placed,
// so a tip tracking the cursor mid-flight would leave its own sand behind.
let placeHeldUntil = 0;
// The keycap nudge waits for the motes to land — a shake played mid-assembly is a move nobody
// sees — and is held so a tip dismissed mid-flight never shakes caps that are already gone.
let shakeTimer = null;
const now = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());
// Dust comes from the control's centre (rectCenter); past this distance a control STRETCHED
// across its row has its centre in empty space, so the pointer is the better origin.
export const DUST_CURSOR_PX = 40;
// The choice itself, pure so it is testable without a pointer.
export const dustOrigin = (centre, cursor, maxPx = DUST_CURSOR_PX) => {
  if (!centre || !cursor) return centre;
  return Math.hypot(cursor.x - centre.x, cursor.y - centre.y) > maxPx ? cursor : centre;
};
const dustPoint = (el) =>
  dustOrigin(rectCenter(el),
             lastEvent ? { x: lastEvent.clientX, y: lastEvent.clientY } : null);

let tip = null;             // the floating element (created lazily)
let curEl = null;           // element whose tooltip is currently shown/pending
let curCombos = [];         // the shortcut(s) the live tooltip is showing, in render order
let showTimer = null;
let lastEvent = null;       // last pointer event, for positioning

// Restart-safe one-shot class: pressing the same shortcut twice must shake twice, and
// re-adding a class the element already carries replays nothing.
const flashClass = (el, cls, ms) => {
  clearTimeout(el.__flashTimer);
  el.classList.remove(cls);
  void el.offsetWidth;   // force a reflow so re-adding the class restarts the animation
  el.classList.add(cls);
  el.__flashTimer = setTimeout(() => el.classList.remove(cls), ms);
};

const ensureTip = () => {
  if (tip) return tip;
  tip = document.createElement('div');
  tip.id = 'app-tooltip';
  tip.setAttribute('role', 'tooltip');
  document.body.appendChild(tip);
  void tip.offsetWidth;   // flush the new node's style, else the first show can't transition
  return tip;
};

// Prefer the live composed data-tip (carries the "(combo)" hint + "— reason"), else data-title.
// Never the native `title`: the app authors none, so its delayed popup cannot double this one.
const textFor = (el) => (el.dataset && (el.dataset.tip || el.dataset.title)) || '';

const place = (e) => {
  if (!tip || !e) return;
  if (now() < placeHeldUntil) return;   // the motes are still on their way to this box
  const pad = 10;
  // offsetWidth/Height, NOT a client rect: the entry transform scales the box while it plays, so a
  // rect measured mid-flight would clamp against a tooltip 3% too small. Transforms miss layout.
  const r = { width: tip.offsetWidth, height: tip.offsetHeight };
  let x = e.clientX + 14;
  let y = e.clientY + 18;
  if (x + r.width + pad > window.innerWidth) x = window.innerWidth - r.width - pad;
  if (y + r.height + pad > window.innerHeight) y = e.clientY - r.height - 12;
  if (x < pad) x = pad;
  if (y < pad) y = pad;
  tip.style.left = `${x}px`;
  tip.style.top = `${y}px`;
};

const hide = () => {
  clearTimeout(showTimer);
  clearTimeout(shakeTimer);
  showTimer = shakeTimer = null;
  curCombos = [];
  const owner = curEl;
  curEl = null;
  if (!tip) return;
  // It comes apart into its control. The class goes NOW either way: the cloud owns its
  // own lifetime, and the end state must never depend on the animation.
  if (tip.classList.contains('visible')) surfaceOut(tip, dustPoint(owner), { ms: TIP_OUT_MS });
  else settleSurface(tip);
  placeHeldUntil = 0;
  tip.classList.remove('visible');
};

const reveal = (el) => {
  const txt = textFor(el);
  if (!txt) return;
  const html = renderTip(txt);
  if (!html) return;
  const t = ensureTip();
  t.innerHTML = html;         // renderTip escapes every value it interpolates
  // The same parse renderTip ran, kept so a keystroke can be matched against the caps
  // it drew. Order matches the .tip-combo spans in the markup one for one.
  curCombos = parseTip(txt).keys;
  t.classList.add('visible');
  placeHeldUntil = 0;
  place(lastEvent);
  // Placed first, so the motes stream at the box the tip will actually occupy.
  // (surfaceIn settles the tip itself when there is no point to fly from.)
  const dusted = surfaceIn(t, dustPoint(el), { ms: TIP_IN_MS });
  if (dusted) placeHeldUntil = now() + TIP_IN_MS;
  // …and the caps nudge once it has ARRIVED — never while it is still sand.
  clearTimeout(shakeTimer);
  if (dusted) shakeTimer = setTimeout(() => { shakeTimer = null; shakeKeys(t); }, TIP_IN_MS);
  else shakeKeys(t);
};

// Every keycap nudges once as the tip lands, so the eye goes to the shortcut. flashClass is
// restart-safe, so re-pointing the one shared tooltip shakes the NEW caps instead of stacking.
const shakeKeys = (t) => {
  t.querySelectorAll('.tip-key').forEach(cap => flashClass(cap, SHAKE_CLASS, SHAKE_MS));
};

// A shortcut pressed while its own control's tooltip is up shakes the cap that spells it and
// reports the hit, so the caller keeps the tooltip up. Every other key still dismisses it.
const shakeMatchingKeys = (e) => {
  if (!tip || !tip.classList.contains('visible') || curCombos.length === 0) return false;
  const spans = tip.querySelectorAll('.tip-combo');
  let hit = false;
  curCombos.forEach((combo, i) => {
    if (!spans[i] || !comboMatchesEvent(combo, e)) return;
    hit = true;
    spans[i].querySelectorAll('.tip-key').forEach(cap => flashClass(cap, SHAKE_CLASS, SHAKE_MS));
  });
  return hit;
};

// Drop the tip now, whatever the hover/focus state — for a caller opening its own
// popup over the same control (the tip's 100003 tier would bury it).
export const dismissTip = () => hide();

export const initTooltips = () => {
  if (typeof document === 'undefined') return;
  document.addEventListener('pointerover', (e) => {
    lastEvent = e;
    // A control owning its own hover popup (the "?" badge and .hints-popup) opts out — a floating
    // copy of the same text is the tooltip said twice. Tested on the TARGET, not an ancestor.
    if (e.target.closest && e.target.closest('[data-no-tooltip]')) { hide(); return; }
    // The owner can VANISH under the pointer, and a detached element never fires pointerout — so
    // the tooltip would hang there describing a control that is gone.
    if (curEl && curEl.isConnected === false) hide();
    const el = e.target.closest ? e.target.closest('[data-tip], [data-title]') : null;
    // Still inside the active target (moved onto its child icon) -> keep showing, UNLESS that child
    // carries its own text: a status dot inside a row label describes ITSELF (desktop parity).
    if (curEl && curEl.contains(e.target) && (!el || el === curEl)) return;
    if (!el || el === curEl) return;
    hide();
    curEl = el;
    showTimer = setTimeout(() => reveal(el), SHOW_DELAY_MS);
  });
  document.addEventListener('pointermove', (e) => {
    lastEvent = e;
    // Same guard as pointerover: a sweep can produce moves with no over in between.
    if (curEl && curEl.isConnected === false) { hide(); return; }
    if (tip && tip.classList.contains('visible')) place(e);
  });
  document.addEventListener('pointerout', (e) => {
    if (!curEl) return;
    // Hide only when the pointer truly leaves the active element (not onto a descendant).
    if (!curEl.contains(e.relatedTarget)) hide();
  });
  // Never let a tooltip get stuck: drop it on any scroll / click / key / blur.
  document.addEventListener('scroll', hide, true);
  document.addEventListener('pointerdown', hide, true);
  document.addEventListener('keydown', (e) => {
    // Escape is the app's universal "get out" and always dismisses, even when a cap
    // spells it. Anything else that matches the live tooltip's shortcut shakes it.
    if (e.key !== 'Escape' && shakeMatchingKeys(e)) return;
    hide();
  }, true);
  window.addEventListener('blur', hide);
};
