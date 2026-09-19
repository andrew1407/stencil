// ── open.actions → editor launch options (contract §8 translation) ──────────

// One §2 crop token to an absolute pixel coordinate on an axis of `length` px; mirrors
// core/units.js resolveAxisPx minus cm/in (no page metrics here). null = unsupported.
const resolveCropToken = (tok, length) => {
  const m = /^(-?)(\d+(?:\.\d+)?|\.\d+)(%|px|cm|in)?$/.exec(String(tok));
  if (!m) return null;
  const unit = m[3] || 'px';
  if (unit === 'cm' || unit === 'in') return null;
  const v = parseFloat(m[2]);
  const px = unit === '%' ? (v / 100) * length : v;
  return m[1] === '-' ? length - px : px;
};

// Validated open.actions → the `#stencil=` launch options (contract §8). crop needs the
// image dims; rotate has no slot in the payload, so it is dropped with a warning.
const STEPS = Object.freeze({
  crop: (st, a, width, height) => {
    if (!(width > 0 && height > 0)) {
      st.warnings.push('Skipped "crop" — the image dimensions are unknown');
      return;
    }
    const cur = st.rect || { x1: 0, x2: width, y1: 0, y2: height };
    const next = { ...cur };
    let bad = null;
    for (const [key, length] of [['x1', width], ['x2', width], ['y1', height], ['y2', height]]) {
      const tok = a.spec[key];
      if (tok == null) continue;
      const px = resolveCropToken(tok, length);
      if (px == null) { bad = tok; break; }
      next[key] = px;
    }
    if (bad != null) {
      st.warnings.push(`Skipped "crop" — cm/in crop units can't be resolved in the extension (token "${bad}"); crop in the editor instead`);
      return;
    }
    st.rect = next;
  },
  // The launch payload has no rotation slot — rotating happens in the editor.
  rotate: (st) => {
    st.warnings.push('Skipped "rotate" — the editor hand-off can\'t carry a rotation; rotate in the editor after it opens');
  },
  filter: (st, a) => {
    st.layout = st.layout || { lines: [] };
    st.layout.imageFilter = a.mode;
    if (a.mode === 'custom') st.layout.filterColor = a.tint;
    else delete st.layout.filterColor;
  },
  layout: (st, a) => {
    st.layout = st.layout || { lines: [] };
    st.layout.lines = st.layout.lines.concat(a.lines);
  },
  page: (st, a) => { st.launch.page = { size: a.format.toUpperCase() }; },
});

// Pure. Returns { launch: { crop?, layout?, page? }, warnings }.
export const translateOpenActions = (actions, { width = 0, height = 0 } = {}) => {
  const st = {   // one pass's state, each step writes into it
    warnings: [], launch: {},
    rect: null,     // { x1, x2, y1, y2 } crop-edge state across crop actions
    layout: null,   // { lines, imageFilter?, filterColor? }
  };
  for (const a of actions || []) {
    if (Object.hasOwn(STEPS, a.op)) STEPS[a.op](st, a, width, height);   /* no default — the parser only emits the ops above */
  }
  const { rect, layout, launch, warnings } = st;

  if (rect) {
    launch.crop = {   // canonical wire spelling ({w,h})
      x: Math.round(Math.min(rect.x1, rect.x2)),
      y: Math.round(Math.min(rect.y1, rect.y2)),
      w: Math.max(1, Math.round(Math.abs(rect.x2 - rect.x1))),
      h: Math.max(1, Math.round(Math.abs(rect.y2 - rect.y1))),
    };
  }
  if (layout) {
    if (width > 0 && height > 0) {
      layout.imageWidth = width;
      layout.imageHeight = height;
    }
    launch.layout = layout;
  }
  return { launch, warnings };
};
