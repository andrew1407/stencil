#pragma once
// The pieces a dialog's reveal is built from: the flight-ownership flag, the on-screen anchor test,
// the ghost that flies between two rects, the surface dust it breaks into and the centre it lands
// on. Private to the modalReveal TUs; the public surface stays modalReveal.hpp.
#include "modalReveal.hpp"
#include "imageAnchor.hpp"
#include "ModalBackdrop.hpp"
#include "DisintegrateOverlay.hpp"

#include <QAbstractAnimation>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDialog>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRect>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <memory>

namespace stencil::support {

  // One surface ceiling across the app: a dialog's cloud is a surface like any other.
  static_assert(DIALOG_DUST_MAX_CELLS == gui::DisintegrateOverlay::SURFACE_MAX_CELLS,
                "a dialog's mote budget is the shared surface ceiling");

  // Set once a call site or the watcher owns the dialog's flight.
  constexpr const char* REVEALED_PROPERTY = "stencilDialogRevealed";
  // The slide flight (no dust): the browser's modalFromIcon / modalToIcon curves, 1.5x quicker.
  constexpr int OPEN_MS = 300;
  constexpr int CLOSE_MS = 360;
  QEasingCurve cssBezier(double x1, double y1, double x2, double y2) {
    QEasingCurve c(QEasingCurve::BezierSpline);
    c.addCubicBezierSegment(QPointF(x1, y1), QPointF(x2, y2), QPointF(1, 1));
    return c;
  }
  QEasingCurve openEase() { return cssBezier(0.22, 0.61, 0.36, 1); }    // overlays.css modalFromIcon
  QEasingCurve closeEase() { return cssBezier(0.45, 0.05, 0.6, 0.9); }  // overlays.css modalToIcon
  // Dialog clocks run 1.5x the shared surface clock; the close another 1.5x on top.
  constexpr int DIALOG_DUST_IN_MS = gui::DisintegrateOverlay::SURFACE_IN_MS * 3 / 2;
  constexpr int DIALOG_DUST_OUT_MS = gui::DisintegrateOverlay::SURFACE_OUT_MS * 9 / 4;

  // Flight origin/target in GLOBAL coords: the icon, else out of the host window's top edge (the
  // browser's flight.js: max(48px, 30%) above the viewport); with no host, above the dialog.
  QRect originRect(QWidget* anchor, const QRect& target, const QRect& anchorRect = QRect(),
                   const QWidget* host = nullptr) {
    if (anchor && anchor->isVisible() && anchor->width() > 0 && anchor->height() > 0)
      return QRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    if (anchorRect.isValid() && anchorRect.width() > 0 && anchorRect.height() > 0)
      return anchorRect;
    const QSize small(qMax(target.width() / 4, 40), qMax(target.height() / 4, 32));
    const int top = host ? host->mapToGlobal(QPoint(0, 0)).y() : target.top();
    const int centreY = top - qMax(48, int(target.height() * 0.3));
    return QRect(QPoint(target.center().x() - small.width() / 2, centreY - small.height() / 2), small);
  }

  bool anchorOnScreen(QWidget* anchor, const QRect& anchorRect) {
    if (anchor && anchor->isVisible() && anchor->width() > 0 && anchor->height() > 0) return true;
    return anchorRect.isValid() && anchorRect.width() > 0 && anchorRect.height() > 0;
  }

  // Exit target once the opener is gone: the open-image flow's own canvas box (imageAnchor.hpp).
  QRect canvasHomeRect(QWidget* host) {   // invalid = keep the caller's own fallback
    QWidget* canvas =
        host ? host->findChild<QWidget*>(QLatin1String(gui::CANVAS_VIEWPORT_NAME)) : nullptr;
    if (!canvas || !canvas->isVisible() || canvas->width() < 1 || canvas->height() < 1)
      return QRect();
    return gui::canvasAnchorRect(canvas);
  }

