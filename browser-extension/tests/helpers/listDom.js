// A minimal list DOM + a manual clock for the filter-transition tests (src/lib/motion.js
// createFilterTransition). Node has no DOM and this repo adds no deps, so the rows carry
// exactly the surface the transition touches: children, dataset, classList, style,
// insertBefore/appendChild/remove and a height.

export const makeRow = (key, { attr = 'key', height = 40 } = {}) => {
  const classes = new Set();
  const row = {
    dataset: { [attr]: key },
    parent: null,
    classes,
    props: {},
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
    },
    style: { setProperty: (k, v) => { row.props[k] = v; } },
    getBoundingClientRect: () => ({ height }),
    remove: () => { row.parent?.removeChild(row); },
  };
  return row;
};

export const makeList = () => {
  const list = {
    children: [],
    appendChild(el) {
      list.removeChild(el);
      el.parent = list;
      list.children.push(el);
      return el;
    },
    insertBefore(el, ref) {
      list.removeChild(el);
      el.parent = list;
      const i = ref ? list.children.indexOf(ref) : -1;
      if (i < 0) list.children.push(el); else list.children.splice(i, 0, el);
      return el;
    },
    removeChild(el) {
      const i = list.children.indexOf(el);
      if (i >= 0) { list.children.splice(i, 1); el.parent = null; }
    },
    // The wholesale rebuild every one of these lists does (`innerHTML = ''`).
    wipe() { for (const el of [...list.children]) list.removeChild(el); },
    keys: (attr = 'key') => list.children.map((el) => el.dataset[attr]),
  };
  return list;
};

// One render exactly as the surfaces do it: snapshot, wipe, rebuild, let it play.
export const renderKeys = (list, tr, keys, { attr = 'key', ...opts } = {}) => {
  tr.begin();
  list.wipe();
  for (const k of keys) list.appendChild(makeRow(k, { attr }));
  return tr.end(opts);
};

// A minimal element stand-in for the class-toggling helpers (flashLanding, materialize, chatIn).
export const classEl = (cls = '') => {
  const classes = new Set(cls ? cls.split(' ') : []);
  return {
    offsetWidth: 0,
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
    },
    has: (c) => classes.has(c),
  };
};

// A hand-wound clock, so "after the animation" is a fact and not a sleep.
export const fakeTimers = () => {
  const pending = new Map();
  let now = 0;
  let seq = 0;
  return {
    setTimer: (fn, ms) => { pending.set(++seq, { fn, at: now + ms }); return seq; },
    clearTimer: (id) => { pending.delete(id); },
    advance(ms) {
      now += ms;
      for (const [id, t] of [...pending]) if (t.at <= now) { pending.delete(id); t.fn(); }
    },
    get pendingCount() { return pending.size; },
  };
};
