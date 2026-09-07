#pragma once

#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include <functional>

class QTimer;
class QToolButton;
class QVariantAnimation;

namespace stencil::gui {

  // Logo hover fx: pulse + levitate + accent glow + orbiting rays — the desktop
  // match for the browser/extension logo hover (css/animations.css logoPulse /
  // logoRaysSpin / logoRaysShimmer). A mouse-through overlay child of the WINDOW
  // (not the button) paints in a margin around the logo so the toolbar never
  // reflows; while the loop runs the button's icon is blanked and the overlay
  // draws the mark. Runs ONLY while hovered (hidden + stopped = no idle CPU).
  class LogoHoverFx : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kMargin = 10;   // paint room around the button (ring + glow)
    LogoHoverFx(QToolButton* logo, std::function<QPixmap()> makePixmap,
                std::function<QColor()> accent);

    // The accent can cycle from a click on the hovered logo (mid-animation):
    // refresh the cached art and re-blank the button so the overlay keeps the pixels.
    void themeChanged();
    bool active() const;
    // The logo's accent popover counts as hovering the logo (browser parity: the menu
    // lives inside .app-logo-wrap, so the shine holds over it). Enter on `box` starts
    // the loop, and leaving the logo/box only stops it once neither is under the cursor
    // — after a short grace, so crossing the anchor gap never blinks the glow.
    void holdWhile(QWidget* box);

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void paintEvent(QPaintEvent*) override;

   private:
    void start();
    void stop();
    void blankButtonIcon();
    void syncGeometry();
    void leaveSoon();
    bool hoveredAnywhere() const;

    QToolButton* logo_;
    std::function<QPixmap()> makePixmap_;
    std::function<QColor()> accent_;
    QVariantAnimation* pulse_ = nullptr;
    QVariantAnimation* spin_ = nullptr;
    QPointer<QWidget> box_;           // the open accent popover's in-window box, if any
    QTimer* grace_ = nullptr;         // deferred stop across the logo → popover crossing
    QPixmap pm_;      // the mark at the CURRENT accent (cached per hover / theme change)
    qreal beat_ = 0.0;
    qreal angle_ = 0.0;
  };

  // MainWindow stores the overlay as a plain QWidget* member — this types it back.
  inline LogoHoverFx* asLogoFx(QWidget* w) { return static_cast<LogoHoverFx*>(w); }

}  // namespace stencil::gui
