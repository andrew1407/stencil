#pragma once
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QWidget>

class QEvent;
class QObject;
class QPaintEvent;
class QVariantAnimation;

// Incognito indicator: browser body.incognito-mode (components.css) — a 3px dashed accent outline
// flush around the VIEWPORT, tracking the visible frame, not the image. Click-through child of
// the scroll viewport; purely visual, saving is gated in MainWindow.
namespace stencil::gui {

  class IncognitoOverlay : public QWidget {
    Q_OBJECT
   public:
    explicit IncognitoOverlay(QWidget* viewport);

    // Dashes DRAW ON clockwise from the top-left and retract the same way.
    void setActive(bool on);

    double progress() const { return progress_; }
    // Pure, so the draw order is testable without a display (browser: four staggered .ig-edge).
    static QPainterPath framePath(const QRectF& box, double t);
    static constexpr int kDrawMs = 480;
    // FLUSH with the viewport edge, inset only by the pen's half-width (browser outline-offset: -3px).
    static constexpr int kPenPx = 3;
    static QRectF frameBox(const QRectF& widgetRect) {
      return widgetRect.adjusted(kPenPx / 2.0, kPenPx / 2.0, -kPenPx / 2.0, -kPenPx / 2.0);
    }
    void setTheme(bool dark, const QString& accentKey);

   protected:
    void paintEvent(QPaintEvent* event) override;
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
