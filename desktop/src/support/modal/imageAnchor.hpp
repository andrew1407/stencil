#pragma once
// The open-image flow's flight anchors, the twin of browser/js/ui/modal/imageAnchor.js: a window
// or confirm about opening an image grows out of the CANVAS CENTRE whatever gesture raised it,
// and an answer that opens an image pours back into the toolbar's Open control.
#include "modalChrome.hpp"

#include <QAction>
#include <QLayout>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QToolButton>
#include <QVector>
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

  // The sweep runs after the dialog hides, so the cluster is held in the state the flight lands
  // in for one measure (browser: settledRect).
  inline QRect settledRectIn(QWidget* win, bool imageOpen, QWidget* of) {
    QWidget* icon = win ? win->findChild<QWidget*>(QLatin1String(OPEN_ANOTHER_BTN_NAME)) : nullptr;
    QWidget* big = win ? win->findChild<QWidget*>(QLatin1String(OPEN_IMAGE_BTN_NAME)) : nullptr;
    QWidget* row = icon ? icon->parentWidget() : nullptr;
    if (!row || !of) return {};
    QVector<QPair<QWidget*, bool>> held;
    const auto put = [&held](QWidget* w, bool on) {
      if (!w || w->isVisible() == on) return;
      held.append({w, w->isVisible()});
      w->setVisible(on);
    };
    const auto relayout = [row, win] {
      if (row->layout()) row->layout()->activate();
      if (win->layout()) win->layout()->activate();
    };
    // With an image, every icon its action allows; with none, the big button alone.
    for (QToolButton* b : row->findChildren<QToolButton*>())
      if (b != big) put(b, imageOpen && (!b->defaultAction() || b->defaultAction()->isVisible()));
    put(big, !imageOpen);
    relayout();
    const QRect r(of->mapToGlobal(QPoint(0, 0)), of->size());
    for (auto it = held.crbegin(); it != held.crend(); ++it) it->first->setVisible(it->second);
    relayout();
    return (r.width() > 0 && r.height() > 0) ? r : QRect();
  }

  inline QRect openImageAnchorRect(QWidget* host) {
    QWidget* win = host ? host->window() : nullptr;
    QWidget* icon = win ? win->findChild<QWidget*>(QLatin1String(OPEN_ANOTHER_BTN_NAME)) : nullptr;
    const QRect settled = settledRectIn(win, /*imageOpen=*/true, icon);
    if (settled.isValid()) return settled;
    const QRect shown = liveWidgetRect(win, OPEN_ANOTHER_BTN_NAME);
    if (shown.isValid()) return shown;   // whichever half shows — the pair may be mid-swap
    const QRect empty = liveWidgetRect(win, OPEN_IMAGE_BTN_NAME);
    return empty.isValid() ? empty : canvasAnchorRect(host);
  }

  // Where a control lands once the editor is EMPTY — the shrinking cluster slides the row.
  inline QRect emptiedControlRect(QWidget* host, QWidget* control) {
    QWidget* win = host ? host->window() : nullptr;
    const QRect settled = settledRectIn(win, /*imageOpen=*/false, control);
    if (settled.isValid()) return settled;
    if (!control || !control->isVisible()) return canvasAnchorRect(host);
    return QRect(control->mapToGlobal(QPoint(0, 0)), control->size());
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
