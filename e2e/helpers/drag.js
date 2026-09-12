// Drag gestures shared by the browser drag specs. A REAL finger goes through the
// browser's input pipeline (CDP), never `dispatchEvent`: synthetic PointerEvents never
// reach the compositor, so they can't show whether the browser steals the gesture for
// scrolling — which is exactly how a reorder no finger could complete once passed here.

// { down, move, up, glide } over Input.dispatchTouchEvent, one touch point.
export async function finger(page) {
  const cdp = await page.context().newCDPSession(page);
  const send = (type, x, y) => cdp.send('Input.dispatchTouchEvent', {
    type, touchPoints: type === 'touchEnd' ? [] : [{ x, y, id: 1 }],
  });
  return {
    down: (x, y) => send('touchStart', x, y),
    move: (x, y) => send('touchMove', x, y),
    up: (x, y) => send('touchEnd', x, y),
    // Glide in steps, as a finger does — one jump can be mistaken for a flick.
    async glide(x, y, tx, ty, steps = 6) {
      for (let i = 1; i <= steps; i++) {
        await send('touchMove', x + ((tx - x) * i) / steps, y + ((ty - y) * i) / steps);
        await page.waitForTimeout(30);
      }
    },
  };
}

// Record what gets handed to the native drag image, to prove we suppress it.
export const spyOnDragImage = (page) => page.addInitScript(() => {
  window.__dragImages = [];
  const orig = DataTransfer.prototype.setDragImage;
  DataTransfer.prototype.setDragImage = function (img, x, y) {
    window.__dragImages.push({ tag: img && img.tagName, w: img && img.width, h: img && img.height, x, y });
    return orig.apply(this, arguments);
  };
});

// The ghost's box, and the source row's measured at the SAME instant — row heights settle
// a little after first paint, so a pre-drag measurement would drift against a mid-drag ghost.
export const ghostBox = (page) => page.evaluate(() => {
  const g = document.querySelector('[data-drag-ghost]');
  if (!g) return null;
  const b = g.getBoundingClientRect();
  const src = document.querySelector('.project-row.project-dragging');
  const s = src && src.getBoundingClientRect();
  return {
    left: b.left, top: b.top, width: b.width, height: b.height,
    row: s ? { width: s.width, height: s.height } : null,
  };
});
