#pragma once
#include <cctype>
#include <limits>
#include <string>

// The named values a formula may read besides its own axis — twin of
// browser/js/core/parse/formulaContext.js. Page fields are always cm and image fields
// always pixels; `unit` only picks the spelling PAGE_WIDTH / PAGE_HEIGHT report in.
namespace stencil::core {

  // Not supplied: the name stays unknown, so the formula is invalid (-> identity).
  inline constexpr double kFormulaUnset = std::numeric_limits<double>::quiet_NaN();

  struct FormulaContext {
    double x = kFormulaUnset;
    double y = kFormulaUnset;
    double pageWidthCm = kFormulaUnset;
    double pageHeightCm = kFormulaUnset;
    double imageWidth = kFormulaUnset;
    double imageHeight = kFormulaUnset;
    std::string unit = "cm";  // "in" converts; every other word reads as cm (cmToUnit)
  };

  // A blank formula is the identity: valid, and apply() returns its input unchanged.
  inline bool isBlankFormula(const std::string& expr) {
    for (char c : expr) {
      if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
  }

  // `varName` first, so the context-free entry points keep their exact behaviour; then
  // x, y, PAGE_WIDTH / PAGE_HEIGHT (+ _CM / _IN), IMAGE_WIDTH / IMAGE_HEIGHT. Case-sensitive.
  bool formulaConstant(const FormulaContext& ctx, const std::string& name, char varName,
                       double varValue, double& out);

}
