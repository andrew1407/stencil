// Where the pointer last stood over the page, so a show opened by a word or a call chases the
// real cursor from its first frame, moved or not. Desktop twin: QCursor::pos() in LogoStage.cpp.
let at = null;
const note = (e) => { at = { x: e.clientX, y: e.clientY }; };

export const trackPointer = (doc = globalThis.document) => {
  for (const type of ['pointermove', 'pointerover']) doc?.addEventListener?.(type, note, { capture: true, passive: true });
};

// null until the pointer has been seen over the page.
export const lastPointer = () => (at ? { ...at } : null);
