#include "iconInline.hpp"

#include "iconSet.hpp"
#include "modalReveal.hpp"   // support::motionReduced()
#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QColor>
#include <QEasingCurve>
#include <QVariantAnimation>

namespace stencil::gui {

  namespace {
    const QString ICON_SPIN = QStringLiteral("stencilIconSpin");
  }  // namespace

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
    anim->setDuration(support::motionMs(ms));
    anim->setEasingCurve(QEasingCurve::OutCubic);   // the extent slides' curve
    anim->setStartValue(fromDeg);
    anim->setEndValue(toDeg);
    QObject::connect(anim, &QVariantAnimation::valueChanged, btn,
                     [paint](const QVariant& v) { paint(v.toReal()); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}
