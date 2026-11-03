#pragma once
// The genie flight: a snapshot poured into (or out of) its icon along genieWarp.hpp's funnel, on a
// ghost that spans both. The Sliding motion mode's window open/close (support/modal/modalRevealFlight.hpp).
// Browser port: js/ui/motion/genie/flight.js.
#include "genieWarp.hpp"
#include "../vsync/FramePulse.hpp"
#include "../motionPrefs.hpp"   // support::motionMs

#include <QEasingCurve>
#include <QElapsedTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QRegion>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <utility>

namespace stencil::support::genie {

  // The tuned lengths at the default speed (ms): the open is the unhurried half.
  inline constexpr int OPEN_MS = 560;
  inline constexpr int CLOSE_MS = 480;
  // Room past the union of window and icon, so the funnel's rounding never clips (px).
  inline constexpr int MARGIN_PX = 4;
  // Strips per damage rect: a frame repaints the funnel's footprint, not the ghost's whole box, so the
  // widgets underneath (the canvas, a sliding dock) repaint only where the picture was or now is.
  inline constexpr int DAMAGE_STRIPS = 8;
  // A fold's space beside its flight: closing, it waits for the picture's far edge to start moving (the warp's
  // LAG through the close's InOutSine, a time share); the flight outlasts the slide so it covers the landing.
  inline constexpr double CLOSE_SPACE_WAIT = 0.4;
  inline constexpr int SPACE_TAIL_MS = 40;
  struct SpaceClock { int delayMs; int ms; };
  inline SpaceClock spaceClock(bool opening, int ms) {
    const int wait = opening ? 0 : int(ms * CLOSE_SPACE_WAIT);
    return {wait, ms - wait};
  }

  class GenieGhost : public QWidget {
   public:
    // `win` and `icon` are in the coordinates of the ghost's parent (global for a window ghost).
    GenieGhost(QWidget* parent, QPixmap shot, const QRect& win, const QRect& icon, Side side)
        : QWidget(parent), shot(std::move(shot)), win(win), icon(icon), side(side) {
      setObjectName(QStringLiteral("stencilModalGenie"));
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setAttribute(Qt::WA_NoSystemBackground);
      place();
    }
    QRect span() const { return win.united(icon).adjusted(-MARGIN_PX, -MARGIN_PX, MARGIN_PX, MARGIN_PX); }
    void place() { setGeometry(span()); }
    // A target that moves while the space beside it slides: re-read every frame (parent coordinates).
    void setFollow(std::function<QRect()> at) { follow = std::move(at); }
    // Only ever grows while following: a box resized every frame repaints all of it, and a window ghost's resize
    // is a native one.
    void setProgress(double p) {
      if (const QRect r = follow ? follow() : QRect(); r.isValid() && r != icon) {
        icon = r;
        if (!geometry().contains(span())) setGeometry(geometry().united(span()));
      }
      warpInto(strips, QRectF(win), QRectF(icon), side, p);
      const QRegion now = footprint();
      update((now | painted).translated(-geometry().topLeft()));
      painted = now;
    }

   protected:
    void paintEvent(QPaintEvent* e) override {
      QPainter painter(this);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      const QPointF origin = geometry().topLeft();
      const QSizeF px = shot.size();
      const QRectF dirty(e->rect());
      for (const Strip& s : strips) {
        // Overlapped half a pixel, so no seam shows between bands at a fractional scale.
        const QRectF at = s.target.translated(-origin).adjusted(-0.25, -0.25, 0.25, 0.25);
        if (!at.intersects(dirty)) continue;
        const QRectF from(s.source.x() * px.width(), s.source.y() * px.height(),
                          s.source.width() * px.width(), s.source.height() * px.height());
        painter.drawPixmap(at, shot, from);
      }
    }

