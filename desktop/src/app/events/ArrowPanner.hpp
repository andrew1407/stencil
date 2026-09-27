#pragma once
#include <QObject>
#include <functional>

class QTimer;

namespace stencil::gui {

  // Panning the canvas with the held arrows (browser controlsBinder.js arrowPanTick). Two held keys
  // arrive as independent native auto-repeat streams, so the held set is combined on a tick of its
  // own and two arrows pan diagonally.
  class ArrowPanner : public QObject {
    Q_OBJECT
   public:
    // `panBy` scrolls the canvas by (dx, dy) viewport px.
    ArrowPanner(QObject* parent, std::function<void(int dx, int dy)> panBy);

    // An arrow went down: dirX / dirY are -1, 0 or 1.
    void press(int dirX, int dirY, bool shift);
    void setShift(bool on);
    // A key-up that is not an auto-repeat.
    void release(int key);
    // Nothing is held any more.
    void stop();
    // One step of the held set: 7 px per 16 ms, 22 with Shift; the timer stops once nothing is held.
    void tick();
    bool isPanning() const;

   private:
    std::function<void(int dx, int dy)> panBy;
    QTimer* panTimer;
    bool panLeft = false;
    bool panRight = false;
    bool panUp = false;
    bool panDown = false;
    bool panShift = false;
  };

}  // namespace stencil::gui
