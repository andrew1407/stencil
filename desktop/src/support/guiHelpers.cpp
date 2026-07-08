#include "guiHelpers.hpp"
#include "pageMetrics.hpp"
#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QtMath>
#include <cmath>

namespace stencil::gui {

  QString panelToggleQss() {
    return QStringLiteral(
        "QToolButton{background:rgba(70,76,94,230);border:1px solid rgba(255,255,255,42);"
        "border-radius:7px;padding:0;}"
        "QToolButton:hover{background:rgba(94,102,124,245);}");
  }

  QDialogButtonBox* makeButtonBox(QDialog* parent,
                                  QDialogButtonBox::StandardButtons buttons) {
    auto* box = new QDialogButtonBox(buttons, parent);
    QObject::connect(box, &QDialogButtonBox::accepted, parent, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, parent, &QDialog::reject);
    // Only the affirmative action (Ok/Save/Yes/Apply) gets the accent #primaryButton
    // look; otherwise a Close-/Cancel-only box auto-promotes its lone button to a CTA.
    for (QAbstractButton* btn : box->buttons()) {
      const QDialogButtonBox::ButtonRole role = box->buttonRole(btn);
      const bool primary = role == QDialogButtonBox::AcceptRole ||
                           role == QDialogButtonBox::YesRole ||
                           role == QDialogButtonBox::ApplyRole;
      btn->setObjectName(primary ? QStringLiteral("primaryButton") : QString());
      if (auto* pb = qobject_cast<QPushButton*>(btn)) {
        pb->setDefault(primary);
        pb->setAutoDefault(primary);
      }
    }
    return box;
  }

  void setColorSwatch(QAbstractButton* btn, const QColor& color) {
    if (!btn) return;
    // A compact colour-WELL whose whole surface is the colour (the browser's
    // <input type=color>), not a wide button with a tiny chip icon. Fixed size so the
    // form layout doesn't stretch it; a soft luminance-tuned outline keeps a
    // near-background colour visible on any theme.
    btn->setIcon(QIcon());
    btn->setText(QString());
    btn->setFixedSize(46, 24);
    btn->setCursor(Qt::PointingHandCursor);
    const bool lightFill = color.lightnessF() > 0.7;
    const QString outline = lightFill ? "rgba(0,0,0,0.40)" : "rgba(255,255,255,0.40)";
    const QString bg = QString("rgba(%1,%2,%3,%4)")
                           .arg(color.red()).arg(color.green()).arg(color.blue())
                           .arg(color.alphaF(), 0, 'f', 3);
    btn->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; }"
                               "QPushButton:hover { border: 1px solid palette(highlight); }")
                           .arg(bg, outline));
  }

  void fillPageSizeCombo(QComboBox* combo, bool includeCustom,
                         const QString& units) {
    if (!combo) return;
    const bool inches = (units == QLatin1String("in"));
    const double factor = inches ? 1.0 / 2.54 : 1.0;
    const QString unitLabel = inches ? QStringLiteral("in") : QStringLiteral("cm");
    // ≤2 decimals, trailing zeros trimmed ("21", "29.7", "8.27") — the shared
    // option-label contract with the browser page dropdown.
    const auto num = [](double v) {
      return QString::number(std::round(v * 100.0) / 100.0);
    };
    // Label-only re-render must never fire the callers' change handlers.
    const QSignalBlocker block(combo);
    if (combo->count() == 0) {  // first fill: items in canonical order
      if (includeCustom)
        combo->addItem(QStringLiteral("Custom…"), QStringLiteral("custom"));
      const QStringList names = QString::fromLatin1(core::pageFormatNames())
                                    .split(' ', Qt::SkipEmptyParts);
      for (const QString& n : names) combo->addItem(n, n);
    }
    for (int i = 0; i < combo->count(); ++i) {
      const QString name = combo->itemData(i).toString();
      if (name == QLatin1String("custom")) continue;  // label stays "Custom…"
      const core::PageSize ps = core::namedPageSize(name.toStdString());
      combo->setItemText(i, QString("%1 (%2 × %3 %4)")
                                .arg(name, num(ps.width * factor),
                                     num(ps.height * factor), unitLabel));
    }
  }

}
