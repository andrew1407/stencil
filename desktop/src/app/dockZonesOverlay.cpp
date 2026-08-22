#include "dockZonesOverlay.hpp"

#include <QEasingCurve>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <QVariantAnimation>

namespace stencil::gui {

  namespace {
    constexpr int kZoneBand = 56;      // band thickness
    constexpr int kZoneInset = 8;      // inset from the area edges
    constexpr int kZoneNudgePx = 4;    // chevron travel (±px, browser chatZoneNudge)
    constexpr int kZoneNudgeMs = 1400; // full out-and-back cycle (0.7 s each way)
  }  // namespace

  DockZonesOverlay::DockZonesOverlay(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("chatDockZones"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    nudgeAnim_ = new QVariantAnimation(this);
    nudgeAnim_->setDuration(kZoneNudgeMs);
    nudgeAnim_->setLoopCount(-1);  // for the whole drag
    nudgeAnim_->setEasingCurve(QEasingCurve::InOutSine);
    nudgeAnim_->setKeyValueAt(0.0, -double(kZoneNudgePx));
    nudgeAnim_->setKeyValueAt(0.5, double(kZoneNudgePx));
    nudgeAnim_->setKeyValueAt(1.0, -double(kZoneNudgePx));
    connect(nudgeAnim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) {
              nudge_ = v.toReal();
              update();
            });
    watchdog_ = new QTimer(this);
    watchdog_->setInterval(120);
    connect(watchdog_, &QTimer::timeout, this, [this] {
      // Fail-safe: the overlay exists only while the drag poll runs — it can
      // never linger after a release, whatever events got swallowed.
      if (stillDragging_ && !stillDragging_()) hide();
    });
    hide();
  }

  void DockZonesOverlay::beginDrag(const QColor& accent, const QRect& targetRect,
                                   std::function<bool()> stillDragging) {
    accent_ = accent;
    stillDragging_ = std::move(stillDragging);
    hover_ = -1;
    setGeometry(targetRect);
    raise();
    show();
    update();
  }

  void DockZonesOverlay::dragTo(const QPoint& globalPos) {
    const int z = zoneAt(globalPos);
    if (z != hover_) {
      hover_ = z;
      update();
    }
  }

  int DockZonesOverlay::zoneAt(const QPoint& globalPos) const {
    const QPoint p = mapFromGlobal(globalPos);
    const QRect r = rect();
    if (!r.contains(p)) return -1;
    const int reach = kZoneInset + kZoneBand;
    const int d[4] = {p.x(), r.width() - p.x(), p.y(), r.height() - p.y()};
    int best = -1;
    for (int i = 0; i < 4; ++i)
      if (d[i] <= reach && (best < 0 || d[i] < d[best])) best = i;
    return best;
  }

  Qt::DockWidgetArea DockZonesOverlay::area(int zone) {
    switch (zone) {
      case 0: return Qt::LeftDockWidgetArea;
      case 1: return Qt::RightDockWidgetArea;
      case 2: return Qt::TopDockWidgetArea;
      default: return Qt::BottomDockWidgetArea;
    }
  }

  void DockZonesOverlay::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    nudgeAnim_->start();
    watchdog_->start();
  }

  void DockZonesOverlay::hideEvent(QHideEvent* e) {
    nudgeAnim_->stop();
    watchdog_->stop();
    QWidget::hideEvent(e);
  }

  void DockZonesOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < 4; ++i) {
      const bool hot = i == hover_;
      const QRect z = zoneRect(i);
      QColor fill = accent_;
      fill.setAlpha(hot ? 97 : 51);    // ~38% targeted / ~20% rest
      QColor stroke = accent_;
      stroke.setAlpha(hot ? 255 : 179);  // full / ~70%
      p.setPen(QPen(stroke, 2, Qt::DashLine));
      p.setBrush(fill);
      p.drawRoundedRect(z, 10, 10);
      drawChevron(p, i, z, hot);
    }
  }

  // NON-overlapping bands: top/bottom span the width; left/right fill the
  // space BETWEEN them (no corner intersections).
  QRect DockZonesOverlay::zoneRect(int i) const {
    const QRect r = rect().adjusted(kZoneInset, kZoneInset, -kZoneInset, -kZoneInset);
    const int vTop = r.top() + kZoneBand + kZoneInset;
    const int vH = r.height() - 2 * (kZoneBand + kZoneInset);
    switch (i) {
      case 0: return {r.left(), vTop, kZoneBand, vH};
      case 1: return {r.right() - kZoneBand, vTop, kZoneBand, vH};
      case 2: return {r.left(), r.top(), r.width(), kZoneBand};
      default: return {r.left(), r.bottom() - kZoneBand, r.width(), kZoneBand};
    }
  }

  void DockZonesOverlay::drawChevron(QPainter& p, int i, const QRect& z, bool hot) {
    QPoint ctr = z.center();
    const int a = 7;  // chevron arm
    // Continuous nudge toward the zone's edge (±kZoneNudgePx, animated).
    const int off = qRound(nudge_);
    QPoint pts[3];
    switch (i) {
      case 0:
        ctr.rx() -= off;
        pts[0] = {ctr.x() + a, ctr.y() - a}; pts[1] = {ctr.x() - a, ctr.y()};
        pts[2] = {ctr.x() + a, ctr.y() + a};
        break;
      case 1:
        ctr.rx() += off;
        pts[0] = {ctr.x() - a, ctr.y() - a}; pts[1] = {ctr.x() + a, ctr.y()};
        pts[2] = {ctr.x() - a, ctr.y() + a};
        break;
      case 2:
        ctr.ry() -= off;
        pts[0] = {ctr.x() - a, ctr.y() + a}; pts[1] = {ctr.x(), ctr.y() - a};
        pts[2] = {ctr.x() + a, ctr.y() + a};
        break;
      default:
        ctr.ry() += off;
        pts[0] = {ctr.x() - a, ctr.y() - a}; pts[1] = {ctr.x(), ctr.y() + a};
        pts[2] = {ctr.x() + a, ctr.y() - a};
        break;
    }
    // Subtle dark halo under the accent stroke for legibility.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 90), 4.5, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
    p.drawPolyline(pts, 3);
    QColor c = accent_;
    c.setAlpha(hot ? 255 : 230);
    p.setPen(QPen(c, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(pts, 3);
  }

}  // namespace stencil::gui
