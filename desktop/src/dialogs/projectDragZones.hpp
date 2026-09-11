#pragma once
// Three-zone drop overlay shown on the MAIN WINDOW behind the (modal) Projects dialog while a
// project row is dragged out of it — the desktop analogue of the browser projects-modal drag
// zones (feature #4): top-left "Open here" (grey), top-right "Open in a new window" (blue),
// bottom "Remove" (red). The dialog floats in the centre; the zones are reachable in the margins
// around it. Purely visual + a cursor poll for the live highlight; the ACTION is decided by the
// dialog from the release position via zoneAt().
//
// Header-only and Q_OBJECT-free (std::function/QTimer-lambda, no signals), so it needs no MOC.
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
    enum class Zone { None, Here, NewWindow, Remove };

    explicit ProjectDragZones(QWidget* parent) : QWidget(parent) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      hide();
      poll_.setInterval(16);   // ~60fps so the pulse reads as smooth, not steppy
      // While shown: poll the cursor so the zone under it highlights live (the modal dialog's
      // blocking drag loop means we can't get drag-move events here), and advance the icon pulse.
      // Phase step matched to the interval for a ~1.5s breathing cycle (2π / (1.5s / 16ms)).
      QObject::connect(&poll_, &QTimer::timeout, [this] {
        hover_ = zoneAt(QCursor::pos());
        phase_ += 0.067;
        if (phase_ > 6.2831853) phase_ -= 6.2831853;
        update();
      });
    }

    // Show the zones for a drag. `dialogFrameGlobal` is the dialog's frame (global coords) so the
    // area under the dialog isn't treated as a zone. Fills the parent widget.
    void begin(const QRect& dialogFrameGlobal) {
      dialogFrame_ = dialogFrameGlobal;
      if (parentWidget()) setGeometry(parentWidget()->rect());
      hover_ = Zone::None;
      raise();
      show();
      poll_.start();
    }
    void end() { poll_.stop(); hide(); }

    // The zone a GLOBAL point falls in — None when it's over the dialog or outside the window.
    Zone zoneAt(const QPoint& global) const {
      if (dialogFrame_.contains(global)) return Zone::None;
      if (!parentWidget()) return Zone::None;
      const QPoint p = parentWidget()->mapFromGlobal(global);
      if (!rect().contains(p)) return Zone::None;
      if (p.y() > height() * 0.68) return Zone::Remove;
      return p.x() < width() / 2 ? Zone::Here : Zone::NewWindow;
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter g(this);
      g.setRenderHint(QPainter::Antialiasing, true);
      // Dim the canvas behind (incl. its "Open an image…" idle text) so it doesn't bleed through
      // the zones — mirrors the browser overlay's translucent backdrop (kept light, like the web).
      g.fillRect(rect(), QColor(8, 10, 16, 120));
      const int w = width(), h = height();
      const int top = static_cast<int>(h * 0.68);
      // Labels hug the OUTER edges (top zones → top, remove → bottom) so the centred Projects
      // dialog never overlaps a zone's icon/text — it sits between them. Mirrors the browser.
      drawZone(g, QRect(10, 10, w / 2 - 15, top - 20), QColor("#64748b"), QStringLiteral("folder"),
               QStringLiteral("Open here"), hover_ == Zone::Here, true);
      drawZone(g, QRect(w / 2 + 5, 10, w / 2 - 15, top - 20), QColor("#2563eb"), QStringLiteral("external"),
               QStringLiteral("Open in a new window"), hover_ == Zone::NewWindow, true);
      drawZone(g, QRect(10, top + 6, w - 20, h - top - 16), QColor("#dc3545"), QStringLiteral("trash"),
               QStringLiteral("Remove"), hover_ == Zone::Remove, false);
    }

   private:
    void drawZone(QPainter& g, const QRect& r, const QColor& col, const QString& iconName,
                  const QString& title, bool active, bool labelTop) {
      // Slightly-translucent zone-colour fill (matches the browser's ~0.9-alpha panels — the
      // canvas reads faintly through, rather than a heavy opaque block).
      const QColor panel(24, 26, 32);
      const int pct = active ? 40 : 26;
      QColor fill((col.red() * pct + panel.red() * (100 - pct)) / 100,
                  (col.green() * pct + panel.green() * (100 - pct)) / 100,
                  (col.blue() * pct + panel.blue() * (100 - pct)) / 100);
      fill.setAlpha(active ? 232 : 216);
      g.fillRect(r, fill);
      // Border + label use a BRIGHTENED zone colour so every zone's dashes read with equal weight
      // on the dark fill — otherwise the grey "Open here" dashes wash out next to the vivid
      // blue/red ones and the three look mismatched. Each keeps its own hue (grey/blue/red).
      const QColor accent = col.lighter(active ? 145 : 130);
      QPen pen(accent);
      pen.setWidth(3);   // thin stroke
      pen.setStyle(Qt::CustomDashLine);
      // Pattern is in units of line width; keep short dashes (dash ≈ 3×width, gap ≈ 2×width).
      pen.setDashPattern(QList<qreal>{3.0, 2.0});
      g.setPen(pen);
      g.setBrush(Qt::NoBrush);
      g.drawRoundedRect(r, 14, 14);
      // Pulsing icon (small → large → small), like the browser's zone icons. Rasterize ONCE at a
      // fixed high-res size and scale it with a sub-pixel-smooth QPainter transform about a FIXED
      // centre — so the size animates continuously instead of jumping between whole-pixel icon
      // rasterizations (which read as an uneven, steppy pulse), and it breathes symmetrically
      // rather than growing from one edge.
      const int base = 24;
      const double scale = 1.0 + 0.18 * std::sin(phase_);
      const int render = 40;   // fixed crisp source raster (well above the max pulsed size)
      const QPixmap px = themedIcon(iconName, accent, render).pixmap(render, render);
      const int cx = r.center().x();
      // Anchor the icon+title group near the zone's outer edge (top for open zones, bottom for
      // remove) instead of its centre, so the centred dialog never sits over the label.
      const int anchorY = labelTop ? r.top() + base + 22 : r.bottom() - base - 22;
      const double iconCenterY = anchorY - base / 2.0 - 6;   // fixed centre; the pulse scales about it
      g.save();
      g.setRenderHint(QPainter::SmoothPixmapTransform, true);
      g.translate(cx, iconCenterY);
      g.scale(base * scale / render, base * scale / render);
      g.drawPixmap(QPointF(-render / 2.0, -render / 2.0), px);
      g.restore();
      // Title below the icon.
      g.setPen(accent);
      QFont f = font();
      f.setPointSizeF(f.pointSizeF() + (active ? 7 : 5));
      f.setBold(true);
      g.setFont(f);
      g.drawText(QRect(r.left(), anchorY + 2, r.width(), r.height() / 3),
                 Qt::AlignHCenter | Qt::AlignTop, title);
    }

    QTimer poll_;
    Zone hover_ = Zone::None;
    double phase_ = 0.0;
    QRect dialogFrame_;
  };

}  // namespace stencil::gui
