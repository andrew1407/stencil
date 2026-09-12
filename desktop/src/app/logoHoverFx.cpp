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
}  // namespace stencil::gui

