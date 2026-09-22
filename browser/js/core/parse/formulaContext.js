import { cmToUnit } from '../../utils.js';

// The named values a formula may read besides its own axis — twin of
// core/parse/formulaContext.hpp. Page fields are always cm and image fields always pixels;
// `unit` only picks the spelling PAGE_WIDTH / PAGE_HEIGHT report in.

// A blank formula is the identity: valid, and apply() returns its input unchanged.
export const isBlankFormula = (s) => !s || !s.trim();

// Validation has no live coordinates: an unbound axis probes at 1, as it always has.
export const withProbeAxes = (ctx) => ({
  ...ctx,
  x: Number.isFinite(ctx?.x) ? ctx.x : 1,
  y: Number.isFinite(ctx?.y) ? ctx.y : 1,
});

// `varName` first, so the context-free entry points keep their exact behaviour; then
// x, y, PAGE_WIDTH / PAGE_HEIGHT (+ _CM / _IN), IMAGE_WIDTH / IMAGE_HEIGHT. Case-sensitive.
// null for an unknown name and for a name whose field is absent or non-finite.
export const formulaConstant = (ctx, name, varName, varValue) => {
  if (name.length === 1 && name === varName) return varValue;
  const c = ctx || {};
  let v;
  if (name === 'x') v = c.x;
  else if (name === 'y') v = c.y;
  else if (name === 'PAGE_WIDTH') v = cmToUnit(c.pageWidthCm, c.unit);
  else if (name === 'PAGE_HEIGHT') v = cmToUnit(c.pageHeightCm, c.unit);
  else if (name === 'PAGE_WIDTH_CM') v = c.pageWidthCm;
  else if (name === 'PAGE_HEIGHT_CM') v = c.pageHeightCm;
  else if (name === 'PAGE_WIDTH_IN') v = cmToUnit(c.pageWidthCm, 'in');
  else if (name === 'PAGE_HEIGHT_IN') v = cmToUnit(c.pageHeightCm, 'in');
  else if (name === 'IMAGE_WIDTH') v = c.imageWidth;
  else if (name === 'IMAGE_HEIGHT') v = c.imageHeight;
  else return null;
  return Number.isFinite(v) ? v : null;
};
