#include "guiHelpers.hpp"
#include <QAbstractButton>
#include <QColor>
#include <QDialog>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QtMath>

namespace stencil::gui {

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
    // A rounded chip with a soft contrasting outline reads as a color well (the
    // browser's <input type=color>) rather than a flat square — and the border
    // keeps a near-background color (e.g. white tint on a light theme) visible.
    const int dpr = btn->devicePixelRatio() > 0 ? qCeil(btn->devicePixelRatio()) : 1;
    QPixmap chip(QSize(20, 20) * dpr);
    chip.setDevicePixelRatio(dpr);
    chip.fill(Qt::transparent);
    QPainter pr(&chip);
    pr.setRenderHint(QPainter::Antialiasing, true);
    // Outline tuned from the fill's luminance so it shows on any swatch color.
    const bool lightFill = color.lightnessF() > 0.7;
    pr.setPen(QPen(lightFill ? QColor(0, 0, 0, 60) : QColor(255, 255, 255, 90), 1));
    pr.setBrush(color);
    pr.drawRoundedRect(QRectF(1.0, 1.0, 18.0, 18.0), 5.0, 5.0);
    pr.end();
    btn->setIcon(QIcon(chip));
  }

}
