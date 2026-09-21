#include "LogoHoverFx.hpp"
#include "modalReveal.hpp"   // support::motionReduced()

#include <QEvent>
#include <QPainter>
#include <QCursor>
#include <QRadialGradient>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <cmath>

namespace stencil::gui {

  void LogoHoverFx::paintEvent(QPaintEvent*) {
    if (pm.isNull()) return;
    const bool anim = active();   // hovering: pulse/glow/rays; at rest: just the mark
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF c = QRectF(rect()).center();   // == the button's icon centre
    const QColor accent = this->accent();
    const QSizeF mark(logo->iconSize());        // logical px; pixmap carries the dpr
    const qreal beat = anim ? this->beat : 0.0;       // no breath / lift / glow at rest
    const qreal lift = 2.0 * beat;               // translateY(-2px) at the peak
    const qreal scale = 1.0 + 0.12 * beat;       // scale(1.12) at the peak
    const QPointF mc(c.x(), c.y() - lift);       // the levitating mark's centre
    // Accent glow, radial falloff — the CSS drop-shadow halo. Hover-only.
    if (anim) {
      const qreal r = mark.width() * 0.5 * scale + 2.0 + 5.0 * this->beat;
      QRadialGradient g(mc, r);
      QColor g0 = accent; g0.setAlphaF(0.25 + 0.55 * this->beat);
      QColor g1 = accent; g1.setAlphaF(0.0);
      g.setColorAt(0.0, g0);
      g.setColorAt(0.55, g0);   // solid to the mark's edge, then fall off
      g.setColorAt(1.0, g1);
      p.setPen(Qt::NoPen);
      p.setBrush(g);
      p.drawEllipse(mc, r, r);
    }
    // 8 spokes, two strokes each (soft halo + bright core) standing in for the CSS conic gradient.
    if (anim) {
      const qreal alpha = 0.14 + 0.26 * this->beat;
      const qreal r1 = mark.width() * 0.5 + 3.0;
      const qreal r2 = r1 + 3.5;
      QColor soft = accent;   soft.setAlphaF(alpha * 0.45);
      QColor bright = accent; bright.setAlphaF(alpha);
      for (int i = 0; i < 8; ++i) {
        const qreal a = qDegreesToRadians(angle + i * 45.0);
        const QPointF dir(std::cos(a), std::sin(a));
        p.setPen(QPen(soft, 3.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c + dir * r1, c + dir * r2);
        p.setPen(QPen(bright, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c + dir * r1, c + dir * r2);
      }
    }
    // The mark itself — the button's icon is blanked while the loop runs.
    const QSizeF s(mark.width() * scale, mark.height() * scale);
    p.drawPixmap(QRectF(mc.x() - s.width() / 2, mc.y() - s.height() / 2,
                        s.width(), s.height()),
                 pm, QRectF(pm.rect()));
  }

  void LogoHoverFx::start() {
    // Reduced motion: no loop; the button keeps the plain mark (faceSwap / filterFade rule).
    if (active() || support::motionReduced()) return;
    beat = 0.0;   // every hover begins at the loop's rest pose
    pm = makePixmap();
    blankButtonIcon();
    syncGeometry();
    raise();
    if (box && box->isVisible()) stackUnder(box);   // the glow never paints over the popover
    show();
    pulse->start();
    spin->start();
    setProperty("fxActive", true);
  }

  void LogoHoverFx::stop() {
    if (!active()) return;
    pulse->stop();
    spin->stop();
    setProperty("fxActive", false);
    showStatic();   // keep painting the resting mark (the button icon stays blanked)
  }

  // The resting mark, never at the mercy of QToolButton's icon rendering.
  void LogoHoverFx::showStatic() {
    if (!logo || !logo->isVisible()) { hide(); return; }
    beat = 0.0;
    pm = makePixmap();
    blankButtonIcon();
    syncGeometry();
    raise();
    show();
    update();
  }

  void LogoHoverFx::blankButtonIcon() {
    QPixmap b(logo->iconSize());
    b.fill(Qt::transparent);
    logo->setIcon(QIcon(b));
  }

  void LogoHoverFx::syncGeometry() {
    const QPoint tl = logo->mapTo(parentWidget(), QPoint(0, 0));
    setGeometry(QRect(tl, logo->size()).adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN));
    // Fullscreen's edge reveal SLIDES the toolbars' height, so the logo is clipped without a Hide event; visibleRegion() is the honest question.
    if (logo->visibleRegion().isEmpty()) hide();
  }
}  // namespace stencil::gui

