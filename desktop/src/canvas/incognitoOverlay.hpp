#pragma once
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QWidget>

class QEvent;
class QObject;
class QPaintEvent;
class QVariantAnimation;

// Incognito indicator overlay. Port of the browser's body.incognito-mode styling
// (browser/css/components.css): a 3px dashed accent outline flush around the canvas
// VIEWPORT. The "Incognito — not saved" fact is NOT painted over the picture — it
// rides on the toolbar's "?" hint beside the project name, which stays readable
// with the tool rows collapsed. Like the browser, it tracks the visible frame, NOT the image — so it
// stays put as the image pans/zooms, and frames the whole viewport even when the
// image is smaller than it. Purely visual; saving is gated in MainWindow.
//
// Implemented as a transparent, click-through child of the scroll viewport (the
// desktop analog of .canvas-viewport), raised above the canvas. It resizes itself
// to fill the viewport by watching the parent's resize events (cf. Notifications).
namespace stencil::gui {

  class IncognitoOverlay : public QWidget {
    Q_OBJECT
   public:
    explicit IncognitoOverlay(QWidget* viewport);

    // Show/hide the indicator (driven by MainWindow's incognito_ state). The dashes
    // DRAW ON clockwise from the top-left and retract the same way out, so switching
    // the mode reads as the frame closing/opening rather than blinking.
    void setActive(bool on);

    // 0 = no frame, 1 = closed. Exposed for the headless test; also what paintEvent uses.
    double progress() const { return progress_; }
    // The clockwise partial perimeter at `t`, as a path over `box`. Pure — the whole
    // reason the draw order is testable without a display. Mirrors the browser's four
    // staggered .ig-edge elements (browser/css/components.css).
    static QPainterPath framePath(const QRectF& box, double t);
    static constexpr int kDrawMs = 480;
    // Dash stroke width, and the box the frame is stroked over: FLUSH with the
    // viewport edge, inset only by the pen's half-width so the stroke isn't
    // clipped (the browser's outline-offset: -3px on a 3px outline).
    static constexpr int kPenPx = 3;
    static QRectF frameBox(const QRectF& widgetRect) {
      return widgetRect.adjusted(kPenPx / 2.0, kPenPx / 2.0, -kPenPx / 2.0, -kPenPx / 2.0);
    }
    // Recolour to the current theme accent (cf. CanvasWidget::setAccent/setDark).
    void setTheme(bool dark, const QString& accentKey);

   protected:
    void paintEvent(QPaintEvent* event) override;
    // Track the parent viewport's size so the outline always frames it.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void fitToParent();

    bool active_ = false;
    double progress_ = 0.0;
    QVariantAnimation* anim_ = nullptr;
    bool dark_ = false;
    QString accentKey_ = "violet";
  };

}
