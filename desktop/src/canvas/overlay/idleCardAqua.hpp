#pragma once
// The empty canvas's card under the aqua skin: the glass capsule (browser css/aqua/chrome.css
// .idle-create-btn over tokens.css --aq-glass / --aq-glass-ring), the gel while pressed (:active),
// the skin's glossy `image` art and the ink with its lit underline. No lift, sweep or settle.
#include "../../support/skin/SkinLookStrategy.hpp"
#include "../../support/skinPrefs.hpp"

#include <algorithm>
#include <cmath>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>

namespace stencil::gui {

  // The card's measures as IdleCard lays it out; `arrive` is the arrival, 1 = resting.
  struct AquaCardLayout { int iconPx; qreal gap, padY, arrive; };

  // Paints the card into `box` and returns it, the hit rect.
  inline QRectF paintAquaCard(QPainter& p, const QRectF& box, const QString& label, const QFont& font,
                              const AquaCardLayout& at, bool pressed) {
    const support::SkinGlass& g = support::skinGlass();
    const int iconPx = at.iconPx;
    const qreal r = box.height() / 2;   // border-radius: 999px
    const auto capsule = [&box](const QGradientStops& stops) {
      QLinearGradient fill(box.topLeft(), box.bottomLeft());
      fill.setStops(stops);
      return fill;
    };
    p.save();
    p.setOpacity(std::clamp(at.arrive, 0.0, 1.0));   // the arrival fades; the skin stills its grow
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    // 0 1px 2px rgba(0,0,0,.22 | .3): two soft offsets stand in for the blur.
    for (const qreal grow : {1.0, 0.5}) {
      QColor sh(0, 0, 0);
      sh.setAlphaF((pressed ? 0.3 : 0.22) * 0.5);
      p.setBrush(sh);
      p.drawRoundedRect(box.adjusted(-grow, 1 - grow, grow, 1 + grow), r + grow, r + grow);
    }
    p.setBrush(capsule(pressed ? g.gelStops : g.glassStops));
    p.drawRoundedRect(box, r, r);
    // inset 0 0 0 1px: the ring sits inside the box.
    p.setPen(QPen(pressed ? g.gelEdge : g.edge, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), r - 0.5, r - 0.5);

    const QRectF icon(box.center().x() - iconPx / 2.0, box.top() + at.padY, iconPx, iconPx);
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const int perCell = std::max(1, static_cast<int>(std::lround(iconPx / 16.0 * dpr)));
    const QImage glyph = support::skinLook().iconImage(QStringLiteral("image"), support::skinDark(), perCell);
    if (!glyph.isNull()) p.drawImage(icon, glyph);

    const QFontMetricsF fm(font);
    const QRectF text(box.left(), icon.bottom() + at.gap, box.width(), fm.height());
    p.setFont(font);
    // text-shadow 0 1px: white at .6 on the glass, black at .3 on the gel.
    p.setPen(pressed ? QColor(0, 0, 0, 77) : QColor(255, 255, 255, 153));
    p.drawText(text.translated(0, 1), Qt::AlignCenter, label);
    p.setPen(pressed ? g.onGel : g.ink);
    p.drawText(text, Qt::AlignCenter, label);
    p.restore();
    return box;
  }

}  // namespace stencil::gui
