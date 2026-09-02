#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "modalChrome.hpp"   // confirmModal — the browser-styled yes/no question
#include "modalReveal.hpp"   // support::motionReduced()
#include "pageMetrics.hpp"
#include <QAbstractButton>
#include <QBuffer>
#include <QGuiApplication>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QEasingCurve>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVariantAnimation>
#include <QtMath>
#include <cmath>

namespace stencil::gui {

  // Object name that marks (and lets us cancel) an in-flight spinIcon animation.
  static const QString kIconSpin = QStringLiteral("stencilIconSpin");

  QString inlineIconHtml(const QString& name, const QColor& color, int px,
                         const QString& style, qreal dpr) {
    if (px <= 0 || !hasIcon(name)) return QString();
    const qreal ratio = dpr > 0 ? dpr : (qApp ? qApp->devicePixelRatio() : qreal(1));
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    // The dpr-AWARE pixmap overload: pixmap(w, h) asks for device pixels and would
    // hand back the raster scaled DOWN to px, throwing the Retina detail away. The
    // width/height attributes below scale the px·dpr raster back to px on screen.
    themedIcon(name, color, px, /*shadow=*/false, ratio)
        .pixmap(QSize(px, px), ratio)
        .toImage()
        .save(&buf, "PNG");
    return QStringLiteral("<img src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%3\"%4>")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(px)
        .arg(px)
        .arg(style.isEmpty() ? QString()
                             : QStringLiteral(" style=\"%1\"").arg(style));
  }

  QString panelToggleQss() {
    return QStringLiteral(
        "QToolButton{background:rgba(70,76,94,230);border:1px solid rgba(255,255,255,42);"
        "border-radius:7px;padding:0;outline:none;}"
        "QToolButton:hover{background:rgba(94,102,124,245);}");
  }

  QString showSaveDialog(QWidget* parent, const QString& title,
                         const QString& suggested, const QString& filter) {
    QFileDialog dlg(parent, title, suggested, filter);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return QString();
    return dlg.selectedFiles().first();
  }

  bool confirmYesNo(QWidget* parent, const QString& title, const QString& text) {
    // The browser's styled confirm (modalChrome confirmModal), not a native
    // QMessageBox — every yes/no question in the app wears the same shell.
    ConfirmSpec spec;
    spec.title = title;
    spec.message = text;
    return confirmModal(parent, spec);
  }

  QDialogButtonBox* makeButtonBox(QDialog* parent,
                                  QDialogButtonBox::StandardButtons buttons) {
    auto* box = new QDialogButtonBox(buttons, parent);
    QObject::connect(box, &QDialogButtonBox::accepted, parent, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, parent, &QDialog::reject);
    // Only the affirmative action (Ok/Save/Yes/Apply) gets the accent CTA look
    // (theme.cpp QPushButton[accentCta="true"]); otherwise a Close-/Cancel-only box
    // auto-promotes its lone button to a CTA.
    for (QAbstractButton* btn : box->buttons()) {
      const QDialogButtonBox::ButtonRole role = box->buttonRole(btn);
      const bool primary = role == QDialogButtonBox::AcceptRole ||
                           role == QDialogButtonBox::YesRole ||
                           role == QDialogButtonBox::ApplyRole;
      if (primary) btn->setProperty("accentCta", true);
      if (auto* pb = qobject_cast<QPushButton*>(btn)) {
        pb->setDefault(primary);
        pb->setAutoDefault(primary);
        // Browser parity: every confirm-style button wears its glyph — ✓ on the
        // affirmative CTA (white on the accent fill), ✕ on Cancel/Close (the
        // text colour; browser confirmModal / .app-modal-close do the same).
        if (primary)
          pb->setIcon(themedIcon("check", QColor("#ffffff"), 14));
        else if (box->buttonRole(pb) == QDialogButtonBox::RejectRole)
          pb->setIcon(themedIcon("x", parent->palette().color(QPalette::WindowText), 14));
      }
    }
    return box;
  }

  void spinIcon(QAbstractButton* btn, const QString& name, const QColor& color, int size,
                qreal fromDeg, qreal toDeg, int ms) {
    if (!btn) return;
    for (QVariantAnimation* old : btn->findChildren<QVariantAnimation*>(kIconSpin)) {
      old->stop();
      old->deleteLater();
    }
    auto paint = [btn, name, color, size](qreal deg) {
      btn->setIcon(rotatedIcon(name, color, size, deg));
    };
    // Reduced motion lands on the end state at once — the angle IS the panel's state,
    // so only the turn is dropped (faceSwap / filterFade rule).
    if (ms <= 0 || support::motionReduced()) { paint(toDeg); return; }
    auto* anim = new QVariantAnimation(btn);
    anim->setObjectName(kIconSpin);
    anim->setDuration(ms);
    anim->setEasingCurve(QEasingCurve::OutCubic);   // the extent slides' curve
    anim->setStartValue(fromDeg);
    anim->setEndValue(toDeg);
    QObject::connect(anim, &QVariantAnimation::valueChanged, btn,
                     [paint](const QVariant& v) { paint(v.toReal()); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
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
    // QAbstractButton, not QPushButton: a QSS type selector does NOT match sibling
    // classes, so the open-image dialog's QToolButton swatch rendered unstyled
    // (an invisible white chip on a white dialog).
    btn->setStyleSheet(QString("QAbstractButton { background: %1; border: 1px solid %2; border-radius: 6px; }"
                               "QAbstractButton:hover { border: 1px solid palette(highlight); }")
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
