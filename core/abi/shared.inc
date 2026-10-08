// Export bodies that are identical across the two extern "C" ABIs. Included by
// wasmApi.cpp and cliApi.cpp INSIDE their extern "C" block; each defines
// STENCIL_ABI(wasmName, cliName) first to pick its own exported spelling, so one
// body here emits both symbols. Editing this file changes BOTH ABIs.
#ifndef STENCIL_ABI
#  error "define STENCIL_ABI(wasmName, cliName) before including abi/shared.inc"
#endif

// The canonical page-format names, space-separated ("A0 A1 … C10", no "custom") in
// A/B/C-series order. Static storage — the caller must not free it.
const char* STENCIL_ABI(pageFormats, pageFormats)(void) { return pageFormatNames(); }

// varName is the ASCII code of the single variable, 'x' or 'y'. An empty expr is
// the identity: valid, and apply() returns `value` unchanged.
int STENCIL_ABI(formulaValidate, validateFormula)(const char* expr, int varName) {
  return FormulaParser::validate(expr ? expr : "", static_cast<char>(varName)) ? 1 : 0;
}

double STENCIL_ABI(formulaApply, applyFormula)(const char* expr, int varName,
                                               double value, int allowFormulas) {
  return FormulaParser::apply(expr ? expr : "", static_cast<char>(varName), value,
                              allowFormulas != 0);
}

// The same pair with named constants in reach: `x` / `y` (the other axis; `value` still
// binds `varName`), PAGE_WIDTH / PAGE_HEIGHT in `unit` plus their _CM / _IN forms, and
// IMAGE_WIDTH / IMAGE_HEIGHT in pixels. A non-finite context value is "not supplied": that
// name stays unknown, so the formula is invalid and apply() returns `value`. `unit` is the
// selected display unit — "in" converts, anything else (and NULL) reads as cm.
int STENCIL_ABI(formulaValidateCtx, validateFormulaCtx)(const char* expr, double x, double y,
                                                        double pageWcm, double pageHcm,
                                                        double imageW, double imageH,
                                                        const char* unit) {
  return FormulaParser::validate(expr ? expr : "",
                                 abi::toFormulaContext(x, y, pageWcm, pageHcm, imageW,
                                                       imageH, unit))
             ? 1
             : 0;
}

double STENCIL_ABI(formulaApplyCtx, applyFormulaCtx)(const char* expr, int varName,
                                                     double value, int allowFormulas,
                                                     double x, double y, double pageWcm,
                                                     double pageHcm, double imageW,
                                                     double imageH, const char* unit) {
  return FormulaParser::apply(expr ? expr : "", static_cast<char>(varName), value,
                              allowFormulas != 0,
                              abi::toFormulaContext(x, y, pageWcm, pageHcm, imageW, imageH,
                                                    unit));
}

// Sobel edge detection in place on a width x height RGBA8 buffer: dark edges on a
// white page, alpha preserved. Pinned integer math — the JS fallback
// (contourFilter.js) must stay byte-identical. Null data / degenerate dims: no-op.
void STENCIL_ABI(applyContourRGBA, applyContour)(std::uint8_t* data, int width,
                                                 int height) {
  applyContourRGBA(data, width, height);
}

// A human duration ("days 23", "fortnight", "off") -> *outMs, 0 = keep forever; a 0 return
// is invalid. int64 on BOTH ABIs: ms caps at 2^53-1, so JS reads it as two 32-bit halves.
int STENCIL_ABI(parseDuration, parseDuration)(const char* spec, long long* outMs) {
  static const DurationParser dp;
  long long ms = 0;
  if (!dp.parse(spec ? spec : "", ms)) return 0;
  if (outMs) *outMs = ms;
  return 1;
}

// A crop window committed to integer pixels inside imageW x imageH: out[0..3] = x, y, w, h.
void STENCIL_ABI(snapCropRect, snapCropRect)(double x, double y, double w, double h,
                                             double imageW, double imageH, double* out) {
  const CropRect r = snapCropRect(CropRect{x, y, w, h}, imageW, imageH);
  if (out) { out[0] = r.x; out[1] = r.y; out[2] = r.width; out[3] = r.height; }
}

// What moving a crop window from old to new does to its lines: out[0] = orientationChanged
// (0/1, the lines clear), out[1] = the width ratio they scale by (1 on a flip).
void STENCIL_ABI(cropChange, cropChange)(double oldX, double oldY, double oldW, double oldH,
                                         double newX, double newY, double newW, double newH,
                                         double* out) {
  const CropChange c =
      cropChange(CropRect{oldX, oldY, oldW, oldH}, CropRect{newX, newY, newW, newH});
  if (out) { out[0] = c.orientationChanged ? 1.0 : 0.0; out[1] = c.scale; }
}

// One quarter-turn of an edit's window over the unturned original: out[0..3] = the snapped
// rect in the turned space, out[4] = the wrapped count. The lines turn host-side.
void STENCIL_ABI(rotateEditQuarter, rotateEditQuarter)(double x, double y, double w, double h,
                                                       int quarters, double originalW,
                                                       double originalH, int clockwise,
                                                       double* out) {
  const EditTurn t =
      rotateEditQuarter(CropRect{x, y, w, h}, quarters, originalW, originalH, clockwise != 0);
  if (!out) return;
  out[0] = t.crop.x; out[1] = t.crop.y; out[2] = t.crop.width; out[3] = t.crop.height;
  out[4] = t.quarters;
}

// A left-right flip of the shown edit: out[0..3] = the reflected, snapped window, out[4] = the
// negated count; the host toggles its mirror flag and mirrors the lines.
void STENCIL_ABI(mirrorEdit, mirrorEdit)(double x, double y, double w, double h, int quarters,
                                         double originalW, double originalH, double* out) {
  const EditTurn t = mirrorEdit(CropRect{x, y, w, h}, quarters, originalW, originalH);
  if (!out) return;
  out[0] = t.crop.x; out[1] = t.crop.y; out[2] = t.crop.width; out[3] = t.crop.height;
  out[4] = t.quarters;
}
