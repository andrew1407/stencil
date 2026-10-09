#pragma once
// A drag's painted overlays, private to support/drag: the ghost of the control under the pointer,
// shining along its edge, and a drop target's glow, each a mouse-transparent child of the window, so QApplication::widgetAt sees
// through them. Browser twins: ui/canvas/dragGhost.js and css/animations/icon/drag.css.
#include <QColor>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace stencil::support {

  // The ghost's rim at `ms` into the drag, 0..1: a slow breath, held full when motion is reduced.
  double ghostShine(double ms, bool still);

  class DragGhost : public QWidget {
   public:
    // `grab`: where the pointer took hold, in source; a null `picture` takes the source's own grab.
    DragGhost(QWidget* source, const QPoint& grab, const QPixmap& picture = QPixmap());
    void follow(const QPoint& global);
    // The control's picture alone, without the room its shining rim takes.
    QSize faceSize() const;

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QPixmap face;
    QPoint grab;
    QColor accent;
    QTimer ticker;
    QElapsedTimer clock;
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
