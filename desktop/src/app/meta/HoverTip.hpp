#pragma once
#include <QPoint>
#include <QString>
#include <Qt>
#include <functional>

class QTimer;

namespace stencil::gui {

  class MainWindow;

  // Browser tooltip.js over the canvas: what the pointer is on (a point, a line, the page) and
  // when its tooltip shows, the wait before it and the key that says it is still the same.
  class HoverTip {
   public:
    explicit HoverTip(MainWindow& w) : w(w) {}

    void onHoverDetail(double imageX, double imageY, const QPoint& globalPos,
                       Qt::KeyboardModifiers mods, bool immediate = false);
    // Browser tooltip.js scheduleShow; `immediate` skips the wait outright.
    void scheduleHoverShow(const QString& key, std::function<void()> revealFn, bool immediate);
    void hideHoverTooltip();

    QTimer* hoverTooltipTimer = nullptr;
    QString hoverPendingKey;
    QString hoverShownKey;
    std::function<void()> hoverPendingReveal;

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
