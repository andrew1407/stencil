#include "guiHelpers.hpp"

#include "theme.hpp"
#include "iconSet.hpp"
#include "modalChrome.hpp"   // confirmModal — the browser-styled yes/no question
#include "modalReveal.hpp"   // support::motionReduced()
#include "pageMetrics.hpp"
#include <QAbstractButton>
#include <QBuffer>
#include <QGuiApplication>
#include <QColor>
#include <QComboBox>
#include <QMenu>
#include <QDialog>
#include <QEasingCurve>
#include <QEvent>
#include <QFileDialog>
#include <QIcon>
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

  static const QString ICON_SPIN = QStringLiteral("stencilIconSpin");

  QString inlineIconHtml(const QString& name, const QColor& color, int px,
                         const QString& style, qreal dpr) {
    if (px <= 0 || !hasIcon(name)) return QString();
    const qreal ratio = dpr > 0 ? dpr : (qApp ? qApp->devicePixelRatio() : qreal(1));
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    // pixmap(w, h) asks for device pixels and hands back the raster scaled DOWN to px;
    // the width/height attributes scale the px·dpr raster back on screen (Retina).
    themedIcon(name, color, px, ratio)
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
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return QString();
    return dlg.selectedFiles().first();
  }

  bool confirmYesNo(QWidget* parent, const QString& title, const QString& text) {
    // The browser's styled confirm (modalChrome confirmModal), never a native QMessageBox.
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
    // Only the affirmative action gets the accent CTA look (theme.cpp accentCta); a
    // Close-/Cancel-only box promotes its lone button.
    for (QAbstractButton* btn : box->buttons()) {
      const QDialogButtonBox::ButtonRole role = box->buttonRole(btn);
      const bool primary = role == QDialogButtonBox::AcceptRole ||
                           role == QDialogButtonBox::YesRole ||
                           role == QDialogButtonBox::ApplyRole;
      if (primary) btn->setProperty("accentCta", true);
      if (auto* pb = qobject_cast<QPushButton*>(btn)) {
        pb->setDefault(primary);
        pb->setAutoDefault(primary);
        // Browser parity: ✓ on the affirmative CTA, ✕ on Cancel/Close (confirmModal / .app-modal-close).
        if (primary)
          pb->setIcon(labelIcon("check", QColor("#ffffff"), 14));
        else if (box->buttonRole(pb) == QDialogButtonBox::RejectRole)
          pb->setIcon(labelIcon("x", parent->palette().color(QPalette::WindowText), 14));
      }
    }
    return box;
  }

  void spinIcon(QAbstractButton* btn, const QString& name, const QColor& color, int size,
                qreal fromDeg, qreal toDeg, int ms) {
    if (!btn) return;
    for (QVariantAnimation* old : btn->findChildren<QVariantAnimation*>(ICON_SPIN)) {
      old->stop();
      old->deleteLater();
    }
    auto paint = [btn, name, color, size](qreal deg) {
      btn->setIcon(rotatedIcon(name, color, size, deg));
    };
    // Reduced motion lands on the end state at once — the angle IS the panel's state.
    if (ms <= 0 || support::motionReduced()) { paint(toDeg); return; }
    auto* anim = new QVariantAnimation(btn);
    anim->setObjectName(ICON_SPIN);
    anim->setDuration(ms);
    anim->setEasingCurve(QEasingCurve::OutCubic);   // the extent slides' curve
    anim->setStartValue(fromDeg);
    anim->setEndValue(toDeg);
    QObject::connect(anim, &QVariantAnimation::valueChanged, btn,
                     [paint](const QVariant& v) { paint(v.toReal()); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  void fitMenuWidth(QMenu& menu) {
    int label = 0;
    for (QAction* a : menu.actions())
      label = std::max(label, menu.fontMetrics().horizontalAdvance(a->text()));
    // 6 left pad + 16 icon + ~8 icon-text gap + 10 right pad + menu pads/margins.
    menu.setFixedWidth(label + 52);
  }

  void compactIconMenu(QMenu& menu) {
    menu.setStyleSheet(compactMenuQss());   // shared with MenuHotkeyChips' compact mode
    fitMenuWidth(menu);
  }

  void fillPageSizeCombo(QComboBox* combo, bool includeCustom,
                         const QString& units) {
    if (!combo) return;
    const bool inches = (units == QLatin1String("in"));
    const double factor = inches ? 1.0 / 2.54 : 1.0;
    const QString unitLabel = inches ? QStringLiteral("in") : QStringLiteral("cm");
    // ≤2 decimals, trailing zeros trimmed — the option-label contract with the browser dropdown.
    const auto num = [](double v) {
      return QString::number(std::round(v * 100.0) / 100.0);
    };
    // A label-only re-render must never fire the callers' change handlers.
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

