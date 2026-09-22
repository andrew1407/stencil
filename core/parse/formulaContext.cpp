#include "formulaContext.hpp"
#include "formulaParser.hpp"
#include <cmath>

namespace stencil::core {

  namespace {

    constexpr double CM_PER_INCH = 2.54;  // browser/js/utils/math.js CM_PER_INCH

    // Twin of cmToUnit(): only "in" converts, every other unit word reads as cm.
    double inUnit(double cm, const std::string& unit) {
      return unit == "in" ? cm / CM_PER_INCH : cm;
    }

    // Validation has no live coordinates: an unbound axis probes at 1, as it always has.
    FormulaContext withProbeAxes(FormulaContext c) {
      if (!std::isfinite(c.x)) c.x = 1.0;
      if (!std::isfinite(c.y)) c.y = 1.0;
      return c;
    }

  }  // namespace

  bool formulaConstant(const FormulaContext& ctx, const std::string& name, char varName,
                       double varValue, double& out) {
    if (name.size() == 1 && name[0] == varName) {
      out = varValue;
      return true;
    }
    double v = kFormulaUnset;
    if (name == "x") v = ctx.x;
    else if (name == "y") v = ctx.y;
    else if (name == "PAGE_WIDTH") v = inUnit(ctx.pageWidthCm, ctx.unit);
    else if (name == "PAGE_HEIGHT") v = inUnit(ctx.pageHeightCm, ctx.unit);
    else if (name == "PAGE_WIDTH_CM") v = ctx.pageWidthCm;
    else if (name == "PAGE_HEIGHT_CM") v = ctx.pageHeightCm;
    else if (name == "PAGE_WIDTH_IN") v = inUnit(ctx.pageWidthCm, "in");
    else if (name == "PAGE_HEIGHT_IN") v = inUnit(ctx.pageHeightCm, "in");
    else if (name == "IMAGE_WIDTH") v = ctx.imageWidth;
    else if (name == "IMAGE_HEIGHT") v = ctx.imageHeight;
    else return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
  }

  std::optional<double> FormulaParser::evaluate(const std::string& expr,
                                                const FormulaContext& ctx) {
    return evaluate(expr, '\0', 0.0, ctx);
  }

  bool FormulaParser::validate(const std::string& expr, const FormulaContext& ctx) {
    if (isBlankFormula(expr)) return true;
    return evaluate(expr, withProbeAxes(ctx)).has_value();
  }

  double FormulaParser::apply(const std::string& expr, char varName, double value,
                              bool allowFormulas, const FormulaContext& ctx) {
    if (!allowFormulas || isBlankFormula(expr)) return value;
    const auto result = evaluate(expr, varName, value, ctx);
    return result.has_value() ? *result : value;
  }

}
