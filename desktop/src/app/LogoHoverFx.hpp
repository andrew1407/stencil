#pragma once

#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include <functional>

class QTimer;
class QToolButton;
class QVariantAnimation;

namespace stencil::gui {

  // Logo hover fx (browser css/animations.css logoPulse / logoRaysSpin / logoRaysShimmer): a mouse-through overlay child
  // of the WINDOW paints in a margin around the logo so the toolbar never reflows. Runs ONLY while hovered.
  class LogoHoverFx : public QWidget {
    Q_OBJECT
   public:
    static constexpr int MARGIN = 10;   // paint room around the button (ring + glow)
    LogoHoverFx(QToolButton* logo, std::function<QPixmap()> makePixmap,
                std::function<QColor()> accent);

    // The accent can cycle mid-animation: refresh the art and re-blank the button.
    void themeChanged();
    // Stand down while something covers the header (the logo stage), then paint the mark again.
    void standDown(bool on);
    bool active() const;
    // The accent popover counts as hovering the logo (browser: the menu lives inside .app-logo-wrap); a short grace covers the anchor gap.
    void holdWhile(QWidget* box);

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void paintEvent(QPaintEvent*) override;

   private:
    void start();
    void stop();
    void showStatic();   // paint the resting mark (no animation), button icon blanked
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

  inline LogoHoverFx* asLogoFx(QWidget* w) { return static_cast<LogoHoverFx*>(w); }

}  // namespace stencil::gui
