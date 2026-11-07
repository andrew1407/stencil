#pragma once

// Numeric fields take an expression: "45 + 9" → 54, "* 9" on 3 → 27. Operators mirror
// browser/js/ui/control/numericInput.js and core/parse/formulaParser (** right-associative).

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QString>
#include <QValidator>

namespace stencil::gui {

  /// A leading `*`, `/` or `**` continues from `current`; a leading `+`/`-` is a SIGN.
  /// Sets *ok false (and returns 0.0) for anything not a valid, finite expression.
  double evalNumericExpression(const QString& text, double current, bool* ok);

  class ExprSpinBox : public QSpinBox {
    Q_OBJECT
   public:
    explicit ExprSpinBox(QWidget* parent = nullptr) : QSpinBox(parent) {}

   protected:
    QValidator::State validate(QString& input, int& pos) const override;
    int valueFromText(const QString& text) const override;
  };

  class ExprDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT
   public:
    explicit ExprDoubleSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {}

   protected:
    QValidator::State validate(QString& input, int& pos) const override;
    double valueFromText(const QString& text) const override;
  };

  /// Every spin box's value stands in from its well by the skin's padding (browser input: aqua 12px,
  /// webcore 6px): QSS padding never reaches a spin box's own line edit. Idempotent, app-wide.
  void installSpinTextInsets();

}  // namespace stencil::gui
