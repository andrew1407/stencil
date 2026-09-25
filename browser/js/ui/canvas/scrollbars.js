// The canvas viewport's overlay scrollbars (desktop parity: canvas/OverlayScrollArea.hpp).
// The native bars are hidden (layout/scrollbars.css): scrollbar-color is one colour for
// both bars and the webkit pseudo-elements are ignored once scrollbar-width is set.
import { motionReduced } from '../motion.js';
import { onWindowResize } from './frameSync.js';
import { SB_MIN_THUMB_PX, thumbMetrics } from '../control/thumbMetrics.js';

// The thumb's arithmetic stays part of this module's surface: the canvas suite reaches for it here.
export { SB_MIN_THUMB_PX, thumbMetrics };

const SB_SLOT_PX = 12;      // the strip each bar owns along the viewport edge
const SB_HIDE_MS = 900;     // idle before the bars fade (desktop: revealCanvasScrollbars)

export const wireCanvasScrollbars = (vp) => {
  if (!vp || vp.__canvasSb) return vp?.__canvasSb || null;
// On the body: pan.js sizes the viewport by summing its following siblings' heights.
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
  let held = 0;

  const hide = () => { for (const b of Object.values(bars)) b.bar.classList.remove('canvas-sb-on'); };
  const scheduleHide = () => {
    clearTimeout(hideTimer);
    hideTimer = null;
    if (held) return;
    hideTimer = setTimeout(hide, SB_HIDE_MS);
  };

// Off the viewport's on-screen box (position: fixed, so it follows fullscreen). A
// mid-flight viewport (the fullscreen flip) shows no bars.
  const geom = (axis) => (axis === 'y'
    ? { client: vp.clientHeight, scroll: vp.scrollHeight, offset: vp.scrollTop }
    : { client: vp.clientWidth, scroll: vp.scrollWidth, offset: vp.scrollLeft });
  const scrollTo = (axis, v) => { if (axis === 'y') vp.scrollTop = v; else vp.scrollLeft = v; };
  const alongOf = (axis, e) => (axis === 'y' ? e.clientY : e.clientX);

  const layout = () => {
    const flying = vp.classList.contains('flip-active');
// No picture, nothing to scroll: whatever size the canvas element still carries is not content.
    const empty = document.body.classList.contains('canvas-empty');
    const canY = !flying && !empty && vp.scrollHeight > vp.clientHeight;
    const canX = !flying && !empty && vp.scrollWidth > vp.clientWidth;
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
// On the frame the viewport shrinks (a panel drag) its children still measure at the old
// width and read as overflow, so the verdict waits for the layout to settle.
    const settled = (fn) => (typeof requestAnimationFrame === 'function'
      ? requestAnimationFrame(() => requestAnimationFrame(fn)) : fn());
    const ro = new ResizeObserver(() => settled(reveal));   // the viewport's box, and the picture's (a zoom)
    ro.observe(vp);
    const canvas = vp.querySelector('#canvas');
    if (canvas) ro.observe(canvas);
  }
  if (typeof MutationObserver !== 'undefined')
    new MutationObserver(layout).observe(vp, { attributes: true, attributeFilter: ['class'] });

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
