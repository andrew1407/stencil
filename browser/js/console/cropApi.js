// ── window.stencil's crop and page-coordinate conversions ──────────────────
// crop() resolves axis tokens to rotated-original pixels and commits through the
// UI's own applyCrop, so a scripted crop and the modal's crop are one code path.
import { resolveAxisPx } from '../core/units.js';
import { cropAspect, scaleCropCentered } from '../core/cropGeometry.js';

export const createCropApi = ({ app }) => {
  let stencil;   // the frozen facade, handed over by setFacade after the guard

  const api = {
    // Crop by axis edges: x1/y1/x2/y2 each a px move, an absolute length ('3cm'/'-4in'/
    // '50%'; '-' = from the axis end), or omitted. Commits via the UI's own applyCrop.
    // ONE axis alone derives the other's LENGTH from the page proportion (`album` picks the
    // orientation); `aspect` ('W:H') then SHRINKS one dimension about its centre to fit.
    // `{ scale }` instead grows/shrinks the current crop about its centre (the modal's
    // wheel/pinch) — mutually exclusive with the edge tokens.
    crop(spec = {}) {
      if (!app.originalImage) throw new Error('No image loaded to crop');
      const dims = app.imageModel.effectiveOriginalDims();   // { w, h } in rotated-original pixels
      const r = app.cropRect || app.imageModel.defaultCropRect();
      if (spec.scale != null) {
        const factor = Number(spec.scale);
        if (!(factor > 0)) throw new Error('crop scale must be a positive number');
        const aspect = r.height > 0 ? r.width / r.height : 1;
        const next = scaleCropCentered(r, factor, aspect, dims.w, dims.h);
        app.imageModel.applyCrop({ x: next.x, y: next.y, width: next.width, height: next.height }, { recalc: true });
        return stencil;
      }
      const ps = app.getPageDimensions();
      const pxPerCmX = app.canvas.width / ps.width, pxPerCmY = app.canvas.height / ps.height;
      const edge = (tok, cur, lengthPx, pxPerCm) =>
        tok == null ? cur : resolveAxisPx(tok, { lengthPx, pxPerCm, currentPx: cur });
      let x1 = edge(spec.x1, r.x, dims.w, pxPerCmX);
      let x2 = edge(spec.x2, r.x + r.width, dims.w, pxPerCmX);
      let y1 = edge(spec.y1, r.y, dims.h, pxPerCmY);
      let y2 = edge(spec.y2, r.y + r.height, dims.h, pxPerCmY);

      const xGiven = spec.x1 != null || spec.x2 != null;
      const yGiven = spec.y1 != null || spec.y2 != null;
      if (xGiven !== yGiven) {
        const aspect = cropAspect(ps.width, ps.height, !!spec.album);   // width / height
        if (xGiven) {                                  // have width → derive height
          y1 = r.y; y2 = r.y + Math.abs(x2 - x1) / aspect;
        } else {                                       // have height → derive width
          x1 = r.x; x2 = r.x + Math.abs(y2 - y1) * aspect;
        }
      }
      let rx = Math.min(x1, x2), ry = Math.min(y1, y2);
      let rw = Math.abs(x2 - x1), rh = Math.abs(y2 - y1);
      // Optional aspect fit (mirrors core resolveCropRect): shrink ONE dimension
      // symmetrically about the centre to hit W:H — never grow, never move the rect.
      if (spec.aspect != null) {
        const m = /^(\d+):(\d+)$/.exec(String(spec.aspect));
        const ratio = m && Number(m[1]) > 0 && Number(m[2]) > 0 ? Number(m[1]) / Number(m[2]) : 0;
        if (!(ratio > 0)) throw new Error('crop aspect must be "W:H" with positive integers');
        let w = rw, h = rh;
        if (h * ratio <= w) w = h * ratio;   // too wide → shrink the width
        else h = w / ratio;                  // too tall → shrink the height
        // Degenerate results keep at least 1px, but never grow past the resolved rect.
        w = Math.min(rw, Math.max(w, 1));
        h = Math.min(rh, Math.max(h, 1));
        rx += (rw - w) / 2; ry += (rh - h) / 2;
        rw = w; rh = h;
      }
      app.imageModel.applyCrop({ x: rx, y: ry, width: rw, height: rh }, { recalc: true });
      return stencil;
    },

    // px → page coords (cm, with active formulas applied).
    px2Page({ x = 0, y = 0 } = {}) { return app.pixelToPageCoords(Number(x), Number(y)); },
    // page (cm) → px. Inverts the linear page mapping; does NOT invert formulas.
    page2Px({ x = 0, y = 0 } = {}) {
      const ps = app.getPageDimensions();
      return { x: (Number(x) / ps.width) * app.canvas.width, y: (Number(y) / ps.height) * app.canvas.height };
    },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};
