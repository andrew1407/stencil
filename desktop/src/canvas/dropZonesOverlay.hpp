#pragma once
// Split image-drop overlay: LEFT = "Upload & save", RIGHT = "Upload incognito" (browser
// #global-drop-overlay). Click-through and purely visual; MainWindow::dropEvent decides.
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QPixmap>
#include <QList>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <cmath>

#include "../support/iconSet.hpp"   // the same glyph set the browser draws

namespace stencil::gui {

  class DropZonesOverlay : public QWidget {
   public:
    explicit DropZonesOverlay(QWidget* viewport) : QWidget(viewport) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_TranslucentBackground, true);
      hide();
      // ~60fps pulse; step matched to the interval for a ~1.5s breathing cycle.
      pulse_.setInterval(16);
      QObject::connect(&pulse_, &QTimer::timeout, [this] {
        phase_ += 0.067;
        if (phase_ > 6.2831853) phase_ -= 6.2831853;
        update();
      });
      fade_.setInterval(16);
      QObject::connect(&fade_, &QTimer::timeout, [this] {
        opacity_ -= 16.0 / FADE_OUT_MS;
        if (opacity_ <= 0.0) { opacity_ = 1.0; fade_.stop(); hide(); return; }
        update();
      });
      if (viewport) { viewport->installEventFilter(this); fitToParent(); }
    }

    void setAccent(const QColor& c) { accent_ = c; update(); }
    void setActiveLeft(bool left) { if (activeLeft_ != left) { activeLeft_ = left; update(); } }
    void showZones() { fade_.stop(); opacity_ = 1.0; fitToParent(); raise(); show(); pulse_.start(); }
    // Zones LEAVE on a fade (browser #global-drop-overlay.drop-closing).
    void hideZones() {
      if (isHidden()) return;
      pulse_.stop();
      fade_.start();
    }
    void hideZonesNow() { pulse_.stop(); fade_.stop(); opacity_ = 1.0; hide(); }
    double overlayOpacity() const { return opacity_; }

   protected:
    bool eventFilter(QObject* w, QEvent* e) override {
      if (w == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::Move)) fitToParent();
      return QWidget::eventFilter(w, e);
    }
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setOpacity(opacity_);
      const int w = width(), h = height();
      // The footer is its own strip BELOW the zones (browser .drop-foot).
      const int zoneH = h - 20 - FOOT_H - FOOT_GAP;
      const QRect left(10, 10, w / 2 - 15, zoneH);
      const QRect right(w / 2 + 5, 10, w / 2 - 15, zoneH);
      // The SAME glyphs as ui/dropOverlay.js, not the text "↑"/"◐" (system-font dependent).
      drawZone(p, left, accent_, QStringLiteral("upload"), QStringLiteral("Upload & save"),
               QStringLiteral("Load the image and keep it in your projects"), activeLeft_);
      drawZone(p, right, muted_, QStringLiteral("incognito"), QStringLiteral("Upload incognito"),
               QStringLiteral("Load the image without saving it"), !activeLeft_);
      QFont ff = p.font();
      ff.setPointSizeF(p.font().pointSizeF() - 1);
      p.setFont(ff);
      p.setPen(QColor("#9aa3b2"));
      p.drawText(QRect(20, h - 10 - FOOT_H, w - 40, FOOT_H), Qt::AlignHCenter | Qt::AlignVCenter,
                 QStringLiteral("…or drop a .json layout / .stencil project file "
                                "(either side — the split above is for images)"));
    }

   private:
    void fitToParent() { if (parentWidget()) setGeometry(parentWidget()->rect()); }
    void drawZone(QPainter& p, const QRect& r, const QColor& col, const QString& iconName,
                  const QString& title, const QString& sub, bool active) {
      const QFont base = p.font();
      QColor panel(30, 30, 34);
      QColor fill(
          (col.red() * (active ? 45 : 32) + panel.red() * (100 - (active ? 45 : 32))) / 100,
          (col.green() * (active ? 45 : 32) + panel.green() * (100 - (active ? 45 : 32))) / 100,
          (col.blue() * (active ? 45 : 32) + panel.blue() * (100 - (active ? 45 : 32))) / 100);
      fill.setAlpha(active ? 225 : 200);  // ≈0.88 / 0.78 — a little transparency
      p.fillRect(r, fill);
      QPen pen(col);
      pen.setWidth(3);
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(QList<qreal>{3.0, 2.0});
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(r, 14, 14);
      // Rasterized ONCE at GLYPH_PX: re-rendering the SVG per frame would push ~60 entries/s into themedIcon's cache.
      const double scale = 1.0 + 0.18 * std::sin(phase_);
      const QPixmap px = themedIcon(iconName, col, GLYPH_PX).pixmap(GLYPH_PX, GLYPH_PX);
      if (!px.isNull()) {
        const QRect band(r.left(), r.top() + r.height() / 6, r.width(), r.height() / 3);
        p.save();
        p.translate(band.center());
        const double k = (GLYPH_DRAW_PX * scale) / GLYPH_PX;
        p.scale(k, k);
        p.drawPixmap(QRect(-GLYPH_PX / 2, -GLYPH_PX / 2, GLYPH_PX, GLYPH_PX), px);
        p.restore();
      }
      QFont f = base;
      f.setPointSizeF(base.pointSizeF() + 5);
      f.setBold(true);
      p.setFont(f);
      p.drawText(QRect(r.left() + 10, r.center().y() - r.height() / 12, r.width() - 20, r.height() / 4),
                 Qt::AlignHCenter | Qt::AlignVCenter, title);
      QFont sf = base;
      sf.setPointSizeF(base.pointSizeF() - 1);
      p.setFont(sf);
      p.setPen(QColor("#c4c8cc"));
      p.drawText(QRect(r.left() + 16, r.center().y() + r.height() / 8, r.width() - 32, r.height() / 4),
                 Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, sub);
    }

    static constexpr double FADE_OUT_MS = 200.0;
    static constexpr int FOOT_H = 24;
    static constexpr int FOOT_GAP = 6;
    // Rasterized at this size, painted down to GLYPH_DRAW_PX (the browser's 46px icon) so the pulse never upscales.
    static constexpr int GLYPH_PX = 96;
    static constexpr double GLYPH_DRAW_PX = 46.0;

    QColor accent_{"#7c3aed"};
    QColor muted_{"#80868f"};
    bool activeLeft_ = true;
    QTimer pulse_;
    QTimer fade_;
    double phase_ = 0.0;
    double opacity_ = 1.0;
  };

}  // namespace stencil::gui
