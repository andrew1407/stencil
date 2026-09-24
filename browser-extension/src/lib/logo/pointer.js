// A byte port of browser/js/ui/logo/pointer.js (tests/portParity.test.js): only this header and the
// import paths may differ.
let at = null;
const note = (e) => { at = { x: e.clientX, y: e.clientY }; };

export const trackPointer = (doc = globalThis.document) => {
  for (const type of ['pointermove', 'pointerover']) doc?.addEventListener?.(type, note, { capture: true, passive: true });
};

// null until the pointer has been seen over the page.
export const lastPointer = () => (at ? { ...at } : null);
