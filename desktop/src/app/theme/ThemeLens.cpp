#include "ThemeLens.hpp"

#include "motionPrefs.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <algorithm>
#include <cmath>
#include <utility>

namespace stencil::gui {

  namespace {
    constexpr int FRAME_MS = 16;
  }  // namespace

  double ThemeLens::radiusAt(double ms) {
    if (ms >= GROW_MS) return RADIUS;
    return RADIUS * (1 - std::pow(1 - std::max(0.0, ms) / GROW_MS, 3));
  }

  ThemeLens::ThemeLens(QWidget* host, QPixmap other, const QRect& picture)
      : QWidget(host), shot(std::move(other)) {
    setObjectName(QStringLiteral("themeLens"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    resize(2 * REACH, 2 * REACH);
    setMask(QRegion(rect(), QRegion::Ellipse));   // a move repaints the two discs, not their squares
    if (!picture.isEmpty()) {
      QPainter p(&shot);
      p.setCompositionMode(QPainter::CompositionMode_Difference);   // white minus each opaque pixel
      p.fillRect(picture, Qt::white);
    }
    hide();
    if (support::motionReduced()) return;
    radius = 0;
    connect(&ticker, &QTimer::timeout, this, [this] {
      if (closingFrom >= 0) {
        radius = closingFrom * (1 - radiusAt(clock.elapsed() * double(GROW_MS) / CLOSE_MS) / RADIUS);
        if (radius <= 0) {
          ticker.stop();
          hide();
          if (auto done = std::exchange(closed, {})) done();
          deleteLater();
          return;
        }
      } else {
        radius = radiusAt(clock.elapsed());
        if (radius >= RADIUS) ticker.stop();
      }
      update();
    });
  }

  // The first follow shows the lens, and its circle opens from that instant.
  void ThemeLens::follow(const QPoint& global) {
    move(parentWidget()->mapFromGlobal(global) - QPoint(REACH, REACH));
    raise();
    if (!isVisible() && radius < RADIUS) {
      clock.start();
      ticker.start(FRAME_MS);
    }
    show();
  }

  void ThemeLens::dismiss(std::function<void()> done) {
    if (closingFrom >= 0) return;
    if (support::motionReduced() || !isVisible() || radius <= 0) {
      delete this;
      if (done) done();
      return;
    }
    closed = std::move(done);
    closingFrom = radius;
    clock.restart();
    ticker.start(FRAME_MS);
  }

  void ThemeLens::paintEvent(QPaintEvent*) {
    if (radius <= 0) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF centre(REACH, REACH);
    QPainterPath disc;
    disc.addEllipse(centre, radius, radius);
    p.setClipPath(disc);
    p.drawPixmap(-pos(), shot);
    p.setClipping(false);
    // Two passes, as the compare divider's, so the rim reads over either theme.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 110), 3.0));
    p.drawEllipse(centre, radius, radius);
    p.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
    p.drawEllipse(centre, radius, radius);
  }

}  // namespace stencil::gui
