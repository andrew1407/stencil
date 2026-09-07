// Control tooltip: shows a control's `data-tip` (the composed text) or `data-title` (the
// authored base) on hover after a short delay. The app authors NO native `title` anywhere —
// the browser's own popup takes ~1s, never shows on a disabled control, and would double
// this one — so this is the only tooltip. (tooltip.js is the canvas readout.)
//
// The text is not printed flat: tipContent.js parses the composed title into the desktop
// app's tooltip shape — a heading with keycaps for its shortcut, term/description rows,
// bullets, and the muted disabled-reason note — and this only positions and shows it.

import { renderTip, parseTip } from './tipContent.js';
import { surfaceIn, surfaceOut, settleSurface, rectCenter,
         TIP_DUST_IN_MS, TIP_DUST_OUT_MS, TIP_SHOW_DELAY_MS } from './motion.js';

// Long enough that flicking the cursor across the whole bar shows nothing, short enough
// that pausing on ONE control reads as responsive (shared home: motion.js; desktop
// SnappyTooltipStyle keeps the same wake-up delay).
const SHOW_DELAY_MS = TIP_SHOW_DELAY_MS;
const SHAKE_CLASS = 'key-shake';
const SHAKE_MS = 340;       // the keycap nudge — one shot, matched to the CSS keyframes
// The tooltip is sand too (motion.js surfaceIn/surfaceOut), on the shared short tip
// clock — a toolbar sweep re-points it many times a second.
const TIP_IN_MS = TIP_DUST_IN_MS;
const TIP_OUT_MS = TIP_DUST_OUT_MS;
// While the motes are still gathering the box may not move: the cloud was measured
// where the tip was placed, and a tip that tracked the cursor mid-flight would leave
// its own sand behind. It picks the cursor up again the moment it lands.
let placeHeldUntil = 0;
// The keycap nudge waits for the motes to land: a shake played while the tip is still
// assembling is a move nobody can see, and the point of it is to be seen. Held here so
// a tooltip dismissed or re-pointed mid-flight never shakes the caps of a tip that is
// already gone.
let shakeTimer = null;
const now = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());
// Where a tooltip's dust comes from and goes back to: the centre of the control it
// describes (rectCenter — a detached owner has no point, and the dust declines). But a
// control STRETCHED across its row has its content at one end and its centre in empty
// space, so past this distance the pointer is the better origin. Desktop twin:
// appTooltip.hpp's `stretched` test.
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

// ── Matching a keystroke to a keycap ────────────────────────────────────────
// A tooltip's shortcut is written to be READ ("⇧⌘S", "Alt+0", "Shift+click"), so both
// sides are reduced to the same shape — a set of modifiers plus one key — before they
// are compared. Pure, so the matching is unit-tested without a keyboard.
const MOD_OF = {
  '⌃': 'Ctrl', '⌥': 'Alt', '⇧': 'Shift', '⌘': 'Meta',
  CTRL: 'Ctrl', CONTROL: 'Ctrl', ALT: 'Alt', OPTION: 'Alt', SHIFT: 'Shift',
  META: 'Meta', CMD: 'Meta', COMMAND: 'Meta', WIN: 'Meta',
};
const KEY_OF = { ESC: 'ESCAPE', DEL: 'DELETE', RETURN: 'ENTER', ' ': 'SPACE' };
const keyName = (k) => {
  const u = String(k == null ? '' : k).toUpperCase();
  return KEY_OF[u] || u;
};

export const parseCombo = (combo) => {
  const s = String(combo == null ? '' : combo).trim();
  // Apple's glyph form carries no joiner ("⇧⌘S"); every other form is "+"-separated.
  const lead = s.match(/^[⌃⌥⇧⌘]+/);
  const tokens = lead ? [...lead[0], s.slice(lead[0].length)] : s.split('+');
  const mods = new Set();
  let key = '';
  for (const raw of tokens) {
    const t = raw.trim();
    if (!t) continue;
    const mod = MOD_OF[t] || MOD_OF[t.toUpperCase()];
    if (mod) mods.add(mod);
    else key = keyName(t);
  }
  return { mods, key };
};

