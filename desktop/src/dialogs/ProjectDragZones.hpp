#pragma once
// Three-zone drop overlay on the MAIN WINDOW behind the modal Projects dialog (browser projects-modal
// drag zones): open here / new window / remove. Visual + a cursor poll only; the dialog decides the
// ACTION from the release position via zoneAt(). Header-only and Q_OBJECT-free, so no MOC.
#include "iconSet.hpp"

#include <QColor>
#include <QCursor>
#include <QFont>
#include <QIcon>
#include <QList>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <cmath>

namespace stencil::gui {

  class ProjectDragZones : public QWidget {
   public:
    enum class Zone { NONE, HERE, NEW_WINDOW, REMOVE };

    explicit ProjectDragZones(QWidget* parent) : QWidget(parent) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      hide();
      poll_.setInterval(16);
      // A cursor poll: the modal dialog's blocking drag loop delivers no drag-move events here.
      // Phase step matched to the interval for a ~1.5s breathing cycle (2π / (1.5s / 16ms)).
      QObject::connect(&poll_, &QTimer::timeout, [this] {
        hover_ = zoneAt(QCursor::pos());
        phase_ += 0.067;
        if (phase_ > 6.2831853) phase_ -= 6.2831853;
        update();
      });
    }

    // `dialogFrameGlobal` (global coords) is not treated as a zone. Fills the parent widget.
    void begin(const QRect& dialogFrameGlobal) {
      dialogFrame_ = dialogFrameGlobal;
      if (parentWidget()) setGeometry(parentWidget()->rect());
      hover_ = Zone::NONE;
      raise();
      show();
      poll_.start();
    }
    void end() { poll_.stop(); hide(); }

    Zone zoneAt(const QPoint& global) const {
      if (dialogFrame_.contains(global)) return Zone::NONE;
      if (!parentWidget()) return Zone::NONE;
      const QPoint p = parentWidget()->mapFromGlobal(global);
      if (!rect().contains(p)) return Zone::NONE;
      if (p.y() > height() * 0.68) return Zone::REMOVE;
      return p.x() < width() / 2 ? Zone::HERE : Zone::NEW_WINDOW;
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter g(this);
      g.setRenderHint(QPainter::Antialiasing, true);
      // Dims the canvas behind, as the browser overlay's translucent backdrop does.
      g.fillRect(rect(), QColor(8, 10, 16, 120));
      const int w = width(), h = height();
      const int top = static_cast<int>(h * 0.68);
      // Labels hug the OUTER edges so the centred dialog never overlaps a zone's icon/text.
      drawZone(g, QRect(10, 10, w / 2 - 15, top - 20), QColor("#64748b"), QStringLiteral("folder"),
               QStringLiteral("Open here"), hover_ == Zone::HERE, true);
      drawZone(g, QRect(w / 2 + 5, 10, w / 2 - 15, top - 20), QColor("#2563eb"), QStringLiteral("external"),
               QStringLiteral("Open in a new window"), hover_ == Zone::NEW_WINDOW, true);
      drawZone(g, QRect(10, top + 6, w - 20, h - top - 16), QColor("#dc3545"), QStringLiteral("trash"),
               QStringLiteral("Remove"), hover_ == Zone::REMOVE, false);
    }

   private:
    void drawZone(QPainter& g, const QRect& r, const QColor& col, const QString& iconName,
                  const QString& title, bool active, bool labelTop) {
      // ~0.9 alpha, as the browser's panels.
      const QColor panel(24, 26, 32);
      const int pct = active ? 40 : 26;
      QColor fill((col.red() * pct + panel.red() * (100 - pct)) / 100,
                  (col.green() * pct + panel.green() * (100 - pct)) / 100,
                  (col.blue() * pct + panel.blue() * (100 - pct)) / 100);
      fill.setAlpha(active ? 232 : 216);
      g.fillRect(r, fill);
      // A BRIGHTENED zone colour, so the grey dashes read with the same weight as the blue/red ones.
      const QColor accent = col.lighter(active ? 145 : 130);
      QPen pen(accent);
      pen.setWidth(3);
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(QList<qreal>{3.0, 2.0});
      g.setPen(pen);
      g.setBrush(Qt::NoBrush);
      g.drawRoundedRect(r, 14, 14);
      // Rasterized ONCE at a fixed size and scaled about a FIXED centre, so the pulse is continuous
      // and symmetric rather than steppy whole-pixel rasterizations.
      const int base = 24;
      const double scale = 1.0 + 0.18 * std::sin(phase_);
      const int render = 40;
      const QPixmap px = themedIcon(iconName, accent, render).pixmap(render, render);
      const int cx = r.center().x();
      const int anchorY = labelTop ? r.top() + base + 22 : r.bottom() - base - 22;
      const double iconCenterY = anchorY - base / 2.0 - 6;
      g.save();
      g.setRenderHint(QPainter::SmoothPixmapTransform, true);
      g.translate(cx, iconCenterY);
      g.scale(base * scale / render, base * scale / render);
      g.drawPixmap(QPointF(-render / 2.0, -render / 2.0), px);
      g.restore();
      g.setPen(accent);
      QFont f = font();
      f.setPointSizeF(f.pointSizeF() + (active ? 7 : 5));
      f.setBold(true);
      g.setFont(f);
      g.drawText(QRect(r.left(), anchorY + 2, r.width(), r.height() / 3),
                 Qt::AlignHCenter | Qt::AlignTop, title);
    }

    QTimer poll_;
    Zone hover_ = Zone::NONE;
    double phase_ = 0.0;
    QRect dialogFrame_;
  };

}  // namespace stencil::gui
