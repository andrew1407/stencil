// Window-level pointer tracking + the composer size strip, shared by every chat drag gesture.
export const trackPointer = (onMove, onUp) => {
  const up = (ev) => {
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', up);
    window.removeEventListener('pointercancel', up);
    onUp(ev);
  };
  window.addEventListener('pointermove', onMove);
  window.addEventListener('pointerup', up);
  window.addEventListener('pointercancel', up);
};

// The strip above a composer resizes the textarea (the native grip is off — only the top
// edge can move). `onDrag` lets the flyout re-place itself; `hold` keeps the gesture alive.
export const wireInputSizer = (sizer, input, { host, onDrag, hold } = {}) => {
  sizer.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    const startY = e.clientY;
    const startH = input.getBoundingClientRect().height;
    try { sizer.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
    sizer.classList.add('dragging');
    host?.classList.add('chat-gesturing');
    hold?.(true);
    trackPointer((ev) => {
      input.style.height = Math.round(startH + (startY - ev.clientY)) + 'px';
      onDrag?.();
    }, () => {
      sizer.classList.remove('dragging');
      host?.classList.remove('chat-gesturing');
      hold?.(false);
      onDrag?.();
    });
  });
};
