#pragma once
#include <optional>

class QEvent;
class QObject;

namespace stencil::gui {

  class MainWindow;

  // The window's event-filter chain, one stage per concern: pointer chrome, the docks, key
  // claims, popover gestures and buttons, zoom and the logo, the canvas viewport and the name bar.
  // A void stage observes, an answering one ends the chain; it routes, it holds no state.
  class WindowEvents {
   public:
    explicit WindowEvents(MainWindow& w) : w(w) {}

    void filterPointerChrome(QObject* obj, QEvent* event);
    void filterDockChrome(QObject* obj, QEvent* event);
    std::optional<bool> filterKeyClaims(QObject* obj, QEvent* event);
    std::optional<bool> filterPopoverGestures(QObject* obj, QEvent* event);
    std::optional<bool> filterPopoverButton(QObject* obj, QEvent* event);
    std::optional<bool> filterZoomAndLogo(QObject* obj, QEvent* event);
    std::optional<bool> filterCanvasViewport(QObject* obj, QEvent* event);
    std::optional<bool> filterProjectNameBar(QObject* obj, QEvent* event);

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
