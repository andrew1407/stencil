// ── open.actions → editor launch options (contract §8 translation) ──────────

// Resolve one §2 crop token to an absolute pixel coordinate on an axis of `length`
// px (mirrors browser/js/core/units.js resolveAxisPx, minus cm/in — the extension
// has no page metrics, so physical units can't be resolved here). null = unsupported.
const resolveCropToken = (tok, length) => {
  const m = /^(-?)(\d+(?:\.\d+)?|\.\d+)(%|px|cm|in)?$/.exec(String(tok));
  if (!m) return null;
  const unit = m[3] || 'px';
  if (unit === 'cm' || unit === 'in') return null;
  const v = parseFloat(m[2]);
  const px = unit === '%' ? (v / 100) * length : v;
  return m[1] === '-' ? length - px : px;
};

// Translate validated open.actions onto the existing `#stencil=` launch options:
//   crop           → payload `crop` rect (original-image pixels; needs the dims)
//   filter/layout  → a layout payload with imageFilter/filterColor/lines
//   page           → payload `page: { size }`; rotate has no slot → dropped w/ warning
// Pure. Returns { launch: { crop?, layout?, page? }, warnings }.
export const translateOpenActions = (actions, { width = 0, height = 0 } = {}) => {
  const warnings = [];
  const launch = {};
  let rect = null;     // { x1, x2, y1, y2 } crop-edge state across crop actions
  let layout = null;   // { lines, imageFilter?, filterColor? }

  for (const a of actions || []) {
    switch (a.op) {
      case 'crop': {
        if (!(width > 0 && height > 0)) {
          warnings.push('Skipped "crop" — the image dimensions are unknown');
          break;
        }
        const cur = rect || { x1: 0, x2: width, y1: 0, y2: height };
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
          warnings.push(`Skipped "crop" — cm/in crop units can't be resolved in the extension (token "${bad}"); crop in the editor instead`);
          break;
        }
        rect = next;
        break;
      }
      case 'rotate':
        // The launch payload has no rotation slot — rotating happens in the editor.
        warnings.push('Skipped "rotate" — the editor hand-off can\'t carry a rotation; rotate in the editor after it opens');
        break;
      case 'filter':
        layout = layout || { lines: [] };
        layout.imageFilter = a.mode;
        if (a.mode === 'custom') layout.filterColor = a.tint;
        else delete layout.filterColor;
        break;
      case 'layout':
        layout = layout || { lines: [] };
        layout.lines = layout.lines.concat(a.lines);
        break;
      case 'page':
        launch.page = { size: a.format.toUpperCase() };
        break;
      /* no default — the parser only emits the ops above */
    }
  }

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
