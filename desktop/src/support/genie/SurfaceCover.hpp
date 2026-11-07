#pragma once
// A child painted over a surface while its picture flies: it takes no pointer, hides what it covers (opaque)
// and keeps its place through the surface's resizes. Browser twin: the fixed copies that stand in under
// motion/surface/leaveCopy.js and motion/control/canvasSlide.js.
#include <QColor>
#include <QPixmap>
#include <QWidget>

#include <functional>

class QPainter;

namespace stencil::support::genie {

  class SurfaceCover : public QWidget {
   public:
    using Paint = std::function<void(QPainter&, const SurfaceCover&)>;
    using Place = std::function<QRect(const QWidget& surface)>;

    static QRect wholeOf(const QWidget& surface) { return surface.rect(); }
    SurfaceCover(QWidget* surface, Paint paint, Place place = &SurfaceCover::wholeOf);

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void paintEvent(QPaintEvent*) override;

   private:
    Paint paint;
    Place place;
  };

  // The surface's own background while its picture flies (an opacity veil showed white where the panel is grey).
  SurfaceCover* fillCover(QWidget* surface, QColor fill);
  // The part of `ground` (the window photographed at its device ratio) that lies under the surface.
  SurfaceCover* groundCover(QWidget* surface, QPixmap ground);
  // `shot` held to `edge` of the surface at its own size: a docked chat as it stands while its space slides.
  SurfaceCover* edgeCover(QWidget* surface, QPixmap shot, Qt::DockWidgetArea edge);

}  // namespace stencil::support::genie
