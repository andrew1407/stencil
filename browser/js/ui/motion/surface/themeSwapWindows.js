// The theme swap per window, as the desktop runs one overlay per dialog (ThemeSwapOverlay::captureWindows):
// each open window, menu and list takes a view-transition name of its own and its own reveal, from the
// control used inside it, else the page's control where that lies over it, else its own centre.
// Extension twin: lib/dust/swapWindows.js.
import { swapEdgePolygon } from './themeSwap.js';
import { SWAP_GENIE_KEYFRAMES, WIPE_CLIP_FROM, WIPE_CLIP_TO, swapGenieKeyframes } from './themeSwapShape.js';

// What counts as an open window over the page.
export const SWAP_WINDOWS = [
  '#settings-modal-overlay.modal-open #settings-modal', '.app-modal-overlay.modal-open .app-modal',
  '.accent-dd-menu.dd-portal:not([hidden])', '#ctx-menu.ctx-open', 'stencil-chat-panel.chat-open.chat-dock-float',
].join(', ');
const NAME = 'swap-win-';
// Smaller than this is no window worth its own reveal (px).
const MIN_PX = 8;

// The reveal's origin in the window's own box.
const originIn = (el, r, page, doc) => {
  const focus = doc.activeElement;
  const f = focus && focus !== el && el.contains(focus) ? focus.getBoundingClientRect() : null;
  if (f?.width) return { x: f.left + f.width / 2 - r.left, y: f.top + f.height / 2 - r.top };
  if (page && page.x >= r.left && page.x <= r.right && page.y >= r.top && page.y <= r.bottom)
    return { x: page.x - r.left, y: page.y - r.top };
  return { x: r.width / 2, y: r.height / 2 };
};

// Names every open window for the view transition and returns the CSS that plays each one's reveal
// (`mode` the interface animation, `ms` its length, `ease` the ring and wipe's curve), and `release`,
// which drops the names once the transition is over. `page` is the page's own origin, in px; `selector`
// and `doc` are the page's own unless a test hands it another; `fromTop` is an accent change's pour.
export function nameSwapWindows(page, mode, ms, ease, selector = SWAP_WINDOWS, doc = globalThis.document, fromTop = false) {
  if (typeof doc?.querySelectorAll !== 'function') return { css: '', release: () => {} };
  const named = [];
  const css = [];
  for (const el of doc.querySelectorAll(selector)) {
    const r = el.getBoundingClientRect?.();
    if (!(r?.width >= MIN_PX && r.height >= MIN_PX) || !el.style) continue;
    const name = `${NAME}${named.length}`, frames = `${name}-reveal`;
    const o = originIn(el, r, page, doc);
    el.style.viewTransitionName = name;
    named.push(el);
    let anim = `${frames} ${ms}ms ${ease} both`;
    if (mode === 'slide') {
      css.push(swapGenieKeyframes(o.x, o.y, r.width, r.height, fromTop).replace(SWAP_GENIE_KEYFRAMES, frames));
      anim = `${frames} ${ms}ms linear both`;
    } else {
      const wipe = mode === 'transition';
      const from = wipe ? WIPE_CLIP_FROM : swapEdgePolygon(o.x, o.y, r.width, r.height, 0);
      const to = wipe ? WIPE_CLIP_TO : swapEdgePolygon(o.x, o.y, r.width, r.height, 1);
      css.push(`@keyframes ${frames} { from { clip-path: ${from}; } to { clip-path: ${to}; } }`);
    }
    // Its old picture holds under the new one, which its own clip grows over it.
    css.push(`::view-transition-group(${name}) { animation: none; }`,
             `::view-transition-old(${name}) { animation: none; mix-blend-mode: normal; }`,
             `::view-transition-new(${name}) { animation: ${anim}; mix-blend-mode: normal; }`);
  }
  return { css: css.join('\n'), release: () => { for (const el of named) el.style.viewTransitionName = ''; } };
}
