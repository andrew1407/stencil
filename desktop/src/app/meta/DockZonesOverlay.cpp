#include "DockZonesOverlay.hpp"

#include <QEasingCurve>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTimer>
#include <QVariantAnimation>

namespace stencil::gui {

  namespace {
    // The browser's .chat-dock-zone boxes (css/components/chat/dockZones.css): 56 px bands 8 px in,
    // top and bottom between the side bands, a 10 px radius; DOCK_ZONE_BAND (ui/chat/geometry.js) = 72.
    constexpr int ZONE_BAND = 56;
    constexpr int ZONE_INSET = 8;
    constexpr int ZONE_RADIUS = 10;
    constexpr int DOCK_ZONE_BAND = ZONE_INSET + ZONE_BAND + ZONE_INSET;
    static_assert(DOCK_ZONE_BAND == 72, "the browser's hit band");
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
    nudgeAnim = new QVariantAnimation(this);
    nudgeAnim->setDuration(ZONE_NUDGE_MS);
    nudgeAnim->setLoopCount(-1);  // for the whole drag
    nudgeAnim->setEasingCurve(QEasingCurve::InOutSine);
    nudgeAnim->setKeyValueAt(0.0, -double(ZONE_NUDGE_PX));
    nudgeAnim->setKeyValueAt(0.5, double(ZONE_NUDGE_PX));
    nudgeAnim->setKeyValueAt(1.0, -double(ZONE_NUDGE_PX));
    connect(nudgeAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) {
              nudge = v.toReal();
              update();
            });
    watchdog = new QTimer(this);
    watchdog->setInterval(120);
    connect(watchdog, &QTimer::timeout, this, [this] {
      // Fail-safe: the overlay can never linger after a release, whatever events got swallowed.
      if (stillDragging && !stillDragging()) hide();
    });
    hide();
  }

  void DockZonesOverlay::beginDrag(const QColor& accent, const QRect& targetRect,
                                   std::function<bool()> stillDragging) {
    this->accent = accent;
    this->stillDragging = std::move(stillDragging);
    hover = -1;
    // Grabbed while still hidden, so the bands blur the page and not themselves.
    backdrop = QPixmap();
    if (QWidget* p = parentWidget()) {
      const QPixmap shot = p->grab(targetRect);
      if (!shot.isNull()) backdrop = blurred(shot);
    }
    setGeometry(targetRect);
    raise();
    show();
    update();
  }

  void DockZonesOverlay::dragTo(const QPoint& globalPos) {
    const int z = zoneAt(globalPos);
    if (z != hover) {
      hover = z;
      update();
    }
  }

  int DockZonesOverlay::zoneAt(const QPoint& globalPos) const {
    const QPoint p = mapFromGlobal(globalPos);
    const QRect r = rect();
    if (!r.contains(p)) return -1;
    const int reach = DOCK_ZONE_BAND;
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
    nudgeAnim->start();
    watchdog->start();
  }

  void DockZonesOverlay::hideEvent(QHideEvent* e) {
    backdrop = QPixmap();   // a window-sized pixmap has no business outliving the drag
    nudgeAnim->stop();
    watchdog->stop();
    QWidget::hideEvent(e);
  }

  void DockZonesOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < 4; ++i) {
      const bool hot = i == hover;
      const QRect z = zoneRect(i);
      if (!backdrop.isNull()) {
        QPainterPath clip;
        clip.addRoundedRect(z, ZONE_RADIUS, ZONE_RADIUS);
        p.save();
        p.setClipPath(clip);
        p.drawPixmap(rect(), backdrop);
        p.restore();
      }
      QColor fill = accent;
      fill.setAlpha(hot ? 133 : 82);   // ~52% targeted / ~32% rest
      QColor stroke = accent;
      stroke.setAlpha(hot ? 255 : 179);  // full / ~70%
      p.setPen(QPen(stroke, 2, Qt::DashLine));
      p.setBrush(fill);
      p.drawRoundedRect(z, ZONE_RADIUS, ZONE_RADIUS);
      drawChevron(p, i, z, hot);
    }
  }

  // NON-overlapping: left/right run the full height, top/bottom the room between them.
  QRect DockZonesOverlay::zoneRect(int i) const {
    const QRect r = rect().adjusted(ZONE_INSET, ZONE_INSET, -ZONE_INSET, -ZONE_INSET);
    const int hLeft = r.left() + ZONE_BAND + ZONE_INSET;
    const int hW = r.width() - 2 * (ZONE_BAND + ZONE_INSET);
    switch (i) {
      case 0: return {r.left(), r.top(), ZONE_BAND, r.height()};
      case 1: return {r.right() + 1 - ZONE_BAND, r.top(), ZONE_BAND, r.height()};
      case 2: return {hLeft, r.top(), hW, ZONE_BAND};
      default: return {hLeft, r.bottom() + 1 - ZONE_BAND, hW, ZONE_BAND};
    }
  }

  void DockZonesOverlay::drawChevron(QPainter& p, int i, const QRect& z, bool hot) {
    // The browser's 18 px chevron (a 24-unit glyph, arms 3 by 6, stroke 2) at 18/24.
    QPointF ctr = QRectF(z).center();
    const double a = 4.5, b = 2.25;   // half its span along and across the arrow
    const double off = nudge;
    QPointF pts[3];
    switch (i) {
      case 0:
        ctr.rx() -= off;
        pts[0] = {ctr.x() + b, ctr.y() - a}; pts[1] = {ctr.x() - b, ctr.y()};
        pts[2] = {ctr.x() + b, ctr.y() + a};
        break;
      case 1:
        ctr.rx() += off;
        pts[0] = {ctr.x() - b, ctr.y() - a}; pts[1] = {ctr.x() + b, ctr.y()};
        pts[2] = {ctr.x() - b, ctr.y() + a};
        break;
      case 2:
        ctr.ry() -= off;
        pts[0] = {ctr.x() - a, ctr.y() + b}; pts[1] = {ctr.x(), ctr.y() - b};
        pts[2] = {ctr.x() + a, ctr.y() + b};
        break;
      default:
        ctr.ry() += off;
        pts[0] = {ctr.x() - a, ctr.y() - b}; pts[1] = {ctr.x(), ctr.y() + b};
        pts[2] = {ctr.x() + a, ctr.y() - b};
        break;
    }
    p.setBrush(Qt::NoBrush);
    // drop-shadow(0 0 3px rgba(0,0,0,.55)): a soft halo under the stroke.
    p.setPen(QPen(QColor(0, 0, 0, 80), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(pts, 3);
    // Accent at 0.9 at rest; the targeted band's turns the key text colour (.chat-dock-zone-active).
    QColor c = hot ? palette().color(QPalette::WindowText) : accent;
    c.setAlphaF(hot ? 1.0 : 0.9);
    p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(pts, 3);
  }

}  // namespace stencil::gui
