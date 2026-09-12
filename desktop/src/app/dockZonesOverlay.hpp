#pragma once

#include <QColor>
#include <QPixmap>
#include <QWidget>
#include <functional>

class QTimer;
class QVariantAnimation;

namespace stencil::gui {

  // Drag dock zones (browser chatPanel dockZoneAt parity): four NON-overlapping bands over the CENTRAL dockable area.
  // Transparent for input; a safety poll force-hides it once the mouse button is up.
  class DockZonesOverlay : public QWidget {
    Q_OBJECT
   public:
    explicit DockZonesOverlay(QWidget* parent);

    // `targetRect` = the central dockable area in parent coordinates; `stillDragging` = the watchdog's condition.
    void beginDrag(const QColor& accent, const QRect& targetRect,
                   std::function<bool()> stillDragging);
    void dragTo(const QPoint& globalPos);
    // 0 left, 1 right, 2 top, 3 bottom; -1 none — OVERLAY coordinates. In a corner the NEAREST edge wins.
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
    // Qt has no backdrop filter, so the bands paint their own blurred copy (browser .chat-dock-zone backdrop-filter).
    QPixmap backdrop_;
    int hover_ = -1;
    qreal nudge_ = 0.0;
    QVariantAnimation* nudgeAnim_ = nullptr;
    QTimer* watchdog_ = nullptr;
    std::function<bool()> stillDragging_;
  };

}  // namespace stencil::gui
