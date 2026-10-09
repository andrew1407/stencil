#pragma once
// The theme lens (browser twin js/ui/drag/themeLens.js): while the theme switch is dragged, a disc
// at the pointer shows the window in the other theme — one photograph, taken as the drag starts,
// whose picture region is inverted since a picture has no theme — under a thin rim.
#include <QElapsedTimer>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include <functional>

namespace stencil::gui {

  class ThemeLens : public QWidget {
   public:
    static constexpr int RADIUS = 90;          // px, the browser's LENS_RADIUS_PX
    static constexpr int REACH = RADIUS + 3;   // the disc and the outer half of its rim
    static constexpr int GROW_MS = 192;        // a point to RADIUS, the browser's LENS_GROW_MS
    static constexpr int CLOSE_MS = 360;       // RADIUS back to a point, the browser's LENS_CLOSE_MS
    // The radius `ms` after opening: an ease-out (cubic) from 0, full at GROW_MS.
    static double radiusAt(double ms);
    // `other`: the host photographed in the other theme; `picture` (host px) shows inverted.
    ThemeLens(QWidget* host, QPixmap other, const QRect& picture);
    void follow(const QPoint& global);
    // Shrinks the circle back into a point, runs `done`, then deletes itself; at once when motion is reduced.
    void dismiss(std::function<void()> done = {});
    double getRadius() const { return radius; }

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QPixmap shot;
    double radius = RADIUS, closingFrom = -1;   // -1: not closing
    QTimer ticker;
    QElapsedTimer clock;
    std::function<void()> closed;
  };

}  // namespace stencil::gui
