#include "SurfaceCover.hpp"

#include <QEvent>
#include <QPainter>

#include <utility>

namespace stencil::support::genie {

  SurfaceCover::SurfaceCover(QWidget* surface, Paint paint, Place place)
      : QWidget(surface), paint(std::move(paint)), place(std::move(place)) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    surface->installEventFilter(this);
    setGeometry(this->place(*surface));
    raise();
    show();
  }

  bool SurfaceCover::eventFilter(QObject* o, QEvent* e) {
    if (o == parentWidget() && e->type() == QEvent::Resize) setGeometry(place(*parentWidget()));
    return false;
  }

  void SurfaceCover::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paint(p, *this);
  }

  SurfaceCover* fillCover(QWidget* surface, QColor fill) {
    return new SurfaceCover(surface, [fill](QPainter& p, const SurfaceCover& c) { p.fillRect(c.rect(), fill); });
  }

  SurfaceCover* groundCover(QWidget* surface, QPixmap ground) {
    return new SurfaceCover(surface, [ground](QPainter& p, const SurfaceCover& c) {
      const qreal dpr = ground.devicePixelRatio();
      p.drawPixmap(c.rect(), ground, QRectF(QPointF(c.mapTo(c.window(), QPoint(0, 0))) * dpr, QSizeF(c.size()) * dpr));
    });
  }

  SurfaceCover* edgeCover(QWidget* surface, QPixmap shot, Qt::DockWidgetArea edge) {
    const QSize at = shot.deviceIndependentSize().toSize();
    return new SurfaceCover(surface, [shot](QPainter& p, const SurfaceCover&) { p.drawPixmap(0, 0, shot); },
                            [at, edge](const QWidget& room) {
      return QRect(edge == Qt::RightDockWidgetArea ? room.width() - at.width() : 0,
                   edge == Qt::BottomDockWidgetArea ? room.height() - at.height() : 0, at.width(), at.height());
    });
  }

}  // namespace stencil::support::genie
