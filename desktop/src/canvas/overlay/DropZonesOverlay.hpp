#pragma once
// Split image-drop overlay: LEFT = "Upload & save", RIGHT = "Upload incognito" (browser
// #global-drop-overlay). Click-through and purely visual; MainWindow::dropEvent decides.
// Its host is the whole WINDOW, as the browser's overlay is the whole page, so the split it
// paints is the window midline whatever a dock takes from the canvas.
#include "accentDefaults.hpp"
#include "colorMix.hpp"
#include <QColor>
#include <QPoint>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
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

#include "../../support/icon/iconSet.hpp"   // the same glyph set the browser draws

namespace stencil::gui {

  class DropZonesOverlay : public QWidget {
   public:
    explicit DropZonesOverlay(QWidget* host) : QWidget(host) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_TranslucentBackground, true);
      hide();
      // ~60fps pulse; step matched to the interval for a ~1.5s breathing cycle.
      pulse.setInterval(16);
      QObject::connect(&pulse, &QTimer::timeout, [this] {
        phase += 0.067;
        if (phase > 6.2831853) phase -= 6.2831853;
        update();
      });
      fade.setInterval(16);
      QObject::connect(&fade, &QTimer::timeout, [this] {
        opacity -= 16.0 / FADE_OUT_MS;
        if (opacity <= 0.0) { opacity = 1.0; fade.stop(); hide(); return; }
        update();
      });
      if (host) { host->installEventFilter(this); fitToParent(); }
    }

    // key = --text-key (the saving title), muted = --text-muted (the incognito half), panel = --bg-container.
    void setColors(const QColor& c, const QColor& key, const QColor& grey, const QColor& panel) {
      accent = c;
      textKey = key;
      muted = grey;
      container = panel;
      update();
    }
    void setActiveLeft(bool left) { if (activeLeft != left) { activeLeft = left; update(); } }
    bool getActiveLeft() const { return activeLeft; }
    // The lit half is the one the pointer is over, wherever the drag was delivered.
    void followDrag(const QPoint& global) { setActiveLeft(mapFromGlobal(global).x() < width() / 2); }
    void showZones() { fade.stop(); opacity = 1.0; fitToParent(); raise(); show(); pulse.start(); }
    // Zones LEAVE on a fade (browser #global-drop-overlay.drop-closing).
    void hideZones() {
      if (isHidden()) return;
      pulse.stop();
      fade.start();
    }
    void hideZonesNow() { pulse.stop(); fade.stop(); opacity = 1.0; hide(); }
    double overlayOpacity() const { return opacity; }

   protected:
    bool eventFilter(QObject* w, QEvent* e) override {
      if (w == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::Move)) fitToParent();
      return QWidget::eventFilter(w, e);
    }
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setOpacity(opacity);
      p.fillRect(rect(), QColor(0, 100, 255, WASH_ALPHA));
      const int w = width(), h = height();
      // The footer is its own strip BELOW the zones (browser .drop-foot).
      const int footH = QFontMetrics(scaledFont(p, FOOT_PX, false)).height();
      const int zoneH = h - 2 * PAD - footH - FOOT_GAP;
      const int zoneW = (w - 2 * PAD - SPLIT_GAP) / 2;
      const QRect left(PAD, PAD, zoneW, zoneH);
      const QRect right(w - PAD - zoneW, PAD, zoneW, zoneH);
      // The SAME glyphs as ui/dropOverlay.js, not the text "↑"/"◐" (system-font dependent).
      drawZone(p, left, accent, textKey, QStringLiteral("upload"), QStringLiteral("Upload & save"),
               QStringLiteral("Load the image and keep it in your projects"),
               zoneFill(accent, LEFT_TINT, activeLeft));
      drawZone(p, right, muted, muted, QStringLiteral("incognito"), QStringLiteral("Upload incognito"),
               QStringLiteral("Load the image without saving it"),
               zoneFill(muted, RIGHT_TINT, !activeLeft));
      p.setFont(scaledFont(p, FOOT_PX, false));
      p.setPen(muted);
      p.drawText(QRect(PAD, h - PAD - footH, w - 2 * PAD, footH), Qt::AlignHCenter | Qt::AlignVCenter,
                 QStringLiteral("…or drop a .json layout / .stencil project / .stc script "
                                "(either side — the split above is for images)"));
    }

   private:
    void fitToParent() { if (parentWidget()) setGeometry(parentWidget()->rect()); }
    static QFont scaledFont(const QPainter& p, int px, bool bold) {
      QFont f = p.font();
      f.setPixelSize(px);
      f.setBold(bold);
      return f;
    }
    // Only the LIT zone is tinted (browser .drop-zone-active); the other is bare --bg-container.
    QColor zoneFill(const QColor& col, double tint, bool active) const {
      if (active) return mixSrgb(container, col, tint);
      QColor f = container;
      f.setAlpha(IDLE_ALPHA);
      return f;
    }
    void drawZone(QPainter& p, const QRect& r, const QColor& col, const QColor& titleCol,
                  const QString& iconName, const QString& title, const QString& sub,
                  const QColor& fill) {
      p.fillRect(r, fill);
      QPen pen(col);
      pen.setWidth(BORDER_PX);
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(QList<qreal>{3.0, 2.0});
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(r, RADIUS_PX, RADIUS_PX);

      // Icon, title and subtitle are one centred column with STACK_GAP between them
      // (browser .drop-zone: flex-direction:column; justify-content:center; gap:6px).
      const QFont titleFont = scaledFont(p, TITLE_PX, true);
      const QFont subFont = scaledFont(p, SUB_PX, false);
      const int titleH = QFontMetrics(titleFont).height();
      const int subW = r.width() * 9 / 10;   // browser .drop-zone span { max-width: 90% }
      const QRect subBox = QFontMetrics(subFont).boundingRect(
          QRect(0, 0, subW, 0), Qt::AlignHCenter | Qt::TextWordWrap, sub);
      const int stackH = GLYPH_DRAW_PX + STACK_GAP + titleH + STACK_GAP + subBox.height();
      int y = r.center().y() - stackH / 2;

      const double scale = PULSE_MID + PULSE_AMP * std::sin(phase);
      // Rasterized ONCE at GLYPH_PX: re-rendering the SVG per frame would push ~60 entries/s into themedIcon's cache.
      const QPixmap px = themedIcon(iconName, col, GLYPH_PX).pixmap(GLYPH_PX, GLYPH_PX);
      if (!px.isNull()) {
        p.save();
        p.translate(r.center().x(), y + GLYPH_DRAW_PX / 2);
        const double k = (GLYPH_DRAW_PX * scale) / GLYPH_PX;
        p.scale(k, k);
        p.drawPixmap(QRect(-GLYPH_PX / 2, -GLYPH_PX / 2, GLYPH_PX, GLYPH_PX), px);
        p.restore();
      }
      y += GLYPH_DRAW_PX + STACK_GAP;
      p.setFont(titleFont);
      p.setPen(titleCol);
      p.drawText(QRect(r.left(), y, r.width(), titleH), Qt::AlignHCenter | Qt::AlignVCenter, title);
      y += titleH + STACK_GAP;
      p.setFont(subFont);
      p.setPen(muted);
      p.drawText(QRect(r.center().x() - subW / 2, y, subW, subBox.height()),
                 Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, sub);
    }

    static constexpr double FADE_OUT_MS = 200.0;
    // Mirrors browser/css/components/controls.css #global-drop-overlay, in device-independent px.
    static constexpr int PAD = 24;
    static constexpr int SPLIT_GAP = 16;
    static constexpr int FOOT_GAP = 14;
    static constexpr int STACK_GAP = 6;
    static constexpr int BORDER_PX = 5;
    static constexpr int RADIUS_PX = 16;
    static constexpr int TITLE_PX = 20;
    static constexpr int SUB_PX = 13;
    static constexpr int FOOT_PX = 13;
    // Rasterized at this size, painted down to GLYPH_DRAW_PX (the browser's 46px icon) so the pulse never upscales.
    static constexpr int GLYPH_PX = 96;
    static constexpr int GLYPH_DRAW_PX = 46;
    static constexpr int WASH_ALPHA = 51;    // rgba(0, 100, 255, 0.2)
    static constexpr int IDLE_ALPHA = 224;   // --bg-container at 88%
    static constexpr double LEFT_TINT = 0.28;
    static constexpr double RIGHT_TINT = 0.30;
    // The keyframe scales 0.8 → 1.25, written as a midpoint and an amplitude.
    static constexpr double PULSE_MID = 1.025;
    static constexpr double PULSE_AMP = 0.225;

    QColor accent{DEFAULT_ACCENT_HEX};
    QColor textKey{DEFAULT_ACCENT_HEX};
    QColor muted{"#80868f"};
    QColor container{"#242424"};
    bool activeLeft = true;
    QTimer pulse;
    QTimer fade;
    double phase = 0.0;
    double opacity = 1.0;
  };

}  // namespace stencil::gui
