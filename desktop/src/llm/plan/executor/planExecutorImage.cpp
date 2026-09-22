// The ops that change the working picture: crop, rotate, filter, layout, formula, page and blank.
// Each needs an image (or makes one), and a fresh image is a fresh frame.
#include "planExecutorParts.hpp"

namespace stencil::llm::exec {

  bool applyImageAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                       QStringList* notes, bool* handled, QString* err) {
    *handled = true;
    switch (a.op) {
        case OpKind::CROP: {
          if (!target.hasImage()) {
            *err = QStringLiteral("crop: no working image");
            return false;
          }
          core::CropRect rect;
          if (!resolveCrop(a, target.effectiveOriginalSize(), target.pageCm(), rect, err))
            return false;
          if (!target.applyCropRect(rect)) {
            *err = QStringLiteral("crop: could not apply the crop");
            return false;
          }
          frame.composeCrop(rect);
          return true;
        }
        case OpKind::ROTATE: {
          if (!target.hasImage()) {
            *err = QStringLiteral("rotate: no working image");
            return false;
          }
          for (int i = 0; i < a.times; ++i) {
            const QSize s = target.workingSize();  // dims BEFORE this turn
            frame.composeRotate(!a.rotateLeft, s.width(), s.height());
            target.rotateQuarter(!a.rotateLeft);
          }
          return true;
        }
        case OpKind::FILTER:
          target.setImageFilter(a.mode, a.tint);
          return true;
        case OpKind::LAYOUT: {
          if (!target.hasImage()) {
            *err = QStringLiteral("layout: no working image");
            return false;
          }
          // Model-frame → current frame, then clamp into the working image's
          // bounds before drawing (contract §1).
          const QSize ws = target.workingSize();
          core::Lines lines = a.lines;
          for (core::Line& line : lines)
            for (core::Point& p : line.points) {
              p = frame.map(p);
              p.x = std::clamp(p.x, 0.0, static_cast<double>(ws.width()));
              p.y = std::clamp(p.y, 0.0, static_cast<double>(ws.height()));
            }
          target.setLayoutLines(lines);
          return true;
        }
        case OpKind::FORMULA: {
          // §2 `enabled` form: the allow-formulas toggle, nothing per-axis.
          if (a.formulaEnabled >= 0) {
            target.setFormulasEnabled(a.formulaEnabled == 1);
            return true;
          }
          // "" clears the axis (identity) - nothing to grammar-check. A non-empty expr is validated again
          // by the core formula engine before use (contract §2).
          if (!a.expr.isEmpty() &&
              !core::FormulaParser::validate(a.expr.toStdString(),
                                             target.formulaContext())) {
            *err = QStringLiteral("formula: the core parser rejected \"%1\"").arg(a.expr);
            return false;
          }
          target.setFormula(a.axis, a.expr);
          return true;
        }
        case OpKind::PAGE:
          // §2: format OR custom cm dims (exactly one — the parser enforced it).
          if (a.widthCm > 0)
            target.setPageCustom(a.widthCm, a.heightCm);
          else
            target.setPageFormat(a.format.toUpper());
          return true;
        case OpKind::BLANK: {
          if (!target.newBlank(a.color, a.format.toUpper(), a.widthCm, a.heightCm, err))
            return false;
          frame.reset();  // a fresh image is a fresh frame
          return true;
        }
      default: break;
    }
    *handled = false;
    return false;
  }

}  // namespace stencil::llm::exec
