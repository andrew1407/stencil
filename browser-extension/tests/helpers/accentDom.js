// The fabricated page accentSandbox.js runs the pre-paint classic scripts against: an <html>
// carrying attributes/classes/custom properties, a <head>/<body>, and a recording canvas.

// Rich enough for BOTH creations accent.js does: the favicon <link> (bare property writes) and
// the swap-dust stage, a canvas whose 2d context records the colour, alpha and count of each fill.
export const makeElement = (tag, bodyChildren) => {
  const style = { setProperty: (k, v) => { style[k] = v; } };
  const children = [];
  const el = {
    tagName: tag.toUpperCase(),
    className: '',
    style,
    children,
    appendChild: (c) => children.push(c),
    remove: () => {
      el.removed = true;
      const i = bodyChildren.indexOf(el);
      if (i >= 0) bodyChildren.splice(i, 1);
    },
  };
  if (tag === 'canvas') {
    let arcs = 0;
    el.fills = [];
    const ctx = {
      fillStyle: '#000', globalAlpha: 1,
      scale() {}, clearRect() {}, beginPath() { arcs = 0; },
      moveTo() {}, arc() { arcs++; }, ellipse() { arcs++; }, lineTo() {}, closePath() { arcs++; },
      fill() { el.fills.push({ colour: ctx.fillStyle, alpha: ctx.globalAlpha, arcs }); },
    };
    el.getContext = () => ctx;
  }
  return el;
};

export const makePage = ({ controls, withViewTransitions, deferViewTransitions }) => {
  // ── A minimal <html> + <head> ──
  const attributes = new Map();
  // Only the wipe touches these two; a page without view transitions never reaches them.
  const props = new Map();
  const classes = new Set();
  const documentElement = {
    getAttribute: (k) => (attributes.has(k) ? attributes.get(k) : null),
    setAttribute: (k, v) => attributes.set(k, v),
    // data-accent-light is a bare presence flag (the on-accent ink switch).
    hasAttribute: (k) => attributes.has(k),
    removeAttribute: (k) => attributes.delete(k),
  };
  // Always present: apply()/setCustom()/previewAccent() read and clear inline --accent.
  documentElement.style = {
    setProperty: (k, v) => props.set(k, v),
    removeProperty: (k) => props.delete(k),
    getPropertyValue: (k) => (props.has(k) ? props.get(k) : ''),
  };
  if (withViewTransitions || deferViewTransitions) {
    documentElement.classList = { add: (c) => classes.add(c), remove: (c) => classes.delete(c) };
  }

  // Elements the fabricated page owns, in document order — what querySelectorAll answers.
  const elements = controls.map(({ id, rect, visible = true }) => ({
    id,
    getBoundingClientRect: () => rect,
    checkVisibility: () => visible,
  }));
  const headChildren = [];
  const bodyChildren = [];
  const pointerListeners = [];
  const document = {
    documentElement,
    head: { appendChild: (el) => headChildren.push(el) },
    body: { appendChild: (el) => bodyChildren.push(el) },
    createElement: (tag) => makeElement(tag, bodyChildren),
    // accent.js asks only for link[rel="icon"]; once it has created one, the next apply
    // must find that same element and update it in place rather than appending another.
    querySelector: (sel) =>
      sel === 'link[rel="icon"]'
        ? headChildren.find((el) => el.tagName === 'LINK' && el.rel === 'icon') || null
        : null,
    // The swap origin lookup: every element carrying the id, not just the first.
    querySelectorAll: (sel) => {
      const m = /^\[id="(.*)"\]$/.exec(sel);
      return m ? elements.filter((el) => el.id === m[1]) : [];
    },
    addEventListener: (type, fn) => { if (type === 'pointerdown') pointerListeners.push(fn); },
  };
  // The REAL API runs the callback a beat later, and code runs in that gap (a menu
  // closing over its own pick): `deferViewTransitions` queues them until runSwaps().
  const queuedSwaps = [];
  if (withViewTransitions || deferViewTransitions) {
    // `ready` resolves like the real API's: the wake (swap dust) spawns off it.
    document.startViewTransition = (cb) => {
      if (deferViewTransitions) queuedSwaps.push(cb); else cb();
      return { ready: Promise.resolve(), finished: Promise.resolve() };
    };
  }

  return { document, documentElement, props, headChildren, bodyChildren, pointerListeners, queuedSwaps };
};
