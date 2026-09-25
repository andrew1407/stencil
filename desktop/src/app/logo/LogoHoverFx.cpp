#include "LogoHoverFx.hpp"

#include <QEvent>
#include <QPainter>
#include <QCursor>
#include <QRadialGradient>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <cmath>

namespace stencil::gui {

  // Hover: ×1.12 breath + 2px lift on a 1.2s beat, a glow on the same beat, 8 spokes turning once per 8s.
  LogoHoverFx::LogoHoverFx(QToolButton* logo, std::function<QPixmap()> makePixmap,
                           std::function<QColor()> accent)
      : QWidget(logo->window()), logo(logo),
        makePixmap(std::move(makePixmap)), accent(std::move(accent)) {
    setObjectName(QStringLiteral("logoHoverFx"));   // findable by the GUI test
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    pulse = new QVariantAnimation(this);   // one 1.2s breath per loop (logoPulse)
    pulse->setStartValue(0.0);
    pulse->setEndValue(1.0);
    pulse->setDuration(1200);
    pulse->setLoopCount(-1);
    QObject::connect(pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      // 0 → 1 → 0 across the beat.
      beat = 0.5 - 0.5 * std::cos(qDegreesToRadians(360.0 * v.toReal()));
      setProperty("pulseBeat", beat);   // observable by the GUI test
      syncGeometry();                    // tracks the button between ticks too
      update();
    });
    spin = new QVariantAnimation(this);    // one ray revolution per 8s (logoRaysSpin)
    spin->setStartValue(0.0);
    spin->setEndValue(360.0);
    spin->setDuration(8000);
    spin->setLoopCount(-1);
    QObject::connect(spin, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      angle = v.toReal();
      setProperty("raysAngle", angle);
      update();
    });
    grace = new QTimer(this);
    grace->setSingleShot(true);
    grace->setInterval(150);
    QObject::connect(grace, &QTimer::timeout, this, [this] {
      if (!hoveredAnywhere()) stop();
    });
    setProperty("fxActive", false);
    hide();
    this->logo->installEventFilter(this);
    watchAncestors();
    // Paint the mark at ALL times: QToolButton draws its icon at half size on Retina. Deferred so the button is laid out first.
    QTimer::singleShot(0, this, [this] { showStatic(); });
  }

  // The mark is painted in the WINDOW's frame, and a shell between them moving (the editor
  // beside a docked chat) carries the button along without a Move of its own.
  void LogoHoverFx::watchAncestors() {
    for (const QPointer<QWidget>& w : ancestors)
      if (w) w->removeEventFilter(this);
    ancestors.clear();
    for (QWidget* w = logo->parentWidget(); w && w != logo->window(); w = w->parentWidget()) {
      w->installEventFilter(this);
      ancestors.append(w);
    }
  }

  void LogoHoverFx::holdWhile(QWidget* box) {
    if (this->box) this->box->removeEventFilter(this);
    this->box = box;
    if (this->box) this->box->installEventFilter(this);
  }

  void LogoHoverFx::standDown(bool on) {
    if (on) {
      stop();
      hide();
    } else {
      showStatic();
    }
  }

  void LogoHoverFx::themeChanged() {
    pm = makePixmap();
    blankButtonIcon();
    update();
  }

  bool LogoHoverFx::active() const { return hovering; }

  bool LogoHoverFx::eventFilter(QObject* o, QEvent* e) {
    if (o != logo && o != box && (e->type() == QEvent::Move || e->type() == QEvent::Resize)) {
      if (isVisible()) syncGeometry();   // an ancestor shifted under the button
      return QWidget::eventFilter(o, e);
    }
    if (o == logo) {
      // The button is built on the window and joins the toolbar afterwards.
      if (e->type() == QEvent::ParentChange) watchAncestors();
      switch (e->type()) {
        case QEvent::Enter:
          grace->stop();
          if (logo->isEnabled()) start();
          break;
        case QEvent::Leave:
          if (box && box->isVisible()) leaveSoon(); else stop();
          break;
        case QEvent::Hide:
        case QEvent::EnabledChange:
        case QEvent::WindowDeactivate:
          // Mirror ShimmerOverlay: the button hiding / a modal opening mid-hover stops the LOOP.
          grace->stop();
          stop();
          // The RESTING mark goes with the button too — stop() returns early with no loop running.
          if (!logo || !logo->isVisible()) hide();
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
    } else if (box && o == box) {
      switch (e->type()) {
        case QEvent::Enter:
          grace->stop();
          if (logo->isEnabled() && logo->isVisible()) start();
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
    if (active()) grace->start();
  }

  bool LogoHoverFx::hoveredAnywhere() const {
    // Qt delivers the logo's Leave before the popover's Enter, so the cursor position is the arbiter.
    const auto under = [](const QWidget* w) {
      return w->isVisible() &&
             (w->underMouse() || w->rect().contains(w->mapFromGlobal(QCursor::pos())));
    };
    return under(logo) || (box && under(box));
  }
}  // namespace stencil::gui

