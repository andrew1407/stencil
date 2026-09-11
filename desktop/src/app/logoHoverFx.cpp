#include "logoHoverFx.hpp"
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

  // While hovered the mark breathes to ×1.12 and floats up 2px on a 1.2s
  // ease-in-out beat, an accent glow brightens on the same beat, and a ring of
  // 8 accent spokes turns once per 8s while shimmering 0.14 ↔ 0.4 on that beat.
  LogoHoverFx::LogoHoverFx(QToolButton* logo, std::function<QPixmap()> makePixmap,
                           std::function<QColor()> accent)
      : QWidget(logo->window()), logo_(logo),
        makePixmap_(std::move(makePixmap)), accent_(std::move(accent)) {
    setObjectName(QStringLiteral("logoHoverFx"));   // findable by the GUI test
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    pulse_ = new QVariantAnimation(this);   // one 1.2s breath per loop (logoPulse)
    pulse_->setStartValue(0.0);
    pulse_->setEndValue(1.0);
    pulse_->setDuration(1200);
    pulse_->setLoopCount(-1);
    QObject::connect(pulse_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      // Raw loop time → an ease-in-out up-and-down: 0 → 1 → 0 across the beat.
      beat_ = 0.5 - 0.5 * std::cos(qDegreesToRadians(360.0 * v.toReal()));
      setProperty("pulseBeat", beat_);   // observable by the GUI test
      syncGeometry();                    // tracks the button between ticks too
      update();
    });
    spin_ = new QVariantAnimation(this);    // one ray revolution per 8s (logoRaysSpin)
    spin_->setStartValue(0.0);
    spin_->setEndValue(360.0);
    spin_->setDuration(8000);
    spin_->setLoopCount(-1);
    QObject::connect(spin_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      angle_ = v.toReal();
      setProperty("raysAngle", angle_);
      update();
    });
    grace_ = new QTimer(this);
    grace_->setSingleShot(true);
    grace_->setInterval(150);
    QObject::connect(grace_, &QTimer::timeout, this, [this] {
      if (!hoveredAnywhere()) stop();
    });
    setProperty("fxActive", false);
    hide();
    logo_->installEventFilter(this);
    // Paint the mark at ALL times, not only on hover: QToolButton draws its icon at half
    // size on Retina, so the RESTING logo looked tiny beside the full-size hover fx
    //. Only the pulse / glow / rays stay hover-gated. Deferred so
    // the button is laid out first.
    QTimer::singleShot(0, this, [this] { showStatic(); });
  }

  void LogoHoverFx::holdWhile(QWidget* box) {
    if (box_) box_->removeEventFilter(this);
    box_ = box;
    if (box_) box_->installEventFilter(this);
  }

  void LogoHoverFx::themeChanged() {
    // Refresh the cached art whether hovering or at rest — the resting mark is ours too now.
    pm_ = makePixmap_();
    blankButtonIcon();
    update();
  }

  bool LogoHoverFx::active() const { return pulse_->state() == QAbstractAnimation::Running; }

  bool LogoHoverFx::eventFilter(QObject* o, QEvent* e) {
    if (o == logo_) {
      switch (e->type()) {
        case QEvent::Enter:
          grace_->stop();
          if (logo_->isEnabled()) start();
          break;
        case QEvent::Leave:
          // Bound for the open popover? Hold; the grace check settles it.
          if (box_ && box_->isVisible()) leaveSoon(); else stop();
          break;
        case QEvent::Hide:
        case QEvent::EnabledChange:
        case QEvent::WindowDeactivate:
          // Mirror ShimmerOverlay: the button hiding / a modal opening mid-hover
          // stops the LOOP (stop() falls back to the resting mark, or hides if the
          // button itself went away).
          grace_->stop();
          stop();
          // …and the RESTING mark goes with the button. stop() returns early when no loop
          // was running, so a logo hidden while the overlay merely SAT there (fullscreen
          // hides the header row) left the mark floating over whatever took its place —
          // it covered the label beside it.
          if (!logo_ || !logo_->isVisible()) hide();
          break;
        case QEvent::Move:
        case QEvent::Resize:
          if (isVisible()) syncGeometry();   // track the button as the toolbar reflows
          break;
        case QEvent::Show:
          showStatic();                      // the button came back — repaint the mark
          break;
        default:
          break;
      }
    } else if (box_ && o == box_) {
      switch (e->type()) {
        case QEvent::Enter:
          grace_->stop();
          if (logo_->isEnabled() && logo_->isVisible()) start();
          break;
        case QEvent::Leave:
        case QEvent::Hide:
          leaveSoon();
          break;
        default:
          break;
      }
    }
    return QWidget::eventFilter(o, e);
  }

  void LogoHoverFx::leaveSoon() {
    if (active()) grace_->start();
  }

  bool LogoHoverFx::hoveredAnywhere() const {
    // underMouse() lags a synthetic Enter, and Qt delivers the logo's Leave before the
    // popover's Enter — so the cursor position is the arbiter, as the Alt-glide poll does.
    const auto under = [](const QWidget* w) {
      return w->isVisible() &&
             (w->underMouse() || w->rect().contains(w->mapFromGlobal(QCursor::pos())));
    };
    return under(logo_) || (box_ && under(box_));
  }

  void LogoHoverFx::paintEvent(QPaintEvent*) {
    if (pm_.isNull()) return;
    const bool anim = active();   // hovering: pulse/glow/rays; at rest: just the mark
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF c = QRectF(rect()).center();   // == the button's icon centre
    const QColor accent = accent_();
    const QSizeF mark(logo_->iconSize());        // logical px; pixmap carries the dpr
    const qreal beat = anim ? beat_ : 0.0;       // no breath / lift / glow at rest
    const qreal lift = 2.0 * beat;               // translateY(-2px) at the peak
    const qreal scale = 1.0 + 0.12 * beat;       // scale(1.12) at the peak
    const QPointF mc(c.x(), c.y() - lift);       // the levitating mark's centre
    // Accent glow behind the mark, brightening on the beat. Radial soft falloff —
    // the opaque mark covers the middle, so it reads as the CSS drop-shadow halo.
    // Glow + rays are hover-only; at rest just the mark is painted.
    if (anim) {
      const qreal r = mark.width() * 0.5 * scale + 2.0 + 5.0 * beat_;
      QRadialGradient g(mc, r);
      QColor g0 = accent; g0.setAlphaF(0.25 + 0.55 * beat_);
      QColor g1 = accent; g1.setAlphaF(0.0);
      g.setColorAt(0.0, g0);
      g.setColorAt(0.55, g0);   // solid to the mark's edge, then fall off
      g.setColorAt(1.0, g1);
      p.setPen(Qt::NoPen);
      p.setBrush(g);
      p.drawEllipse(mc, r, r);
    }
    // Ray ring: 8 thin spokes just outside the mark, turning with spin_ while
    // the ring shimmers on the SAME beat (logoRaysShimmer). Two strokes per
    // spoke — a wide soft halo under a thin bright core — stand in for the CSS
    // conic gradient's feathered edges.
    if (anim) {
      const qreal alpha = 0.14 + 0.26 * beat_;
      const qreal r1 = mark.width() * 0.5 + 3.0;
      const qreal r2 = r1 + 3.5;
      QColor soft = accent;   soft.setAlphaF(alpha * 0.45);
      QColor bright = accent; bright.setAlphaF(alpha);
      for (int i = 0; i < 8; ++i) {
        const qreal a = qDegreesToRadians(angle_ + i * 45.0);
        const QPointF dir(std::cos(a), std::sin(a));
        p.setPen(QPen(soft, 3.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c + dir * r1, c + dir * r2);
        p.setPen(QPen(bright, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c + dir * r1, c + dir * r2);
      }
    }
    // The mark itself, pulsing + levitating (the only copy on screen — the
    // button's icon is blanked while the loop runs).
    const QSizeF s(mark.width() * scale, mark.height() * scale);
    p.drawPixmap(QRectF(mc.x() - s.width() / 2, mc.y() - s.height() / 2,
                        s.width(), s.height()),
                 pm_, QRectF(pm_.rect()));
  }

  void LogoHoverFx::start() {
    // Reduced motion: no loop at all — it is pure hover feedback with no end state to
    // reach, and the button keeps painting the plain mark (faceSwap / filterFade rule).
    if (active() || support::motionReduced()) return;
    beat_ = 0.0;   // every hover begins at the loop's rest pose
    pm_ = makePixmap_();
    blankButtonIcon();
    syncGeometry();
    raise();
    if (box_ && box_->isVisible()) stackUnder(box_);   // the glow never paints over the popover
    show();
    pulse_->start();
    spin_->start();
    setProperty("fxActive", true);
  }

  void LogoHoverFx::stop() {
    if (!active()) return;
    pulse_->stop();
    spin_->stop();
    setProperty("fxActive", false);
    showStatic();   // keep painting the resting mark (the button icon stays blanked)
  }

  // The resting mark — no animation, just the pixmap centred at full size, so the logo is
  // never at the mercy of QToolButton's icon rendering.
  void LogoHoverFx::showStatic() {
    if (!logo_ || !logo_->isVisible()) { hide(); return; }
    beat_ = 0.0;
    pm_ = makePixmap_();
    blankButtonIcon();
    syncGeometry();
    raise();
    show();
    update();
  }

  void LogoHoverFx::blankButtonIcon() {
    QPixmap b(logo_->iconSize());
    b.fill(Qt::transparent);
    logo_->setIcon(QIcon(b));
  }

  void LogoHoverFx::syncGeometry() {
    const QPoint tl = logo_->mapTo(parentWidget(), QPoint(0, 0));
    setGeometry(QRect(tl, logo_->size()).adjusted(-kMargin, -kMargin, kMargin, kMargin));
    // …and the mark goes wherever the button has gone. Fullscreen's edge reveal SLIDES the
    // toolbars' height instead of hiding them, so the logo is clipped away without a Hide
    // event ever arriving — the mark hung on over whatever the collapsed row uncovered,
    // and nothing could take it down. visibleRegion() is the
    // honest question: is any of the button actually on screen?
    if (logo_->visibleRegion().isEmpty()) hide();
  }

}  // namespace stencil::gui