   private:
    // The strips' cover in parent coordinates, DAMAGE_STRIPS to a rect, a pixel past the seam overlap.
    QRegion footprint() const {
      QRegion out;
      for (qsizetype k = 0; k < strips.size(); k += DAMAGE_STRIPS) {
        QRectF box;
        for (qsizetype j = k; j < std::min(strips.size(), k + DAMAGE_STRIPS); ++j) box |= strips[j].target;
        if (!box.isEmpty()) out += box.toAlignedRect().adjusted(-1, -1, 1, 1);
      }
      return out;
    }

    QPixmap shot;
    QRect win;
    QRect icon;
    Side side;
    std::function<QRect()> follow;
    QVector<Strip> strips;
    QRegion painted;   // the last frame's footprint, cleared by the next
  };

  // The flight's curve, which a fold's space slide shares so the two land together.
  inline QEasingCurve flightEase(bool opening) { return opening ? QEasingCurve::OutSine : QEasingCurve::InOutSine; }

  // Steps `ghost` end to end on the app's frame pulse (support/vsync), after the space beside it, on
  // the clock alone so a slow frame never slows the flight; then runs `done` and retires the ghost.
  inline void runGhost(GenieGhost* ghost, bool opening, int ms, std::function<void()> done) {
    const QEasingCurve ease = flightEase(opening);
    const int span = std::max(1, motionMs(ms));
    QElapsedTimer clock;
    clock.start();
    FramePulse::instance().join(ghost, [ghost, clock, span, ease, opening, done] {
      const double t = std::min(1.0, double(clock.nsecsElapsed()) / 1e6 / span);
      const double k = ease.valueForProgress(t);
      ghost->setProgress(opening ? 1.0 - k : k);
      if (t < 1.0) return true;
      if (done) done();
      ghost->hide();
      ghost->deleteLater();
      return false;
    }, FramePulse::LATE);
  }

  // Plays the flight on `host` (child ghost) or as its own frameless window (`asWindow`, rects
  // GLOBAL then); false, with nothing shown, for no host or snapshot. `done` runs once.
  // `ms` 0 = the window lengths, OPEN_MS / CLOSE_MS (a popup list passes its own, shorter one).
  // `follow` (GLOBAL), when set, re-reads the icon every frame: a fold's toggle rides its sliding space.
  inline bool fly(QWidget* host, const QPixmap& shot, const QRect& winGlobal, const QRect& iconGlobal,
                  bool opening, bool asWindow, std::function<void()> done, int ms = 0,
                  std::function<QRect()> follow = {}) {
    if (!host || shot.isNull() || !winGlobal.isValid()) return false;
    const QRect sinkGlobal = sinkOf(QRectF(iconGlobal)).toAlignedRect();
    const Side side = sideOf(QRectF(winGlobal), QRectF(sinkGlobal));
    const auto offset = [host, asWindow] { return asWindow ? QPoint() : host->mapFromGlobal(QPoint(0, 0)); };
    const QPoint off = offset();
    auto* ghost = new GenieGhost(host, shot, winGlobal.translated(off), sinkGlobal.translated(off), side);
    if (asWindow) {
      ghost->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                                 | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
                                 | Qt::NoDropShadowWindowHint);
      ghost->setAttribute(Qt::WA_ShowWithoutActivating, true);
      ghost->setAttribute(Qt::WA_TranslucentBackground, true);
      ghost->place();
    }
    if (follow)
      ghost->setFollow([offset, follow = std::move(follow)] {
        const QRect r = follow();
        return r.isValid() ? sinkOf(QRectF(r)).toAlignedRect().translated(offset()) : QRect();
      });
    ghost->setProgress(opening ? 1.0 : 0.0);
    ghost->raise();
    ghost->show();
    // Painted now: a closing dialog unmaps this turn, and one deferred frame blinks. Never an opening child's:
    // that flush would also show the layout its caller measured in and is about to put back.
    if (asWindow || !opening) ghost->repaint();

    runGhost(ghost, opening, ms > 0 ? ms : (opening ? OPEN_MS : CLOSE_MS), std::move(done));
    return true;
  }

}  // namespace stencil::support::genie
