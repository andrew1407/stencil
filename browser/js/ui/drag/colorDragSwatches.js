// The swatches a colour drag reads and writes: a colour field (an `.alpha-input` beside it carries
// its alpha), the well a label wraps around one, a Lines-tab row swatch, or a control that registers
// its own read and apply. `apply` takes a colour through the swatch's own change path, as a pick
// does. Desktop twin: desktop/src/support/drag/colorDrag.{hpp,cpp}.
import { cssColorParts, cssWithAlpha } from '../../utils.js';
import { normalizeHex } from '../../core/settings/accents.js';
import { applyLineChange, selectLineFromList } from '../../core/line/selection.js';
import { pointColorOf } from '../../core/line/render.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const FIELD = 'input[type="color"]';
const WELLS = '.vs-color, .oi-color';
// The Lines-tab row swatches and the line colour each shows (ui/panel/lines/events.js SWATCHES).
export const ROW_SWATCHES = Object.freeze({ 'lines-swatch': 'color', 'lines-point-swatch': 'pointColor' });
const ROW_SWATCH = Object.keys(ROW_SWATCHES).map((cls) => `.${cls}`).join(', ');
const STROKE = constants.DEFAULT_VISUALS.color;

/** `{ hex, alpha }` (alpha 0..1) of a `#rgb`, `#rrggbb` or `#rrggbbaa` colour, else null. */
export const parseSwatchColor = (value) => {
  const { hex, alpha } = cssColorParts(value);
  const h = normalizeHex(hex);
  return h ? { hex: h, alpha } : null;
};

const byteOf = (alpha) => Math.round(alpha * 255);

/** The colour a swatch can hand over: a hex one it shows at all, so not at alpha 0. */
export const liftableColor = (value) => {
  const c = parseSwatchColor(value);
  return c && c.alpha > 0 ? c : null;
};

/** What `target` takes from `source`: its RGBA when both carry alpha, else its RGB over the target's own
 *  alpha; null when the source shows none or the target already shows it (alpha 0 shows none). */
export const colorToApply = (source, target) => {
  const s = liftableColor(source.read());
  if (!s) return null;
  const t = parseSwatchColor(target.read());
  const alpha = source.alpha && target.alpha ? s.alpha : (target.alpha ? (t?.alpha ?? 1) : 1);
  if (t && t.alpha > 0 && t.hex === s.hex && byteOf(t.alpha) === byteOf(alpha)) return null;
  return cssWithAlpha(s.hex, alpha);
};

const alphaBoxOf = (field) => field.closest?.('.control-group')?.querySelector?.('.alpha-input') ?? null;
const boxAlpha = (box) => {
  const n = Number(box.value);
  return Number.isFinite(n) ? Math.max(0, Math.min(255, n)) / 255 : 1;
};

// The pair is one colour, so the box is set first and the field's own input + change carry both.
const fieldSwatch = (field, el = field) => {
  const box = alphaBoxOf(field);
  return {
    el,
    alpha: !!box,
    read: () => (box ? cssWithAlpha(field.value, boxAlpha(box)) : field.value),
    enabled: () => !field.disabled,
    apply: (color) => {
      const { hex, alpha } = cssColorParts(color);
      if (box) box.value = String(byteOf(alpha));
      field.value = hex;
      for (const type of ['input', 'change']) field.dispatchEvent(new Event(type, { bubbles: true }));
    },
  };
};

// A row's colour lands as its swatch's pick does: the stroke swatch selects its line first, and a
// commit re-shows the bar holding that line.
const rowSwatch = (app, el) => {
  const idx = parseInt(el.closest('.lines-row')?.dataset.idx, 10);
  const prop = ROW_SWATCHES[Object.keys(ROW_SWATCHES).find((cls) => el.classList.contains(cls))];
  const line = () => app.lines[idx];
  return {
    el,
    alpha: true,
    read: () => (line() ? (prop === 'pointColor' ? pointColorOf(line()) : line()[prop]) || STROKE : null),
    enabled: () => !!line() && !app.compareReadOnly(),
    apply: (color) => {
      if (prop === 'color') selectLineFromList(app, idx);
      if (applyLineChange(app, idx, prop, color, { commit: true }) && idx === app.selectedLineIdx)
        app.showSelectionPanel(line());
    },
  };
};

let rowsApp = null;
const registered = new Map();

/** The app the Lines-tab row swatches read and write. */
export const bindSwatchApp = (app) => { rowsApp = app; };

/** `el` reads and applies its colour itself: `{ read, apply, alpha?, enabled? }`. */
export const registerColorSwatch = (el, { read, apply, alpha = false, enabled = () => true }) => {
  if (el) registered.set(el, { el, read, apply, alpha, enabled });
};

/** The swatch `node` belongs to, or null. A hidden picker field is never one. */
export const swatchOf = (node) => {
  for (let n = node; n && n.nodeType === 1; n = n.parentElement) if (registered.has(n)) return registered.get(n);
  const well = node?.closest?.(WELLS);
  const inWell = well?.querySelector?.(FIELD);
  if (inWell) return fieldSwatch(inWell, well);
  const field = node?.closest?.(FIELD);
  if (field && field.getAttribute('aria-hidden') !== 'true') return fieldSwatch(field);
  const row = rowsApp ? node?.closest?.(ROW_SWATCH) : null;
  return row?.closest?.('.lines-row') ? rowSwatch(rowsApp, row) : null;
};

const shown = (el) => el.isConnected !== false && (el.getClientRects?.().length ?? 0) > 0;

/** Every swatch on the page that could take a drop now: laid out and enabled. */
export const liveSwatches = (doc = document) => {
  const els = new Set(registered.keys());
  for (const el of doc.querySelectorAll(`${WELLS}, ${FIELD}${rowsApp ? `, ${ROW_SWATCH}` : ''}`)) els.add(el);
  const seen = new Set();
  const out = [];
  for (const el of els) {
    const s = shown(el) ? swatchOf(el) : null;
    if (!s || seen.has(s.el) || !s.enabled()) continue;
    seen.add(s.el);
    out.push(s);
  }
  return out;
};
