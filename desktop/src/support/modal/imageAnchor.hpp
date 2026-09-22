#pragma once
// The open-image flow's flight anchors, the twin of browser/js/ui/modal/imageAnchor.js: a window
// or confirm about opening an image grows out of the CANVAS CENTRE whatever gesture raised it,
// and an answer that opens an image pours back into the toolbar's Open control.
#include "modalChrome.hpp"

#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QWidget>

namespace stencil::gui {

  inline constexpr int IMAGE_ANCHOR_PX = 40;   // side of the box a flight grows out of or into
  // The canvas area, then the Open control's two halves, which refreshActions swaps on hasImage.
  inline constexpr char CANVAS_VIEWPORT_NAME[] = "canvasViewport";   // browser #canvas-viewport
  inline constexpr char OPEN_ANOTHER_BTN_NAME[] = "openAnotherImageBtn";
  inline constexpr char OPEN_IMAGE_BTN_NAME[] = "openImageBtn";

  inline QRect imageAnchorBox(const QPoint& centre) {
    return QRect(centre - QPoint(IMAGE_ANCHOR_PX / 2, IMAGE_ANCHOR_PX / 2),
                 QSize(IMAGE_ANCHOR_PX, IMAGE_ANCHOR_PX));
  }

  inline QRect liveWidgetRect(QWidget* host, const char* name) {
    QWidget* w = host ? host->findChild<QWidget*>(QLatin1String(name)) : nullptr;
    // A hidden half of a swapped pair measures nothing, as a display:none element does.
    if (!w || !w->isVisible() || w->width() < 1 || w->height() < 1) return {};
    return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
  }

  // GLOBAL box on the canvas viewport's centre. Valid with no image open; before the window is
  // laid out the viewport measures nothing, so the window's own centre stands in.
  inline QRect canvasAnchorRect(QWidget* host) {
    QWidget* win = host ? host->window() : nullptr;
    const QRect canvas = liveWidgetRect(win, CANVAS_VIEWPORT_NAME);
    if (canvas.isValid()) return imageAnchorBox(canvas.center());
    if (!win || win->width() < 1 || win->height() < 1) return {};
    return imageAnchorBox(QRect(win->mapToGlobal(QPoint(0, 0)), win->size()).center());
  }

  inline QRect openImageAnchorRect(QWidget* host) {
    QWidget* win = host ? host->window() : nullptr;
    const QRect shown = liveWidgetRect(win, OPEN_ANOTHER_BTN_NAME);
    if (shown.isValid()) return shown;   // whichever half shows — the pair may be mid-swap
    const QRect empty = liveWidgetRect(win, OPEN_IMAGE_BTN_NAME);
    return empty.isValid() ? empty : canvasAnchorRect(host);
  }

  // Browser openImageConfirmAnchors: out of the canvas, into the Open control if the answer opened one.
  inline FlightAnchors openImageConfirmFlight(QWidget* host) {
    QPointer<QWidget> owner(host);
    FlightAnchors flight;
    flight.openRect = canvasAnchorRect(host);
    flight.closeRectFor = [owner](bool opened) {
      return opened ? openImageAnchorRect(owner.data()) : canvasAnchorRect(owner.data());
    };
    return flight;
  }

}  // namespace stencil::gui
