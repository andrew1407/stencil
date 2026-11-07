// The hover magnifier over a chat thumbnail: one preview element app-wide, on <body>,
// since the panel clips its own overflow (and the extension popup is 400px wide).
// Extension twin: browser-extension/src/lib/chat/ui.js wireThumbPreview.
import { surfaceIn, surfaceOut } from '../../motion/surface/surfaces.js';
import { TIP_DUST_IN_MS, TIP_DUST_OUT_MS, rectCenter, tipFallbackIn, tipCopyAway } from '../../motion/control/tips.js';
import { menuAway, pointBox } from '../../motion/genie/window.js';

let thumbPreviewEl = null;
const previews = new WeakMap();   // box → { place(), home(), shown }
// It comes and goes as a tip does, out of and back into its thumbnail: the dust, Sliding's pour, Transition's
// grow; it only faded in and vanished (user report). The leave plays before the box goes: each takes its own copy.
export const hideThumbPreview = () => {
  const box = thumbPreviewEl;
  thumbPreviewEl = null;
  if (!box) return;
  const state = previews.get(box);
  const home = state?.home();
  if (home && state.shown)
    surfaceOut(box, home, { ms: TIP_DUST_OUT_MS }) || tipCopyAway(box) || menuAway(box, pointBox(home));
  box.remove();
};
// Switching window never fires mouseleave, so hide on blur. Alt held doubles the glance
// (chat-thumb-preview-xl) and re-places the open preview.
if (typeof window !== 'undefined') {
  window.addEventListener('blur', hideThumbPreview);
  const altPreview = (on) => {
    if (!thumbPreviewEl) return;
    thumbPreviewEl.classList.toggle('chat-thumb-preview-xl', on);
    previews.get(thumbPreviewEl)?.place();
  };
  window.addEventListener('keydown', (e) => { if (e.key === 'Alt') altPreview(true); });
  window.addEventListener('keyup', (e) => { if (e.key === 'Alt') altPreview(false); });
  window.addEventListener('scroll', hideThumbPreview, { capture: true });   // once: a scroll anywhere hides the one preview
}
export const wireThumbPreview = (img, caption = '') => {
  const place = (box) => {
    // Measured from the corner: a fixed auto-width box only gets the room right of its `left`.
    box.style.left = '0px';
    box.style.top = '0px';
    const r = img.getBoundingClientRect();
    const b = box.getBoundingClientRect();
    const left = Math.max(8, Math.min(r.left, window.innerWidth - b.width - 8));
    const above = r.top - b.height - 10;
    const top = above >= 8 ? above : Math.min(r.bottom + 10, window.innerHeight - b.height - 8);
    box.style.left = `${Math.round(left)}px`;
    box.style.top = `${Math.round(Math.max(8, top))}px`;
    box.style.transformOrigin = above >= 8 ? 'center bottom' : 'center top';   // the thumbnail's side
    const state = previews.get(box);
    const isAppearing = !state.shown;   // a re-place (Alt) moves it, never replays its entrance
    state.shown = true;
    box.classList.add('chat-thumb-preview-in');
    const home = rectCenter(img);
    if (isAppearing && home && !surfaceIn(box, home, { ms: TIP_DUST_IN_MS })) tipFallbackIn(box, home);
  };
  img.addEventListener('mouseenter', (e) => {
    hideThumbPreview();
    const box = document.createElement('div');
    box.className = 'chat-thumb-preview';
    if (e.altKey) box.classList.add('chat-thumb-preview-xl');
    const big = document.createElement('img');
    big.src = img.src;
    big.alt = '';
    box.appendChild(big);
    if (caption) {
      const cap = document.createElement('span');
      cap.className = 'chat-thumb-preview-cap';
      cap.textContent = caption;
      box.appendChild(cap);
    }
    document.body.appendChild(box);
    thumbPreviewEl = box;
    previews.set(box, { place: () => place(box), home: () => rectCenter(img), shown: false });
    // Measure only once the picture has its size.
    if (big.complete) place(box); else big.addEventListener('load', () => place(box), { once: true });
  });
  img.addEventListener('mouseleave', hideThumbPreview);
  img.addEventListener('click', hideThumbPreview);
};
