#pragma once

#include <QColor>
#include <QWidget>
#include <functional>

class QTimer;
class QVariantAnimation;

namespace stencil::gui {

  // Drag dock zones (browser chatPanel dockZoneAt parity): while the floating
  // chat dock is dragged by its title bar, four NON-overlapping accent bands
  // over the CENTRAL dockable area preview the dock targets; the targeted band
  // is stronger. Transparent for input; the RELEASE position decides. A safety
  // poll force-hides the overlay whenever the mouse button is no longer down.
  class DockZonesOverlay : public QWidget {
    Q_OBJECT
   public:
    explicit DockZonesOverlay(QWidget* parent);

    // `targetRect` = the central dockable area in the parent's coordinates
    // (below the toolbars, above the status bar) — never the window chrome.
    // `stillDragging` = the drag poll's liveness (the watchdog's condition).
    void beginDrag(const QColor& accent, const QRect& targetRect,
                   std::function<bool()> stillDragging);
    void dragTo(const QPoint& globalPos);
    // 0 left, 1 right, 2 top, 3 bottom; -1 none — in OVERLAY coordinates.
    // Within reach of several edges (corners), the NEAREST edge wins.
    int zoneAt(const QPoint& globalPos) const;
    static Qt::DockWidgetArea area(int zone);

   protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void paintEvent(QPaintEvent*) override;

   private:
    QRect zoneRect(int i) const;
    void drawChevron(QPainter& p, int i, const QRect& z, bool hot);

    QColor accent_;
    int hover_ = -1;
    qreal nudge_ = 0.0;
    QVariantAnimation* nudgeAnim_ = nullptr;
    QTimer* watchdog_ = nullptr;
    std::function<bool()> stillDragging_;
  };

}  // namespace stencil::gui
