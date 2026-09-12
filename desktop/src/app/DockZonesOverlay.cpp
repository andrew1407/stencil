#include "DockZonesOverlay.hpp"

#include <QEasingCurve>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTimer>
#include <QVariantAnimation>

namespace stencil::gui {

  namespace {
    constexpr int ZONE_BAND = 56;      // band thickness
    constexpr int ZONE_INSET = 8;      // inset from the area edges
    constexpr int ZONE_NUDGE_PX = 4;    // chevron travel (±px, browser chatZoneNudge)
    constexpr int ZONE_NUDGE_MS = 1400; // full out-and-back cycle (0.7 s each way)

    // Blur by smooth-scaling down and up — Qt has no backdrop filter; /4 lands close to the browser's blur(2px).
    QPixmap blurred(const QPixmap& src) {
      const QSize small = src.size() / 4;
      if (small.isEmpty()) return src;
      QPixmap out = src.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      out.setDevicePixelRatio(src.devicePixelRatio());
      return out;
    }
  }  // namespace

  DockZonesOverlay::DockZonesOverlay(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("chatDockZones"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    nudgeAnim_ = new QVariantAnimation(this);
    nudgeAnim_->setDuration(ZONE_NUDGE_MS);
    nudgeAnim_->setLoopCount(-1);  // for the whole drag
    nudgeAnim_->setEasingCurve(QEasingCurve::InOutSine);
    nudgeAnim_->setKeyValueAt(0.0, -double(ZONE_NUDGE_PX));
    nudgeAnim_->setKeyValueAt(0.5, double(ZONE_NUDGE_PX));
    nudgeAnim_->setKeyValueAt(1.0, -double(ZONE_NUDGE_PX));
    connect(nudgeAnim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) {
              nudge_ = v.toReal();
              update();
            });
    watchdog_ = new QTimer(this);
    watchdog_->setInterval(120);
    connect(watchdog_, &QTimer::timeout, this, [this] {
      // Fail-safe: the overlay can never linger after a release, whatever events got swallowed.
      if (stillDragging_ && !stillDragging_()) hide();
    });
    hide();
  }

  void DockZonesOverlay::beginDrag(const QColor& accent, const QRect& targetRect,
                                   std::function<bool()> stillDragging) {
    accent_ = accent;
    stillDragging_ = std::move(stillDragging);
    hover_ = -1;
    // Grabbed while still hidden, so the bands blur the page and not themselves.
    backdrop_ = QPixmap();
    if (QWidget* p = parentWidget()) {
      const QPixmap shot = p->grab(targetRect);
      if (!shot.isNull()) backdrop_ = blurred(shot);
    }
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
    const int reach = ZONE_INSET + ZONE_BAND;
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
    backdrop_ = QPixmap();   // a window-sized pixmap has no business outliving the drag
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
      if (!backdrop_.isNull()) {
        QPainterPath clip;
        clip.addRoundedRect(z, 10, 10);
        p.save();
        p.setClipPath(clip);
        p.drawPixmap(rect(), backdrop_);
        p.restore();
      }
      QColor fill = accent_;
      fill.setAlpha(hot ? 133 : 82);   // ~52% targeted / ~32% rest
      QColor stroke = accent_;
      stroke.setAlpha(hot ? 255 : 179);  // full / ~70%
      p.setPen(QPen(stroke, 2, Qt::DashLine));
      p.setBrush(fill);
      p.drawRoundedRect(z, 10, 10);
      drawChevron(p, i, z, hot);
    }
  }

  // NON-overlapping: top/bottom span the width; left/right fill the space BETWEEN them.
  QRect DockZonesOverlay::zoneRect(int i) const {
    const QRect r = rect().adjusted(ZONE_INSET, ZONE_INSET, -ZONE_INSET, -ZONE_INSET);
    const int vTop = r.top() + ZONE_BAND + ZONE_INSET;
    const int vH = r.height() - 2 * (ZONE_BAND + ZONE_INSET);
    switch (i) {
      case 0: return {r.left(), vTop, ZONE_BAND, vH};
      case 1: return {r.right() - ZONE_BAND, vTop, ZONE_BAND, vH};
      case 2: return {r.left(), r.top(), r.width(), ZONE_BAND};
      default: return {r.left(), r.bottom() - ZONE_BAND, r.width(), ZONE_BAND};
    }
  }

  void DockZonesOverlay::drawChevron(QPainter& p, int i, const QRect& z, bool hot) {
    QPoint ctr = z.center();
    const int a = 7;  // chevron arm
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
