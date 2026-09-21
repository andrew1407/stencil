#include "CanvasWidget.hpp"

#include "theme.hpp"
#include <algorithm>
#include <cmath>
#include <QEasingCurve>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QVector>

// The empty canvas: page fill + the "＋ Blank image" card (browser .idle-create-btn, layout.css).
namespace stencil::gui {

  void CanvasWidget::paintIdleCard(QPainter& p, const Palette& pal) {
    p.fillRect(rect(), pal.bgPage);
    // The clear's dust is still falling: bare page only (setIdleHintHidden).
    if (idleHintHidden) { unsetCursor(); return; }
    // The card alone is the click/cursor target, never the whole page.
    const QString label = QStringLiteral("＋ Blank image");
    constexpr int ICON_PX = 32;
    constexpr qreal GAP = 6, PAD_X = 26, PAD_Y = 16;

    QFont cardFont = p.font();
    cardFont.setPixelSize(13);
    const QFontMetricsF fm(cardFont);
    const qreal textW = fm.horizontalAdvance(label);
    const qreal textH = fm.height();
    const QRectF wr(rect());
    QRectF box(0, 0, std::max<qreal>(ICON_PX, textW) + PAD_X * 2,
               ICON_PX + GAP + textH + PAD_Y * 2);
    // Never wider/taller than the canvas it sits in, so the dashed border stays whole.
    box.setWidth(std::min(box.width(), wr.width() - 32.0));
    box.setHeight(std::min(box.height(), wr.height() - 32.0));
    box.moveCenter(wr.center());

    QColor accent = accentPrimary(accentKey);
    if (!accent.isValid()) accent = pal.textMuted;
    // The whole hover is a blend on `t` (components.css .idle-create-btn:hover + animations.css).
    const double t = idleCardHoverT;
    // --border-hint as theme.cpp derives it for %BORDER_HINT% (css/theme.css).
    const QColor borderHint = dark ? mixSrgb(QColor("#2a2a2a"), accent, 0.50)
                                    : mixSrgb(QColor(Qt::white), accent, 0.35);
    const QColor fill = mixSrgb(pal.bgControls, accent, t);
    const QColor edge = mixSrgb(borderHint, pal.borderMain, t);
    const QColor ink = mixSrgb(pal.textMain, QColor(Qt::white), t);
    // Hit-test the RESTING rect, never the lifted one, or a cursor on the bottom edge oscillates.
    idleCardRect = box;
    box.translate(0, -3.0 * t);

    // Drop shadow (0 8px 24px) as expanding rounded rects — QPainter has no blur. Lifted only.
    if (t > 0.01) {
      constexpr int LAYERS = 6;
      for (int i = LAYERS; i >= 1; --i) {
        const double spread = i * 2.4;
        QColor sh(0, 0, 0);
        sh.setAlphaF(0.05 * t * (1.0 - double(i) / (LAYERS + 1)));
        p.setPen(Qt::NoPen);
        p.setBrush(sh);
        p.drawRoundedRect(box.adjusted(-spread, -spread + 8, spread, spread + 8),
                          10 + spread, 10 + spread);
      }
    }
    // Fill and dashed stroke as two calls: one drawRoundedRect drops the vertical dashes on some Qt/macOS builds.
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(box, 10, 10);
    QPen dash(edge, 2, Qt::DashLine);
    dash.setDashPattern({3.0, 2.0});
    p.setPen(dash);
    p.setBrush(Qt::NoBrush);
    QPainterPath border;
    border.addRoundedRect(box, 10, 10);
    p.drawPath(border);

    // Glass sweep (browser layout.css ::after + @keyframes ui-shimmer).
    if (idleShimmerT >= 0.0) {
      const double w = box.width();
      const double x = box.left() - 1.35 * w + idleShimmerT * 2.7 * w;
      QLinearGradient band(x, box.top(), x + w * 0.9, box.bottom());
      QColor glass(Qt::white);
      glass.setAlphaF(dark ? 0.30 : 0.55);
      band.setColorAt(0.38, QColor(255, 255, 255, 0));
      band.setColorAt(0.50, glass);
      band.setColorAt(0.62, QColor(255, 255, 255, 0));
      p.save();
      p.setClipPath(border);
      p.setPen(Qt::NoPen);
      p.setBrush(band);
      p.drawRect(box);
      p.restore();
    }

    const QRectF content = box.adjusted(PAD_X, PAD_Y, -PAD_X, -PAD_Y);
    // iconMotion.json "image", mode "settle", hand-evaluated (see the glyph note below); IDLE_GLYPH_*
    // mirror the canon, pinned by tests/idleCardMotion.headless.cpp. A settle finishes on leave: its end IS rest.
    const double gm = idleGlyphMs;
    // The sun: keyframes 0 → −1.6, 70% → +0.3, 100% → 0 on the settle default (OutBack).
    const auto orbDy = [](double ms) {
      const double local = ms - IDLE_GLYPH_ORB_DELAY_MS;
      if (local <= 0) return IDLE_GLYPH_ORB_DROP;
      const double pct = 100.0 * local / IDLE_GLYPH_ORB_MS;
      if (pct >= 100.0) return 0.0;
      const QEasingCurve ease(QEasingCurve::OutBack);   // iconMotion.json defaults.settle
      if (pct <= 70.0) {
        const double u = ease.valueForProgress(pct / 70.0);
        return IDLE_GLYPH_ORB_DROP + (IDLE_GLYPH_ORB_OVERSHOOT - IDLE_GLYPH_ORB_DROP) * u;
      }
      return IDLE_GLYPH_ORB_OVERSHOOT
             * (1.0 - ease.valueForProgress((pct - 70.0) / 30.0));
    };
    // The ridge: stroke-dashoffset 23 → 0 is the polyline revealed from its start.
    const auto ridgeUpTo = [](double frac) {
      const QVector<QPointF> pts{{21, 15}, {16, 10}, {5, 21}};
      QPolygonF drawn;
      drawn << pts.first();
      double left = std::max(0.0, frac) * IDLE_GLYPH_RIDGE_LEN;
      for (int i = 1; i < pts.size() && left > 0.0; ++i) {
        const QPointF d = pts[i] - pts[i - 1];
        const double len = std::hypot(d.x(), d.y());
        if (left >= len) { drawn << pts[i]; left -= len; continue; }
        drawn << pts[i - 1] + d * (left / len);
        left = 0.0;
      }
      return drawn;
    };
    // The glyph grows 1.12x and rises 2px with the hover, about its own centre.
    const double iconScale = 1.0 + 0.12 * t;
    const QRectF iconBox(content.center().x() - ICON_PX / 2.0,
                         content.top() - 2.0 * t, ICON_PX, ICON_PX);
    // Stroked inline rather than via support/iconSet (that would drag Qt6::Svg into the headless
    // targets). Same 0 0 24 24 geometry + 2px stroke as iconSet.cpp / icons.js; keep in sync.
    p.save();
    p.translate(iconBox.center());
    p.scale(iconScale, iconScale);
    p.translate(-iconBox.width() / 2.0, -iconBox.height() / 2.0);
    p.scale(ICON_PX / 24.0, ICON_PX / 24.0);
    QPen glyph(ink, 2);
    glyph.setCapStyle(Qt::RoundCap);
    glyph.setJoinStyle(Qt::RoundJoin);
    p.setPen(glyph);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
    p.drawEllipse(QPointF(8.5, 8.5 + (gm >= 0 ? orbDy(gm) : 0.0)), 1.5, 1.5);
    const double ridgeT = gm < 0 ? 1.0
                                 : QEasingCurve(QEasingCurve::OutCubic)
                                       .valueForProgress(
                                           std::clamp(gm / IDLE_GLYPH_RIDGE_MS, 0.0, 1.0));
    p.drawPolyline(ridgeUpTo(ridgeT));
    p.restore();
    p.setFont(cardFont);
    p.setPen(ink);
    p.drawText(QRectF(content.left(), iconBox.bottom() + GAP, content.width(), textH),
               Qt::AlignCenter, label);
  }

}
