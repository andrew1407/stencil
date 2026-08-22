#pragma once

// ── Arithmetic in numeric inputs ────────────────────────────────────────────
// Numeric fields take an expression, not just a number: "45 + 9" → 54, "* 9" on 3 → 27.
// QAbstractSpinBox owns its line edit, so overriding validate()/valueFromText() adds
// this while the step arrows and clamping keep working.
//
// Operators mirror browser/js/ui/numericInput.js and core/parse/formulaParser:
// + - * / ** and parens, ** right-associative, unary sign outside **.

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QString>
#include <QValidator>

namespace stencil::gui {

  /// Evaluate `text` as an arithmetic expression.
  /// A leading `*`, `/` or `**` continues from `current` ("* 9" on 3 → 27); a leading
  /// `+`/`-` is a SIGN, so "-5" still means negative five. Sets *ok false (and returns
  /// 0.0) when the text isn't a valid, finite expression.
  double evalNumericExpression(const QString& text, double current, bool* ok);

  /// QSpinBox that accepts an expression in its editor.
  class ExprSpinBox : public QSpinBox {
    Q_OBJECT
   public:
    explicit ExprSpinBox(QWidget* parent = nullptr) : QSpinBox(parent) {}

   protected:
    QValidator::State validate(QString& input, int& pos) const override;
    int valueFromText(const QString& text) const override;
  };

  /// QDoubleSpinBox that accepts an expression in its editor.
  class ExprDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT
   public:
    explicit ExprDoubleSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {}

   protected:
    QValidator::State validate(QString& input, int& pos) const override;
    double valueFromText(const QString& text) const override;
  };

}  // namespace stencil::gui
