// A press held still on an element for holdMs, then `onHold()`; when that answers true the
// release's click is swallowed, so the element's own click never follows the hold.
// Browser twin: the hold inside js/ui/logo/stageTrigger.js wireLogoHold.

// px a held press may drift before it stops counting (browser ui/tip/popover.js PRESS_SLOP_PX).
export const PRESS_SLOP_PX = 10;
const RELEASES = Object.freeze(['pointerup', 'pointerleave', 'pointercancel']);

const isPlainPress = (e) => !(e.button || e.altKey || e.ctrlKey || e.metaKey || e.shiftKey);

export const wirePressHold = (wrap, { holdMs, onHold }) => {
  if (!wrap) return;
  let timer = null, from = null, fired = false;
  const cancel = () => { if (timer) clearTimeout(timer); timer = null; from = null; };
  const swallowClick = (e) => { e.preventDefault?.(); e.stopImmediatePropagation?.(); };
  wrap.addEventListener('pointerdown', (e) => {
    if (!isPlainPress(e)) return;
    fired = false;
    from = { x: e.clientX ?? 0, y: e.clientY ?? 0 };
    timer = setTimeout(() => {
      timer = null;
      if (!onHold()) return;
      fired = true;
      wrap.addEventListener('click', swallowClick, { capture: true, once: true });
    }, holdMs);
  });
  wrap.addEventListener('pointermove', (e) => {
    if (from && Math.hypot((e.clientX ?? 0) - from.x, (e.clientY ?? 0) - from.y) > PRESS_SLOP_PX) cancel();
  });
  for (const type of RELEASES) {
    wrap.addEventListener(type, () => {
      cancel();
      // A hold that fired leaves the swallow armed for exactly one click.
      if (!fired) wrap.removeEventListener('click', swallowClick, { capture: true });
    });
  }
};
