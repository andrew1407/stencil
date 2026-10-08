#pragma once
// The theme lens (browser twin js/ui/drag/themeLens.js): while the theme switch is dragged, a disc
// at the pointer shows the window in the other theme — one photograph, taken as the drag starts,
// whose picture region is inverted since a picture has no theme — under a thin rim.
#include <QPixmap>
#include <QWidget>

namespace stencil::gui {

  class ThemeLens : public QWidget {
   public:
    static constexpr int RADIUS = 90;          // px, the browser's LENS_RADIUS_PX
    static constexpr int REACH = RADIUS + 3;   // the disc and the outer half of its rim
    // `other`: the host photographed in the other theme; `picture` (host px) shows inverted.
    ThemeLens(QWidget* host, QPixmap other, const QRect& picture);
    void follow(const QPoint& global);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QPixmap shot;
  };

}  // namespace stencil::gui
