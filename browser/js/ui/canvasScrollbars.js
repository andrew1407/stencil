// The canvas viewport's own overlay scrollbars (desktop parity: canvas/overlayScrollArea.hpp).
// The browser's native bars cannot do what the desktop's painted thumbs do — the standard
// scrollbar-color property is ONE colour for both bars, so hovering one coloured the
// other too, and the webkit pseudo-elements are ignored once scrollbar-width is set
// (user reports). So the native bars are hidden (layout.css) and these two elements
// float over the viewport's edges instead: revealed by a scroll or a zoom, faded after
// an idle spell, each thumb thin at rest and swelling to the accent only under its OWN
// pointer, draggable, with a page-step on its track.
import { motionReduced } from './motion.js';
import { onWindowResize } from './frameSync.js';

const SB_SLOT_PX = 12;      // the strip each bar owns along the viewport edge
export const SB_MIN_THUMB_PX = 28;
const SB_HIDE_MS = 900;     // idle before the bars fade (desktop: revealCanvasScrollbars)

// Pure: a thumb's length and offset along `track` px for one axis — `client` visible of
// `scroll` total, scrolled to `offset`. Null when nothing overflows.
export const thumbMetrics = (client, scroll, offset, track) => {
  if (!(scroll > client) || !(track > 0)) return null;
  const len = Math.min(track, Math.max(SB_MIN_THUMB_PX, Math.round(track * client / scroll)));
  const range = scroll - client;
  const at = Math.min(Math.max(offset, 0), range);
  return { len, pos: Math.round((track - len) * at / range) };
};

