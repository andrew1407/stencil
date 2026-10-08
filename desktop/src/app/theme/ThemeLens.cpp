#include "ThemeLens.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QRegion>

namespace stencil::gui {

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
  }

  void ThemeLens::follow(const QPoint& global) {
    move(parentWidget()->mapFromGlobal(global) - QPoint(REACH, REACH));
    raise();
    show();
  }

  void ThemeLens::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF centre(REACH, REACH);
    QPainterPath disc;
    disc.addEllipse(centre, RADIUS, RADIUS);
    p.setClipPath(disc);
    p.drawPixmap(-pos(), shot);
    p.setClipping(false);
    // Two passes, as the compare divider's, so the rim reads over either theme.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 110), 3.0));
    p.drawEllipse(centre, RADIUS, RADIUS);
    p.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
    p.drawEllipse(centre, RADIUS, RADIUS);
  }

}  // namespace stencil::gui
