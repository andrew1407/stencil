// ── window.stencil: the Line and Point wrappers ─────────────────
// Extracted from stencilApi.js. Mutually recursive (a Line lists Points, a removed Point
// hands back its Line), so they share one factory. `setFacade` supplies the frozen facade
// the wrappers fall back to for chaining — it exists only after createStencil finishes.
import { pointColorOf } from '../core/draw/renderer.js';
import { toHexColor } from '../core/settings/accents.js';
import { str } from './coerce.js';

export const createLineWrappers = ({ app, guard }) => {
  let stencil;   // the facade, handed over once it is built

  // ── Point: wraps one {x,y} in a line's points (crop-local px). lineIdx === -1 is
  // the in-progress currentLine (matches the coord table's target resolution). ──
  const makePoint = (lineIdx, ptIdx) => {
    const raw = () => {
      const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
      return line ? line.points[ptIdx] : null;
    };
    let point = {
      get lineIdx() { return lineIdx; },
      get ptIdx() { return ptIdx; },
      get x() { const p = raw(); return p ? p.x : undefined; },
      get y() { const p = raw(); return p ? p.y : undefined; },
      set x(v) { app.setPointCoord(lineIdx, ptIdx, 'x', v); },
      set y(v) { app.setPointCoord(lineIdx, ptIdx, 'y', v); },
      // Absolute set of x/y (and optionally the parent line's point size).
      apply({ x, y, size } = {}) {
        if (x != null) app.setPointCoord(lineIdx, ptIdx, 'x', x);
        if (y != null) app.setPointCoord(lineIdx, ptIdx, 'y', y);
        if (size != null) {
          const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
          if (line) { line.pointSize = Number(size); app.saveHistory(); app.renderer.redraw(); }
        }
        return point;
      },
      // Relative move by pixels on either axis.
      move({ x, y } = {}) {
        const p = raw();
        if (!p) return point;
        if (x != null) app.setPointCoord(lineIdx, ptIdx, 'x', p.x + Number(x));
        if (y != null) app.setPointCoord(lineIdx, ptIdx, 'y', p.y + Number(y));
        return point;
      },
      // Remove this point; empties the line → the line is dropped too. Returns the
      // owning line (or the facade if the line is gone) so chaining stays useful.
      remove() {
        app.removePoint(lineIdx, ptIdx);
        return (lineIdx === -1 || !app.lines[lineIdx]) ? stencil : makeLine(lineIdx);
      },
    };
    point = guard(point);   // reassign so chained returns hand back the guarded proxy
    return point;
  };

  // ── Line ──
  const makeLine = (startIdx) => {
    let idx = startIdx;                       // may shift when an earlier line is removed
    const obj = () => app.lines[idx];
    const commit = () => { app.saveHistory(); app.renderer.redraw(); };
    const setProp = (prop, value) => { const l = obj(); if (l) { l[prop] = value; commit(); } };
    let line = {
      get idx() { return idx; },
      get points() { const l = obj(); return l ? l.points.map((_, i) => makePoint(idx, i)) : []; },
      // Recolouring the stroke pins an inherit-fallback point colour first, so already-drawn
      // points keep their rendered colour (same rule as applySelectionChange).
      get color() { return obj()?.color; },
      set color(v) {
        const l = obj();
        if (!l) return;
        if (!l.pointColor) l.pointColor = l.color;
        l.color = toHexColor(str(v));
        commit();
      },
      // Point colour for THIS line. Reading reports the colour it actually draws in
      // (falling back to the stroke); assigning null/'' clears it back to that fallback.
      get pointColor() { const l = obj(); return l ? pointColorOf(l) : undefined; },
      set pointColor(v) { setProp('pointColor', v == null || v === '' ? '' : toHexColor(str(v))); },
      get thickness() { return obj()?.thickness; }, set thickness(v) { setProp('thickness', Number(v)); },
      get pointSize() { return obj()?.pointSize; }, set pointSize(v) { setProp('pointSize', Number(v)); },
      get style() { return obj()?.style; }, set style(v) { setProp('style', str(v)); },
      get fillColor() { return obj()?.fillColor; }, set fillColor(v) { setProp('fillColor', v == null ? 'transparent' : toHexColor(str(v))); },
      // Batch style update. Accepts color/pointColor/thickness/pointSize/style/fillColor.
      apply(opts = {}) {
        const l = obj();
        if (!l) return line;
        if (opts.color != null) {
          if (!l.pointColor) l.pointColor = l.color;   // see the color setter
          l.color = toHexColor(str(opts.color));
        }
        if (opts.pointColor != null) l.pointColor = opts.pointColor === '' ? '' : toHexColor(str(opts.pointColor));
        if (opts.thickness != null) l.thickness = Number(opts.thickness);
        const ms = opts.pointSize;
        if (ms != null) l.pointSize = Number(ms);
        if (opts.style != null) l.style = str(opts.style);
        if (opts.fillColor != null) l.fillColor = opts.fillColor === 'transparent' ? 'transparent' : toHexColor(str(opts.fillColor));
        commit();
        return line;
      },
      // Translate every point by pixel deltas.
      move({ x = 0, y = 0 } = {}) {
        const l = obj();
        if (!l) return line;
        const dx = Number(x) || 0, dy = Number(y) || 0;
        for (const p of l.points) { p.x += dx; p.y += dy; }
        app.saveHistory(); app.renderer.redraw(); app.coordTable.update(l.points, idx);
        return line;
      },
      // Rotate the points by `deg` clockwise around `pivot` ({x,y}) or the bbox centre.
      rotate(deg, pivot) {
        const l = obj();
        if (!l || !l.points.length) return line;
        const cx = pivot?.x ?? (Math.min(...l.points.map(p => p.x)) + Math.max(...l.points.map(p => p.x))) / 2;
        const cy = pivot?.y ?? (Math.min(...l.points.map(p => p.y)) + Math.max(...l.points.map(p => p.y))) / 2;
        const rad = (Number(deg) || 0) * Math.PI / 180, cos = Math.cos(rad), sin = Math.sin(rad);
        for (const p of l.points) {
          const dx = p.x - cx, dy = p.y - cy;
          p.x = cx + dx * cos - dy * sin;
          p.y = cy + dx * sin + dy * cos;
        }
        app.saveHistory(); app.renderer.redraw(); app.coordTable.update(l.points, idx);
        return line;
      },
      // Insert a point. `at.neighbour` ({x,y} or index) + `at.after` choose the slot.
      add(point, at = {}) {
        const l = obj();
        if (!l || !point) return line;
        const pt = { x: Number(point.x) || 0, y: Number(point.y) || 0 };
        let i = l.points.length;
        if (at.neighbour != null) {
          const n = typeof at.neighbour === 'number'
            ? at.neighbour
            : l.points.findIndex(p => p.x === at.neighbour.x && p.y === at.neighbour.y);
          if (n >= 0) i = at.after === false ? n : n + 1;
        }
        l.points.splice(i, 0, pt);
        app.saveHistory(); app.renderer.redraw(); app.coordTable.update(l.points, idx);
        return line;
      },
      // Remove a point by index or by a point reference ({x,y} or a Point wrapper).
      remove(indexOrPoint) {
        const l = obj();
        if (!l) return line;
        let i = indexOrPoint;
        if (typeof indexOrPoint !== 'number') {
          const ref = indexOrPoint && typeof indexOrPoint === 'object' ? { x: indexOrPoint.x, y: indexOrPoint.y } : null;
          i = ref ? l.points.findIndex(p => p.x === ref.x && p.y === ref.y) : -1;
        }
        if (i >= 0) app.removePoint(idx, i);
        return line;
      },
      // Append another line's points to this one and drop the other line.
      join(other) {
        const l = obj();
        const oIdx = other && typeof other.idx === 'number' ? other.idx : -1;
        const o = oIdx >= 0 ? app.lines[oIdx] : null;
        if (!l || !o || oIdx === idx) return line;
        l.points.push(...o.points.map(p => ({ x: p.x, y: p.y })));
        app.removeLine(oIdx);                   // saves history + redraws
        if (oIdx < idx) idx -= 1;               // our index shifts if the other was before us
        return line;
      },
    };
    line = guard(line);   // reassign so chained returns hand back the guarded proxy
    return line;
  };

  return { makePoint, makeLine, setFacade: (f) => { stencil = f; } };
};
