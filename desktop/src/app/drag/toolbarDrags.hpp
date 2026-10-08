#pragma once
// What dragging a toolbar icon does, each over support/drag/iconDrag: the chat icon docks or floats
// the chat, a dialog icon opens its dialog where it drops, a canvas icon applies on the canvas, and
// zoom −/+ and fit zoom by where the pointer goes. The window lends only these hooks.
// Browser twins: browser/js/ui/drag/modalDrag.js, canvasDrop.js, chatDrag.js and zoomDrag.js.
#include <QPoint>
#include <Qt>
#include <functional>

class QAbstractButton;
class QWidget;

namespace stencil::gui {

  class DockZonesOverlay;

  // Asked as a drag starts; false leaves the press a press.
  using DragBegin = std::function<bool()>;

  // Dropped away from the icon, `open` runs with the drop point (GLOBAL).
  void installDialogDrag(QAbstractButton* icon, DragBegin begin, std::function<void(const QPoint&)> open);

  // `canvas` glows while the drag lasts; dropped on it `apply` runs, anywhere else nothing does.
  void installCanvasDrag(QAbstractButton* icon, DragBegin begin, std::function<QWidget*()> canvas,
                         std::function<void()> apply);

  // Where the chat can go: the dock bands (shown while `live` holds), a side, a float at a point.
  struct ChatPlacing {
    std::function<void(std::function<bool()> live)> showZones;
    std::function<DockZonesOverlay*()> zones;
    std::function<void(Qt::DockWidgetArea)> dock;
    std::function<void(const QPoint&)> floatAt;
  };

  // Dropped on a band the chat docks on its side; anywhere else away from the icon it floats there.
  void installChatDrag(QAbstractButton* icon, DragBegin begin, ChatPlacing chat);

  // The view a zoom drag holds from its start: shown at another zoom about that view's centre
  // (held to the zoom range, the % field following), or put back as it was.
  struct ZoomHold {
    std::function<void(double)> at;
    std::function<void()> restore;
  };

  // The window's zoom as a drag reads and holds it.
  struct ZoomView {
    std::function<double()> zoom;
    std::function<ZoomHold()> hold;
  };

  // `sign` +1 zooms in, -1 out, by the pointer's distance from the icon's centre.
  void installZoomDrag(QAbstractButton* icon, DragBegin begin, int sign, ZoomView view);

  // `out` and `in` glow; resting on either steps the zoom its way, and released on `fit` it is restored.
  void installFitDrag(QAbstractButton* fit, QAbstractButton* out, QAbstractButton* in, DragBegin begin,
                      ZoomView view);

}  // namespace stencil::gui
