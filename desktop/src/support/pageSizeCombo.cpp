#include "pageSizeCombo.hpp"

#include "pageMetrics.hpp"
#include <QComboBox>
#include <QSignalBlocker>
#include <QStringList>
#include <cmath>

namespace stencil::gui {

  void fillPageSizeCombo(QComboBox* combo, bool includeCustom,
                         const QString& units) {
    if (!combo) return;
    const bool inches = (units == QLatin1String("in"));
    const double factor = inches ? 1.0 / 2.54 : 1.0;
    // ≤2 decimals, trailing zeros trimmed — the option-label contract with the browser dropdown.
    const auto num = [](double v) {
      return QString::number(std::round(v * 100.0) / 100.0);
    };
    // A label-only re-render must never fire the callers' change handlers.
    const QSignalBlocker block(combo);
    if (combo->count() == 0) {  // first fill: items in canonical order
      if (includeCustom)
        combo->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
      const QStringList names = QString::fromLatin1(core::pageFormatNames())
                                    .split(' ', Qt::SkipEmptyParts);
      for (const QString& n : names) combo->addItem(n, n);
    }
    for (int i = 0; i < combo->count(); ++i) {
      const QString name = combo->itemData(i).toString();
      if (name == QLatin1String("custom")) continue;  // label stays "Custom"
      const core::PageSize ps = core::namedPageSize(name.toStdString());
      // No trailing unit word: this combo always sits beside its own unit combo, which already says
      // "cm"/"in" once for the whole row (user report; browser twin: units.js pageFormatLabel).
      combo->setItemText(i, QString("%1 (%2 × %3)")
                                .arg(name, num(ps.width * factor), num(ps.height * factor)));
    }
  }

}
