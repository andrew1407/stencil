#include "faceSwap.hpp"

namespace stencil::gui {

  double faceEaseInCubic(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * u;
  }

  // The browser's cubic-bezier(0.16, 1, 0.3, 1) in spirit: nearly all the distance up front.
  double faceEaseOutExpo(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * u);
  }

  // Pure. Both ends are the face at rest and the pivot is invisible; the turn's SIGN
  // flips across the pivot (+115° out, -115° in), which reads as one continuous turn.
  FaceSwapFrame faceSwapFrame(double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (t < kFaceSwapPivot) {
      const double u = faceEaseInCubic(t / kFaceSwapPivot);
      return {false, 1.0 - u, kFaceSwapTurnDeg * u,
              1.0 - (1.0 - kFaceSwapMinScale) * u};
    }
    const double u = faceEaseOutExpo((t - kFaceSwapPivot) / (1.0 - kFaceSwapPivot));
    return {true, u, -kFaceSwapTurnDeg * (1.0 - u),
            kFaceSwapMinScale + (1.0 - kFaceSwapMinScale) * u};
  }

  // `gap` px of transparent air on the right (FaceSpec::gapPx).
  QIcon detail::withGap(const QIcon& base, int size, int gap) {
    if (base.isNull() || gap <= 0) return base;
    const qreal dpr = qApp ? qApp->devicePixelRatio() : qreal(1);
    const QPixmap src = base.pixmap(QSize(size, size), dpr);
    QPixmap out(QSize(int(std::lround((size + gap) * dpr)), int(std::lround(size * dpr))));
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawPixmap(QPointF(0, 0), src);
    p.end();
    return QIcon(out);
  }

  // rotatedIcon bakes the colour into the SVG and QColor::name() drops alpha, so the
  // fade and the scale are composited here.
  QIcon detail::faceIcon(const FaceSpec& f, const FaceSwapFrame& fr) {
    const int size = std::max(1, f.iconSize);
    const QIcon base = std::abs(fr.deg) < 0.01
                           ? themedIcon(f.glyph, f.glyphColor, size)
                           : rotatedIcon(f.glyph, f.glyphColor, size, fr.deg);
    if (base.isNull()) return base;
    if (fr.alpha >= 0.999 && fr.scale >= 0.999) return withGap(base, size, f.gapPx);
    const qreal dpr = qApp ? qApp->devicePixelRatio() : qreal(1);
    const QPixmap src = base.pixmap(QSize(size, size), dpr);
    QPixmap out(src.size());
    out.setDevicePixelRatio(src.devicePixelRatio());
    out.fill(Qt::transparent);
    {
      QPainter p(&out);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      p.setOpacity(std::clamp(fr.alpha, 0.0, 1.0));
      const QPointF c(size / 2.0, size / 2.0);
      p.translate(c);
      p.scale(fr.scale, fr.scale);
      p.translate(-c);
      p.drawPixmap(QPointF(0, 0), src);
    }
    return QIcon(out);
  }

  void detail::repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
  }

  // A widget stylesheet is the only lever over the app-wide `QToolButton[...] { color }`
  // rules; every state is listed so a hover/press mid-swap cannot outrank it.
  void detail::setLabelAlpha(QAbstractButton* btn, const QColor& color, double alpha) {
    if (!color.isValid()) return;
    // Quantised: every write re-polishes the widget.
    const double q = std::lround(std::clamp(alpha, 0.0, 1.0) * 50.0) / 50.0;
    const QString rgba = QStringLiteral("rgba(%1,%2,%3,%4)")
                             .arg(color.red())
                             .arg(color.green())
                             .arg(color.blue())
                             .arg(q, 0, 'f', 2);
    const bool already = btn->property(kFaceSwappingProperty).toBool();
    if (already && btn->property(kFaceLabelColorProperty).toString() == rgba) return;
    btn->setProperty(kFaceLabelColorProperty, rgba);
    btn->setProperty(kFaceSwappingProperty, true);
    btn->setStyleSheet(
        QStringLiteral("QToolButton[%1=\"true\"],QToolButton[%1=\"true\"]:hover,"
                       "QToolButton[%1=\"true\"]:pressed,QToolButton[%1=\"true\"]:disabled,"
                       "QPushButton[%1=\"true\"],QPushButton[%1=\"true\"]:hover"
                       "{color:%2;}")
            .arg(QString::fromLatin1(kFaceSwappingProperty), rgba));
    // Qt matches property selectors at POLISH time.
    if (!already) repolish(btn);
  }

  void detail::clearLabelAlpha(QAbstractButton* btn) {
    if (!btn->property(kFaceSwappingProperty).toBool()) return;
    btn->setProperty(kFaceSwappingProperty, false);
    btn->setProperty(kFaceLabelColorProperty, QString());
    btn->setStyleSheet(btn->property(kFaceBaseSheetProperty).toString());
    repolish(btn);   // …and the frame that turns it off, for the same reason
  }

  void detail::rememberFace(QAbstractButton* btn, const FaceSpec& f) {
    btn->setProperty(kFaceGlyphProperty, f.glyph);
    btn->setProperty(kFaceLabelProperty, f.label);
    btn->setProperty(kFaceGlyphColorProperty, f.glyphColor);
    btn->setProperty(kFaceTextColorProperty, f.textColor);
    btn->setProperty(kFaceIconSizeProperty, f.iconSize);
    btn->setProperty(kFaceGapProperty, f.gapPx);
  }

  // `known` is false the first time, when there is no face to leave from.
  FaceSpec detail::paintedFace(const QAbstractButton* btn, bool* known) {
    FaceSpec f;
    f.glyph = btn->property(kFaceGlyphProperty).toString();
    *known = !f.glyph.isEmpty();
    f.label = btn->property(kFaceLabelProperty).toString();
    f.glyphColor = btn->property(kFaceGlyphColorProperty).value<QColor>();
    f.textColor = btn->property(kFaceTextColorProperty).value<QColor>();
    f.iconSize = btn->property(kFaceIconSizeProperty).toInt();
    f.gapPx = btn->property(kFaceGapProperty).toInt();
    return f;
  }

  void detail::paintFace(QAbstractButton* btn, const FaceSpec& f, const FaceSwapFrame& fr) {
    btn->setIcon(faceIcon(f, fr));
    if (!f.label.isNull()) btn->setText(f.label);
    setLabelAlpha(btn, f.textColor, fr.alpha);
  }

  void detail::settleFace(QAbstractButton* btn, const FaceSpec& f) {
    btn->setIcon(withGap(themedIcon(f.glyph, f.glyphColor, std::max(1, f.iconSize)),
                         std::max(1, f.iconSize), f.gapPx));
    if (!f.label.isNull()) btn->setText(f.label);
    clearLabelAlpha(btn);
    rememberFace(btn, f);
  }
}  // namespace stencil::gui
