import { isTypingTarget } from '../../utils.js';
export function wireArrowPan(app) {
  // ── Arrow-key panning ──────────────────────────────────────
  // Plain arrows pan the viewport; multiple arrows pan diagonally, opposing pairs
  // cancel; Shift accelerates. Alt/Ctrl/Meta are reserved (e.g. Alt+ArrowUp = zoom).
  const arrowsHeld = new Set();
  let arrowPanRaf = null;
  // Whether R is currently held — the Alt+R+←/→ line-rotate chord.
  let rHeld = false;
  const ARROW_KEYS = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'];
  const arrowPanTick = () => {
    const vp = document.getElementById('canvas-viewport');
    if (!vp || arrowsHeld.size === 0) { arrowPanRaf = null; return; }
    const speed = arrowsHeld.has('Shift') ? 22 : 7;
    let dx = 0;
    let dy = 0;
    if (arrowsHeld.has('ArrowLeft'))  dx -= 1;
    if (arrowsHeld.has('ArrowRight')) dx += 1;
    if (arrowsHeld.has('ArrowUp'))    dy -= 1;
    if (arrowsHeld.has('ArrowDown'))  dy += 1;
    if (dx) vp.scrollLeft += dx * speed;
    if (dy) vp.scrollTop  += dy * speed;
    arrowPanRaf = requestAnimationFrame(arrowPanTick);
  };
  document.addEventListener('keydown', e => {
    if (isTypingTarget(e.target)) return;
    if (e.key === 'r' || e.key === 'R') rHeld = true;  // track the Alt+R+←/→ chord
    if (e.key === 'Shift') { arrowsHeld.add('Shift'); return; }
    if (!ARROW_KEYS.includes(e.key)) return;

    // Alt+R + ←/→ → rotate the selected line(s) (← CCW, → CW), 3°/press. Takes precedence
    // over pan/zoom so the chord always rotates when a line is selected.
    if (e.altKey && rHeld && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')
        && app.selectedIndices().length >= 1 && !app.compareReadOnly()) {
      e.preventDefault();
      app.rotateSelectedLine((e.key === 'ArrowLeft' ? -1 : 1) * (Math.PI / 60));
      return;
    }
    // Don't steal other Alt/Ctrl/Meta+Arrow combos used by other shortcuts (e.g. zoom).
    if (e.ctrlKey || e.altKey || e.metaKey) return;

    // With a line selected, plain arrows NUDGE the selection (1px, Shift = 10px, in image
    // space); with nothing selected they fall through to panning the viewport. In a compare
    // (read-only) view, nudging is disabled — arrows always pan.
    if (app.selectedIndices().length >= 1 && !app.compareReadOnly()) {
      e.preventDefault();
      const step = e.shiftKey ? 10 : 1;
      let dx = 0, dy = 0;
      if (e.key === 'ArrowLeft') dx = -step;
      else if (e.key === 'ArrowRight') dx = step;
      else if (e.key === 'ArrowUp') dy = -step;
      else if (e.key === 'ArrowDown') dy = step;
      app.nudgeSelected(dx, dy);
      return;
    }

    e.preventDefault();
    arrowsHeld.add(e.key);
    arrowPanRaf ??= requestAnimationFrame(arrowPanTick);
  });
  document.addEventListener('keyup', e => {
    if (e.key === 'r' || e.key === 'R') rHeld = false;
    if (e.key === 'Shift') arrowsHeld.delete('Shift');
    if (ARROW_KEYS.includes(e.key)) arrowsHeld.delete(e.key);
  });
  // Clear held keys if the window loses focus while arrows are held
  window.addEventListener('blur', () => { arrowsHeld.clear(); rHeld = false; });
}
