#pragma once
// A drag's painted overlays, private to support/drag: the ghost of the control under the pointer and
// a drop target's glow, each a mouse-transparent child of the window, so QApplication::widgetAt sees
// through them. Browser twins: ui/canvas/dragGhost.js and css/animations/icon/drag.css.
#include <QPixmap>
#include <QPointer>
#include <QWidget>

namespace stencil::support {

  class DragGhost : public QWidget {
   public:
    // `grab`: where the pointer took hold, in source; a null `picture` takes the source's own grab.
    DragGhost(QWidget* source, const QPoint& grab, const QPixmap& picture = QPixmap());
    void follow(const QPoint& global);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QPixmap face;
    QPoint grab;
  };

  class DropGlow : public QWidget {
   public:
    explicit DropGlow(QWidget* target);
    void setOver(bool over);
    void track();   // re-fits the glow around wherever the target is now

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QPointer<QWidget> target;
    bool over = false;
  };

}  // namespace stencil::support
