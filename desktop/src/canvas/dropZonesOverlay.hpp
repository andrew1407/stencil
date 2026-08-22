#pragma once
// Split image-drop overlay: LEFT half = "Upload & save", RIGHT half = "Upload incognito"
// (the desktop analogue of the browser's #global-drop-overlay split — feature #3). Shown
// while a droppable file is dragged over the window; the half under the cursor highlights.
// Transparent + click-through (so drops still reach the MainWindow), and resizes to its
// parent viewport by watching its resizes (like IncognitoOverlay). Purely visual — the
// actual save-vs-incognito / here-vs-new-window decision is made in MainWindow::dropEvent.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
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
      // Drive the pulsing zone glyph: advance the phase + repaint ~60fps while shown so the pulse
      // reads as smooth (step matched to the interval for a ~1.5s breathing cycle).
      pulse_.setInterval(16);
      QObject::connect(&pulse_, &QTimer::timeout, [this] {
        phase_ += 0.067;
        if (phase_ > 6.2831853) phase_ -= 6.2831853;
        update();
      });
      // Fade-out ticker: same ~60fps cadence as the pulse, ~200ms to transparent.
      fade_.setInterval(16);
      QObject::connect(&fade_, &QTimer::timeout, [this] {
        opacity_ -= 16.0 / kFadeOutMs;
        if (opacity_ <= 0.0) { opacity_ = 1.0; fade_.stop(); hide(); return; }
        update();
      });
      if (viewport) { viewport->installEventFilter(this); fitToParent(); }
    }

    void setAccent(const QColor& c) { accent_ = c; update(); }
    void setActiveLeft(bool left) { if (activeLeft_ != left) { activeLeft_ = left; update(); } }
    void showZones() { fade_.stop(); opacity_ = 1.0; fitToParent(); raise(); show(); pulse_.start(); }
    // The zones LEAVE on a fade rather than blinking out, so a drop reads as the
    // overlay handing the canvas over (browser parity: #global-drop-overlay.drop-closing).
    void hideZones() {
      if (isHidden()) return;
      pulse_.stop();
      fade_.start();
    }
    // Skip the fade — used when the overlay must be gone immediately (teardown).
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
      // The footer line is its own strip BELOW the zones (browser .drop-foot is a
      // separate flex row) — the zones stop above it instead of painting under it.
      const int zoneH = h - 20 - kFootH - kFootGap;
      const QRect left(10, 10, w / 2 - 15, zoneH);
      const QRect right(w / 2 + 5, 10, w / 2 - 15, zoneH);
      // The SAME two glyphs the browser paints in #global-drop-overlay (ui/dropOverlay.js):
      // upload for the saving half, incognito for the other. They used to be the text
      // characters "↑" and "◐", which render as whatever the system font has — a stray
      // arrow and a half-moon that mean nothing.
      drawZone(p, left, accent_, QStringLiteral("upload"), QStringLiteral("Upload & save"),
               QStringLiteral("Load the image and keep it in your projects"), activeLeft_);
      drawZone(p, right, muted_, QStringLiteral("incognito"), QStringLiteral("Upload incognito"),
               QStringLiteral("Load the image without saving it"), !activeLeft_);
      // Browser parity: the layout/project files ignore the split (dropOverlay.js .drop-foot).
      QFont ff = p.font();
      ff.setPointSizeF(p.font().pointSizeF() - 1);
      p.setFont(ff);
      p.setPen(QColor("#9aa3b2"));
      p.drawText(QRect(20, h - 10 - kFootH, w - 40, kFootH), Qt::AlignHCenter | Qt::AlignVCenter,
                 QStringLiteral("…or drop a .json layout / .stencil project file "
                                "(either side — the split above is for images)"));
    }

   private:
    void fitToParent() { if (parentWidget()) setGeometry(parentWidget()->rect()); }
    void drawZone(QPainter& p, const QRect& r, const QColor& col, const QString& iconName,
                  const QString& title, const QString& sub, bool active) {
      const QFont base = p.font();
      // Mostly-opaque fill with a slight see-through, blended toward a dark panel so the zones
      // read clearly over the canvas without fully hiding it.
      QColor panel(30, 30, 34);
      QColor fill(
          (col.red() * (active ? 45 : 32) + panel.red() * (100 - (active ? 45 : 32))) / 100,
          (col.green() * (active ? 45 : 32) + panel.green() * (100 - (active ? 45 : 32))) / 100,
          (col.blue() * (active ? 45 : 32) + panel.blue() * (100 - (active ? 45 : 32))) / 100);
      fill.setAlpha(active ? 225 : 200);  // ≈0.88 / 0.78 — a little transparency
      p.fillRect(r, fill);
      // Thin DASHED border — short dashes (pattern in units of line width).
      QPen pen(col);
      pen.setWidth(3);
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(QList<qreal>{3.0, 2.0});
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(r, 14, 14);
      // Pulsing glyph (small → large → small) to catch the eye on drag. Rasterized ONCE at
      // kGlyphPx and scaled by the painter: re-rendering the SVG every frame would push a
      // new entry into themedIcon's (name,color,size) cache ~60 times a second.
      const double scale = 1.0 + 0.18 * std::sin(phase_);
      const QPixmap px = themedIcon(iconName, col, kGlyphPx).pixmap(kGlyphPx, kGlyphPx);
      if (!px.isNull()) {
        const QRect band(r.left(), r.top() + r.height() / 6, r.width(), r.height() / 3);
        p.save();
        p.translate(band.center());
        const double k = (kGlyphDrawPx * scale) / kGlyphPx;
        p.scale(k, k);
        p.drawPixmap(QRect(-kGlyphPx / 2, -kGlyphPx / 2, kGlyphPx, kGlyphPx), px);
        p.restore();
      }
      // Title.
      QFont f = base;
      f.setPointSizeF(base.pointSizeF() + 5);
      f.setBold(true);
      p.setFont(f);
      p.drawText(QRect(r.left() + 10, r.center().y() - r.height() / 12, r.width() - 20, r.height() / 4),
                 Qt::AlignHCenter | Qt::AlignVCenter, title);
      // Subtitle.
      QFont sf = base;
      sf.setPointSizeF(base.pointSizeF() - 1);
      p.setFont(sf);
      p.setPen(QColor("#c4c8cc"));
      p.drawText(QRect(r.left() + 16, r.center().y() + r.height() / 8, r.width() - 32, r.height() / 4),
                 Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, sub);
    }

    static constexpr double kFadeOutMs = 200.0;
    // Footer strip below the zones (browser .drop-foot row): text height + gap to the zones.
    static constexpr int kFootH = 24;
    static constexpr int kFootGap = 6;
    // Rasterized once at this size and painted down to kGlyphDrawPx (the browser's 46px
    // drop-zone icon), so the pulse never rounds to a blurry upscale.
    static constexpr int kGlyphPx = 96;
    static constexpr double kGlyphDrawPx = 46.0;

    QColor accent_{"#7c3aed"};
    QColor muted_{"#80868f"};
    bool activeLeft_ = true;
    QTimer pulse_;
    QTimer fade_;
    double phase_ = 0.0;
    double opacity_ = 1.0;
  };

}  // namespace stencil::gui
