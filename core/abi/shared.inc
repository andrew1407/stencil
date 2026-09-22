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