// What was actually pressed. Both `key` and the PHYSICAL `code` are kept: on a Mac
// Alt+A reports key "å", so the code is the only side that still says "A".
export const eventCombo = (e) => {
  const mods = new Set();
  if (e.ctrlKey) mods.add('Ctrl');
  if (e.altKey) mods.add('Alt');
  if (e.shiftKey) mods.add('Shift');
  if (e.metaKey) mods.add('Meta');
  const code = String(e.code || '').replace(/^(?:Key|Digit|Numpad)/, '');
  return { mods, keys: [keyName(e.key), code ? keyName(code) : ''].filter(Boolean) };
};

export const comboMatchesEvent = (combo, e) => {
  const want = parseCombo(combo);
  // Nothing a keystroke can be: a modifier-only combo leaves no key at all, and a
  // gesture the caps spell out ("Alt+click", "Shift+left-drag") leaves a word instead.
  if (!want.key) return false;
  const got = eventCombo(e);
  if (want.mods.size !== got.mods.size) return false;
  for (const m of want.mods) if (!got.mods.has(m)) return false;
  return got.keys.includes(want.key);
};

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

// Prefer the live composed data-tip (carries the "(combo)" hint + "— reason" line); fall
// back to data-title, the authored base text. Never the native `title`: the app authors
// none, so the browser's own delayed popup can never double the custom one.
const textFor = (el) => (el.dataset && (el.dataset.tip || el.dataset.title)) || '';

const place = (e) => {
  if (!tip || !e) return;
  if (now() < placeHeldUntil) return;   // the motes are still on their way to this box
  const pad = 10;
  // offsetWidth/Height, NOT a client rect: the entry transform scales the box while it
  // plays, and a rect measured mid-flight would clamp against a tooltip 3% too small —
  // so one near the screen edge would settle a few pixels off it. The layout box is
  // what the box will occupy once it lands, and transforms never touch it.
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

// Every keycap the tooltip drew nudges once as the tip lands, so the eye goes straight
// to the shortcut — the answer to "is there a key for this?" arrives with the text. A
// tip with no shortcut has no caps and so does nothing. flashClass is restart-safe, so
// a sweep that re-points the one shared tooltip shakes the NEW control's caps from the
// top instead of stacking; the caps themselves are fresh nodes on every show anyway.
// The CSS neutralises the movement under reduced motion and keeps only the recolour.
const shakeKeys = (t) => {
  t.querySelectorAll('.tip-key').forEach(cap => flashClass(cap, SHAKE_CLASS, SHAKE_MS));
};

// A shortcut pressed while its own control's tooltip is up: shake the cap that spells
// it instead of blinking the tooltip away, and report the hit so the caller keeps the
// tooltip on screen. Every other key still dismisses it.
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
    // A control that owns its own hover popup (the toolbar's "?" badge and its
    // .hints-popup) opts out: a floating copy of the same text on top of the bubble is
    // just the tooltip said twice. Tested on the TARGET, so an ancestor's title can't
    // stand in for it either.
    if (e.target.closest && e.target.closest('[data-no-tooltip]')) { hide(); return; }
    // The owner can VANISH under the pointer — a toggle rewrites its face, a list row
    // re-renders, a modal closes — and a detached element never fires pointerout, so the
    // tooltip would hang there describing a control that is gone.
    if (curEl && curEl.isConnected === false) hide();
    const el = e.target.closest ? e.target.closest('[data-tip], [data-title]') : null;
    // Still inside the active target (e.g. moved onto its child icon) -> keep showing.
    // UNLESS that child carries its own text — a status dot inside a row label describes
    // ITSELF, and its tooltip used to be swallowed here (desktop parity).
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
