#include "GenieGhost.hpp"

#include "../vsync/FramePulse.hpp"
#include "../motionPrefs.hpp"

#include <QElapsedTimer>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace stencil::support::genie {

  GenieGhost::GenieGhost(QWidget* parent, QPixmap shot, const QRect& win, const QRect& icon, Side side)
      : QWidget(parent), shot(std::move(shot)), win(win), icon(icon), side(side) {
    setObjectName(QStringLiteral("stencilModalGenie"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    place();
  }

  void GenieGhost::setProgress(double p) {
    warpInto(strips, QRectF(win), QRectF(icon), side, p);
    mergeStrips(strips, drawn);
    if (isTransposing()) transposeInto(drawn, drawnT);
    const QRegion now = footprint();
    update((now | painted).translated(-geometry().topLeft()));
    painted = now;
  }

  void GenieGhost::paintEvent(QPaintEvent* e) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF origin = geometry().topLeft();
    if (!isTransposing()) { drawStrips(painter, shot, drawn, origin, QRectF(e->rect())); return; }
    if (shotT.isNull()) shotT = transposed(shot.toImage());
    drawStripsAcross(painter, shotT, drawnT, origin, e->region(), devicePixelRatioF(), &scratch);
  }

  QRegion GenieGhost::footprint() const {
    QRegion out;
    for (qsizetype k = 0; k < strips.size(); k += DAMAGE_STRIPS) {
      QRectF box;
      for (qsizetype j = k; j < std::min(strips.size(), k + DAMAGE_STRIPS); ++j) box |= strips[j].target;
      if (!box.isEmpty()) out += box.toAlignedRect().adjusted(-1, -1, 1, 1);
    }
    return out;
  }

  void runGhost(GenieGhost* ghost, bool isOpening, int ms, std::function<void()> done) {
    const QEasingCurve ease = flightEase(isOpening);
    const int span = std::max(1, motionMs(ms));
    QElapsedTimer clock;
    clock.start();
    FramePulse::instance().join(ghost, [ghost, clock, span, ease, isOpening, done = std::move(done)] {
      const double t = std::min(1.0, static_cast<double>(clock.nsecsElapsed()) / 1e6 / span);
      const double k = ease.valueForProgress(t);
      ghost->setProgress(isOpening ? 1.0 - k : k);
      if (t < 1.0) return true;
      if (done) done();
      ghost->hide();
      ghost->deleteLater();
      return false;
    }, FramePulse::LATE);
  }

  bool fly(QWidget* host, const QPixmap& shot, const QRect& winGlobal, const QRect& iconGlobal,
           bool isOpening, bool asWindow, std::function<void()> done, std::optional<int> ms) {
    if (!host || shot.isNull() || !winGlobal.isValid()) return false;
    const QRect sinkGlobal = sinkOf(QRectF(iconGlobal)).toAlignedRect();
    const Side side = sideOf(QRectF(winGlobal), QRectF(sinkGlobal));
    const QPoint off = asWindow ? QPoint() : host->mapFromGlobal(QPoint(0, 0));
    auto* ghost = new GenieGhost(host, shot, winGlobal.translated(off), sinkGlobal.translated(off), side);
    if (asWindow) {
      ghost->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                                 | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
                                 | Qt::NoDropShadowWindowHint);
      ghost->setAttribute(Qt::WA_ShowWithoutActivating, true);
      ghost->setAttribute(Qt::WA_TranslucentBackground, true);
      ghost->place();
    }
    ghost->setProgress(isOpening ? 1.0 : 0.0);
    ghost->raise();
    ghost->show();
    // Painted now: a closing dialog unmaps this turn, and one deferred frame blinks. Never an opening child's:
    // that flush would also show the layout its caller measured in and is about to put back.
    if (asWindow || !isOpening) ghost->repaint();

    runGhost(ghost, isOpening, ms.value_or(isOpening ? OPEN_MS : CLOSE_MS), std::move(done));
    return true;
  }

}  // namespace stencil::support::genie
