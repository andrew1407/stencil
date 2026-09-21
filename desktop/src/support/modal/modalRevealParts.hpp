#pragma once
// The pieces a dialog's reveal is built from: the flight-ownership flag, the on-screen anchor test,
// the ghost that flies between two rects and the surface dust it breaks into. Private to the
// modalReveal TUs; the public surface stays modalReveal.hpp.
#include "modalReveal.hpp"
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
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

namespace stencil::support {

  // One surface ceiling across the app: a dialog's cloud is a surface like any other.
  static_assert(DIALOG_DUST_MAX_CELLS == gui::DisintegrateOverlay::SURFACE_MAX_CELLS,
                "a dialog's mote budget is the shared surface ceiling");

  // Set once a call site or the watcher owns the dialog's flight.
  constexpr const char* REVEALED_PROPERTY = "stencilDialogRevealed";
  // Dialog clocks run 1.5x the shared surface clock; the close another 1.5x on top.
  constexpr int OPEN_MS = 450;
  constexpr int CLOSE_MS = 360 * 3 / 2;
  constexpr int DIALOG_DUST_IN_MS = gui::DisintegrateOverlay::SURFACE_IN_MS * 3 / 2;
  constexpr int DIALOG_DUST_OUT_MS = gui::DisintegrateOverlay::SURFACE_OUT_MS * 9 / 4;

  // Flight origin/target in GLOBAL coords: the icon, else a small box above the dialog.
  QRect originRect(QWidget* anchor, const QRect& target, const QRect& anchorRect = QRect()) {
    if (anchor && anchor->isVisible() && anchor->width() > 0 && anchor->height() > 0)
      return QRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    if (anchorRect.isValid() && anchorRect.width() > 0 && anchorRect.height() > 0)
      return anchorRect;
    const QSize small(qMax(target.width() / 4, 40), qMax(target.height() / 4, 32));
    const int above = target.top() - qMax(48, target.height() / 3) - small.height();
    return QRect(QPoint(target.center().x() - small.width() / 2, above), small);
  }

  bool anchorOnScreen(QWidget* anchor, const QRect& anchorRect) {
    if (anchor && anchor->isVisible() && anchor->width() > 0 && anchor->height() > 0) return true;
    return anchorRect.isValid() && anchorRect.width() > 0 && anchorRect.height() > 0;
  }

  // Exit target once the opener is gone: the canvas. Browser twin: ui/base.js canvasHomeRect.
  QRect canvasHomeRect(QWidget* host) {
    QWidget* canvas =
        host ? host->findChild<QWidget*>(QStringLiteral("canvasViewport")) : nullptr;
    if (!canvas || !canvas->isVisible() || canvas->width() < 1 || canvas->height() < 1)
      return QRect();
    constexpr int HOME_PX = 40;   // a small box, so the shrink reads as collapsing INTO it
    const QRect g(canvas->mapToGlobal(QPoint(0, 0)), canvas->size());
    return QRect(g.center() - QPoint(HOME_PX / 2, HOME_PX / 2), QSize(HOME_PX, HOME_PX));
  }

  // A CHILD of the main window, never a top-level: per-frame moves of a real window go
  // through the window server and stutter, and a snapshot has no layout to fight.
  QLabel* makeGhost(QWidget* host, const QPixmap& shot, const QRect& globalAt) {
    auto* ghost = new QLabel(host);
    ghost->setObjectName(QStringLiteral("stencilModalGhost"));  // findable by the GUI tests
    ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
    ghost->setScaledContents(true);
    ghost->setPixmap(shot);
    ghost->setGeometry(QRect(host->mapFromGlobal(globalAt.topLeft()), globalAt.size()));
    ghost->raise();
    ghost->show();
    return ghost;
  }

  // `hold` keeps the box solid while small so the eye follows a window, not a fade.
  void flyGhost(QLabel* ghost, QWidget* host, const QRect& fromGlobal, const QRect& toGlobal,
                int ms, double fromOpacity, double toOpacity, double hold,
                QEasingCurve::Type easing, std::function<void()> done) {
    const QRect from(host->mapFromGlobal(fromGlobal.topLeft()), fromGlobal.size());
    const QRect to(host->mapFromGlobal(toGlobal.topLeft()), toGlobal.size());
    auto* fx = new QGraphicsOpacityEffect(ghost);
    fx->setOpacity(fromOpacity);
    ghost->setGraphicsEffect(fx);

    auto* geo = new QPropertyAnimation(ghost, "geometry", ghost);
    geo->setDuration(ms);
    geo->setStartValue(from);
    geo->setEndValue(to);
    geo->setEasingCurve(easing);
    auto* fade = new QPropertyAnimation(fx, "opacity", ghost);
    fade->setDuration(ms);
    fade->setKeyValueAt(0.0, fromOpacity);
    fade->setKeyValueAt(hold, qMax(fromOpacity, toOpacity));
    fade->setKeyValueAt(1.0, toOpacity);

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
  bool flySurfaceDust(QWidget* host, const QPixmap& shot, const QRect& windowGlobal,
                      const QRect& iconGlobal, bool opening, const QColor& ink) {
    if (!host || shot.isNull() || !windowGlobal.isValid()) return false;
    const QRect box(host->mapFromGlobal(windowGlobal.topLeft()), windowGlobal.size());
    const QPoint point = host->mapFromGlobal(iconGlobal.center());
    auto* fx = gui::DisintegrateOverlay::overSurface(shot, box, host, point, opening,
                                                     opening ? DIALOG_DUST_IN_MS : DIALOG_DUST_OUT_MS, ink,
                                                     DIALOG_DUST_MAX_CELLS,
                                                     /*escapeHost=*/true);
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
}  // namespace stencil::support
