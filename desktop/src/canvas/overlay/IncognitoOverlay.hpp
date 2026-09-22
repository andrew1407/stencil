#pragma once
#include "accentDefaults.hpp"
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QWidget>
#include <array>

class QEvent;
class QObject;
class QPaintEvent;
class QVariantAnimation;

// Incognito indicator: browser .incognito-frame / .ig-edge (css/components/incognito.css) — four
// 3px dashed accent edges tracing the VIEWPORT, the picture and the empty ground beside it alike.
// Click-through child of the scroll viewport; purely visual, saving is gated in MainWindow.
namespace stencil::gui {

  class IncognitoOverlay : public QWidget {
    Q_OBJECT
   public:
    explicit IncognitoOverlay(QWidget* viewport);

    // The drawn share of each edge, clockwise from the top: top, right, bottom, left.
    using EdgeLengths = std::array<double, 4>;

    // Dashes DRAW ON clockwise from the top-left and retract last-edge-first.
    void setActive(bool on);

    EdgeLengths getEdges() const { return edges; }
    double getProgress() const { return (edges[0] + edges[1] + edges[2] + edges[3]) / 4.0; }
    // Each edge eases from `from` to its target over EDGE_MS, STAGGER_MS behind the one before.
    static EdgeLengths edgeLengths(double ms, bool drawing, const EdgeLengths& from);
    // Pure, so the draw order is testable without a display (browser: four staggered .ig-edge).
    static QPainterPath framePath(const QRectF& box, const EdgeLengths& len);
    static constexpr int EDGE_MS = 170;      // browser --ig-draw
    static constexpr int STAGGER_MS = 110;   // browser --ig-delay, one step per edge
    static constexpr int DRAW_MS = STAGGER_MS * 3 + EDGE_MS;   // the whole frame, end to end
    // Flush with the viewport edge, inset only by the pen's half-width.
    static constexpr int PEN_PX = 3;
    // Browser: accent 0 9px, transparent 9px 16px.
    static constexpr double DASH_ON_PX = 9.0, DASH_OFF_PX = 7.0;
    static QRectF frameBox(const QRectF& widgetRect) {
      return widgetRect.adjusted(PEN_PX / 2.0, PEN_PX / 2.0, -PEN_PX / 2.0, -PEN_PX / 2.0);
    }
    void setTheme(bool dark, const QString& accentKey);

   protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void fitToParent();

    bool active = false;
    EdgeLengths edges{0.0, 0.0, 0.0, 0.0};
    EdgeLengths from{0.0, 0.0, 0.0, 0.0};
    QVariantAnimation* anim = nullptr;
    bool dark = false;
    QString accentKey = DEFAULT_ACCENT_KEY;
  };

}
