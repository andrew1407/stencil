// Export bodies that are identical across the two extern "C" ABIs. Included by
// wasmApi.cpp and cliApi.cpp INSIDE their extern "C" block; each defines
// STENCIL_ABI(wasmName, cliName) first to pick its own exported spelling, so one
// body here emits both symbols. Editing this file changes BOTH ABIs.
#ifndef STENCIL_ABI
#  error "define STENCIL_ABI(wasmName, cliName) before including abi/shared.inc"
#endif

const char* STENCIL_ABI(pageFormats, pageFormats)(void) { return pageFormatNames(); }

int STENCIL_ABI(formulaValidate, validateFormula)(const char* expr, int varName) {
  return FormulaParser::validate(expr ? expr : "", static_cast<char>(varName)) ? 1 : 0;
}

double STENCIL_ABI(formulaApply, applyFormula)(const char* expr, int varName,
                                               double value, int allowFormulas) {
  return FormulaParser::apply(expr ? expr : "", static_cast<char>(varName), value,
                              allowFormulas != 0);
}

void STENCIL_ABI(applyContourRGBA, applyContour)(std::uint8_t* data, int width,
                                                 int height) {
  applyContourRGBA(data, width, height);
}

// outMs is int64 on both ABIs. DurationParser caps ms at 2^53-1 to match
// Number.isSafeInteger, so the browser reads the slot losslessly as two halves.
int STENCIL_ABI(parseDuration, parseDuration)(const char* spec, long long* outMs) {
  static const DurationParser dp;
  long long ms = 0;
  if (!dp.parse(spec ? spec : "", ms)) return 0;
  if (outMs) *outMs = ms;
  return 1;
}