export const wireCanvasScrollbars = (vp) => {
  if (!vp || vp.__canvasSb) return vp?.__canvasSb || null;
  // On the BODY, not beside the viewport: zoomPan.js sizes the viewport by summing the
  // heights of its following siblings, and two bars there (fixed-positioned, but still
  // siblings with a box) shrank the frame to the picture on every zoom (user report).
  const host = document.body;
  const make = (axis) => {
    const bar = document.createElement('div');
    bar.className = `canvas-sb canvas-sb-${axis}`;
    bar.dataset.axis = axis;
    bar.setAttribute('aria-hidden', 'true');
    bar.hidden = true;
    const thumb = document.createElement('div');
    thumb.className = 'canvas-sb-thumb';
    bar.appendChild(thumb);
    host.appendChild(bar);
    return { bar, thumb, axis };
  };
  const bars = { y: make('y'), x: make('x') };
  let hideTimer = null;
  let held = 0;   // bars under the pointer or mid-drag: never fade out from under it

  const hide = () => { for (const b of Object.values(bars)) b.bar.classList.remove('canvas-sb-on'); };
  const scheduleHide = () => {
    clearTimeout(hideTimer);
    hideTimer = null;
    if (held) return;
    hideTimer = setTimeout(hide, SB_HIDE_MS);
  };

  // Geometry off the viewport's on-screen box (position: fixed, so it also follows the
  // viewport when fullscreen pins it to the window). A mid-flight viewport (the
  // fullscreen flip scales it) shows no bars at all — they belong to the settled state.
  // One axis's live geometry and its two writes, so both bars share every formula
  // instead of spelling out a `vertical ? … : …` at each step below.
  const geom = (axis) => (axis === 'y'
    ? { client: vp.clientHeight, scroll: vp.scrollHeight, offset: vp.scrollTop }
    : { client: vp.clientWidth, scroll: vp.scrollWidth, offset: vp.scrollLeft });
  const scrollTo = (axis, v) => { if (axis === 'y') vp.scrollTop = v; else vp.scrollLeft = v; };
  const alongOf = (axis, e) => (axis === 'y' ? e.clientY : e.clientX);

  const layout = () => {
    const flying = vp.classList.contains('flip-active');
    const canY = !flying && vp.scrollHeight > vp.clientHeight;
    const canX = !flying && vp.scrollWidth > vp.clientWidth;
    const r = vp.getBoundingClientRect();
    const left = r.left + vp.clientLeft;
    const top = r.top + vp.clientTop;
    bars.y.bar.hidden = !canY;
    bars.x.bar.hidden = !canX;
    if (canY) {
      const track = vp.clientHeight - (canX ? SB_SLOT_PX : 0);
      Object.assign(bars.y.bar.style, {
        left: `${left + vp.clientWidth - SB_SLOT_PX}px`, top: `${top}px`,
        width: `${SB_SLOT_PX}px`, height: `${track}px`,
      });
      const { client, scroll, offset } = geom('y');
      const m = thumbMetrics(client, scroll, offset, track);
      if (m) Object.assign(bars.y.thumb.style, { top: `${m.pos}px`, height: `${m.len}px` });
    }
    if (canX) {
      const track = vp.clientWidth - (canY ? SB_SLOT_PX : 0);
      Object.assign(bars.x.bar.style, {
        left: `${left}px`, top: `${top + vp.clientHeight - SB_SLOT_PX}px`,
        width: `${track}px`, height: `${SB_SLOT_PX}px`,
      });
      const { client, scroll, offset } = geom('x');
      const m = thumbMetrics(client, scroll, offset, track);
      if (m) Object.assign(bars.x.thumb.style, { left: `${m.pos}px`, width: `${m.len}px` });
    }
    if (flying) hide();
    return { canY, canX };
  };
  const reveal = () => {
    const { canY, canX } = layout();
    const still = motionReduced();
    for (const b of Object.values(bars)) {
      b.bar.classList.toggle('canvas-sb-nomotion', still);
      if (!b.bar.hidden) b.bar.classList.add('canvas-sb-on');
    }
    if (canY || canX) scheduleHide();
  };

  vp.addEventListener('scroll', reveal, { passive: true });
  onWindowResize(layout);
  window.addEventListener('scroll', layout, { capture: true, passive: true });
  if (typeof ResizeObserver !== 'undefined') {
    const ro = new ResizeObserver(reveal);   // the viewport's box, and the picture's (a zoom)
    ro.observe(vp);
    const canvas = vp.querySelector('#canvas');
    if (canvas) ro.observe(canvas);
  }
  if (typeof MutationObserver !== 'undefined')
    new MutationObserver(layout).observe(vp, { attributes: true, attributeFilter: ['class'] });

  // Hover holds the bar up; a drag scrolls the viewport; a click on the track pages.
  for (const b of Object.values(bars)) {
    const axis = b.axis;
    const vertical = axis === 'y';
    b.bar.addEventListener('mouseenter', () => { held++; clearTimeout(hideTimer); hideTimer = null; });
    b.bar.addEventListener('mouseleave', () => { held = Math.max(0, held - 1); scheduleHide(); });
    b.bar.addEventListener('mousedown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      const track = vertical ? b.bar.clientHeight : b.bar.clientWidth;
      const { client, scroll, offset } = geom(axis);
      const m = thumbMetrics(client, scroll, offset, track);
      if (!m) return;
      const barRect = b.bar.getBoundingClientRect();
      const along = alongOf(axis, e) - (vertical ? barRect.top : barRect.left);
      if (along < m.pos || along > m.pos + m.len) {
        // Track click: a page toward the click, like a native bar.
        scrollTo(axis, offset + client * 0.9 * (along < m.pos ? -1 : 1));
        return;
      }
      const start = alongOf(axis, e);
      const perPx = (scroll - client) / Math.max(1, track - m.len);
      held++;
      b.bar.classList.add('canvas-sb-drag');
      const move = (ev) => scrollTo(axis, offset + (alongOf(axis, ev) - start) * perPx);
      const up = () => {
        document.removeEventListener('mousemove', move);
        document.removeEventListener('mouseup', up);
        b.bar.classList.remove('canvas-sb-drag');
        held = Math.max(0, held - 1);
        scheduleHide();
      };
      document.addEventListener('mousemove', move);
      document.addEventListener('mouseup', up);
    });
  }

  const api = { layout, reveal, bars };
  vp.__canvasSb = api;
  layout();
  return api;
};