  // A CHILD of the main window, never a top-level: per-frame moves of a real window go
  // through the window server and stutter, and a snapshot has no layout to fight.
  // A flight reaching past the host (a tall dialog over the window's top edge) is clipped by it, and
  // one under a floating chat passes beneath it: there the ghost is a window, as the dust is.
  QLabel* makeGhost(QWidget* host, const QPixmap& shot, const QRect& globalAt, const QRect& otherEnd,
                    const QWidget* surface) {
    auto* ghost = new QLabel(host);
    ghost->setObjectName(QStringLiteral("stencilModalGhost"));  // findable by the GUI tests
    ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
    const QRect hostBox(host->mapToGlobal(QPoint(0, 0)), host->size());
    const QRect flight = globalAt.united(otherEnd);
    if (gui::DisintegrateOverlay::floatOver(host, flight, surface)
        || (!hostBox.contains(flight) && QGuiApplication::platformName() != QLatin1String("offscreen"))) {
      ghost->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                            | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
                            | Qt::NoDropShadowWindowHint);
      ghost->setAttribute(Qt::WA_ShowWithoutActivating, true);
      ghost->setAttribute(Qt::WA_TranslucentBackground, true);
    }
    ghost->setScaledContents(true);
    ghost->setPixmap(shot);
    ghost->setGeometry(ghost->isWindow() ? globalAt
                                         : QRect(host->mapFromGlobal(globalAt.topLeft()), globalAt.size()));
    ghost->raise();
    ghost->show();
    return ghost;
  }

  // The fade rides the size's own curve end to end, so the window brightens as it magnifies.
  void flyGhost(QLabel* ghost, QWidget* host, const QRect& fromGlobal, const QRect& toGlobal,
                int ms, double fromOpacity, double toOpacity,
                const QEasingCurve& easing, std::function<void()> done) {
    const bool own = ghost->isWindow();
    const QRect from = own ? fromGlobal : QRect(host->mapFromGlobal(fromGlobal.topLeft()), fromGlobal.size());
    const QRect to = own ? toGlobal : QRect(host->mapFromGlobal(toGlobal.topLeft()), toGlobal.size());
    QObject* fadeTarget = ghost;
    if (own) {
      ghost->setWindowOpacity(fromOpacity);
    } else {
      auto* effect = new QGraphicsOpacityEffect(ghost);
      effect->setOpacity(fromOpacity);
      ghost->setGraphicsEffect(effect);
      fadeTarget = effect;
    }

    auto* geo = new QPropertyAnimation(ghost, "geometry", ghost);
    geo->setDuration(ms);
    geo->setStartValue(from);
    geo->setEndValue(to);
    geo->setEasingCurve(easing);
    auto* fade = new QPropertyAnimation(fadeTarget, own ? "windowOpacity" : "opacity", ghost);
    fade->setDuration(ms);
    fade->setStartValue(fromOpacity);
    fade->setEndValue(toOpacity);
    fade->setEasingCurve(easing);

    auto* group = new QParallelAnimationGroup(ghost);
    group->addAnimation(geo);
    group->addAnimation(fade);
    QObject::connect(group, &QParallelAnimationGroup::finished, ghost, [ghost, done] {
      if (done) done();
      ghost->deleteLater();
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
  }

  // Browser twin: js/ui/motion.js surfaceIn / surfaceOut. The ghost stays as the
  // fallback for anything the dust declines, so a window never simply blinks.
  // `surface` is the window the flight forms, never counted as a float over it.
  bool flySurfaceDust(QWidget* host, const QPixmap& shot, const QRect& windowGlobal,
                      const QRect& iconGlobal, bool opening, const QColor& ink, const QWidget* surface) {
    if (!host || shot.isNull() || !windowGlobal.isValid()) return false;
    const QRect box(host->mapFromGlobal(windowGlobal.topLeft()), windowGlobal.size());
    const QPoint point = host->mapFromGlobal(iconGlobal.center());
    const bool floats = gui::DisintegrateOverlay::floatOver(host, windowGlobal.united(iconGlobal), surface);
    auto* fx = gui::DisintegrateOverlay::overSurface(shot, box, host, point, opening,
                                                     opening ? DIALOG_DUST_IN_MS : DIALOG_DUST_OUT_MS, ink,
                                                     DIALOG_DUST_MAX_CELLS,
                                                     /*escapeHost=*/true, /*alwaysEscape=*/floats);
    // Painted NOW: the dialog unmaps this turn, and one deferred frame is the blink.
    if (fx && !opening) fx->repaint();
    return fx != nullptr;
  }

  // Motes are lifted towards the window's text colour (DisintegrateOverlay::SURFACE_INK_MIX).
  QColor inkOf(const QWidget& w) { return w.palette().color(QPalette::WindowText); }

  void fadeUpBehindDust(QWidget* w) { gui::fadeUpBehindDust(w, DIALOG_DUST_IN_MS); }

  // Null only for an unparented dialog. Requiring the target to fit inside the host
  // silently turned the effect off for ordinary centred dialogs — do not add that.
  QWidget* hostFor(const QDialog& dlg) {
    QWidget* parent = dlg.parentWidget();
    if (!parent) return nullptr;
    QWidget* host = parent->window();
    return (host && host->isVisible()) ? host : nullptr;
  }

  // The GLOBAL point a dialog that claimed a DialogLanding opens with its frame's top-left on.
  constexpr const char* LANDING_PROPERTY = "stencilDialogLanding";
  bool claimLanding(QDialog& dlg);   // modalRevealFlight.hpp

  // The dialog's FRAME on its host's client centre (the browser's viewport), or its top-left on its
  // landing, on the host's screen.
  void centreOnHost(QDialog& dlg) {
    const QWidget* host = dialogHost(&dlg);
    if (!host) return;
    const QScreen* s = host->screen();
    const QRect avail = s ? s->availableGeometry() : QRect();
    const QSize box = dlg.frameGeometry().size();
    const QVariant landing = dlg.property(LANDING_PROPERTY);
    dlg.move(landing.isValid()
                 ? topLeftAt(landing.toPoint(), box, avail)
                 : centredTopLeft(host->mapToGlobal(QPoint(host->width() / 2, host->height() / 2)), box, avail));
  }

  // exec() centres on the host less a GUESSED window frame (10x40 on macOS, where no window has a
  // side border). That placement is the first move after a Show; it is redone on the real frame.
  class DialogCentreFilter : public QObject {
   public:
    explicit DialogCentreFilter(QObject* parent) : QObject(parent) {
      setObjectName(QStringLiteral("stencilDialogCentreFilter"));
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      auto* dlg = qobject_cast<QDialog*>(o);
      if (!dlg || !dlg->isWindow() || dlg->testAttribute(Qt::WA_DontShowOnScreen))
        return QObject::eventFilter(o, e);
      if (e->type() == QEvent::Show && !e->spontaneous() && !dlg->testAttribute(Qt::WA_Moved)) {
        if (claimLanding(*dlg)) centreOnHost(*dlg);   // at once: not every platform moves it after a Show
        placing = dlg;
        QPointer<QDialog> shown(dlg);   // no move at all: Qt found it already there
        QTimer::singleShot(0, this, [this, shown] { if (placing == shown) placing.clear(); });
      } else if (e->type() == QEvent::Move && placing == dlg) {
        placing.clear();
        centreOnHost(*dlg);
      }
      return QObject::eventFilter(o, e);
    }

   private:
    QPointer<QDialog> placing;   // shown, its placement not yet made
  };
}  // namespace stencil::support
