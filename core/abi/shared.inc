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

// Sobel edge detection in place on a width x height RGBA8 buffer: dark edges on a
// white page, alpha preserved. Pinned integer math — the JS fallback
// (contourFilter.js) must stay byte-identical. Null data / degenerate dims: no-op.
void STENCIL_ABI(applyContourRGBA, applyContour)(std::uint8_t* data, int width,
                                                 int height) {
  applyContourRGBA(data, width, height);
}

// Parse a human duration ("days 23", "fortnight", "off") into ms (0 = keep forever),
// written to *outMs; 0 return means invalid and leaves *outMs untouched. outMs is
// int64 on BOTH ABIs — DurationParser caps ms at 2^53-1 (Number.isSafeInteger), so
// the browser reads the slot losslessly as two 32-bit halves, no BigInt needed.
int STENCIL_ABI(parseDuration, parseDuration)(const char* spec, long long* outMs) {
  static const DurationParser dp;
  long long ms = 0;
  if (!dp.parse(spec ? spec : "", ms)) return 0;
  if (outMs) *outMs = ms;
  return 1;
}
