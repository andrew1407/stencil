#include "toolbarDrags.hpp"
#include "DockZonesOverlay.hpp"
#include "iconDrag.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QPointer>
#include <memory>

// The dialog, canvas and chat icons' drags; the zoom icons' are toolbarDragsZoom.cpp.

namespace stencil::gui {

  using support::IconDragHooks;
  using support::IconDragPoint;

  namespace {
    // Read at the drop, a turn after the release: the widget the release named may be gone.
    bool droppedOn(const QWidget* area, const QPoint& global) {
      const QWidget* under = QApplication::widgetAt(global);
      return area && under && (under == area || area->isAncestorOf(under));
    }

    decltype(IconDragHooks::start) started(DragBegin begin) {
      return [begin](const QPoint&, const QPoint&) { return !begin || begin(); };
    }
  }  // namespace

  void installDialogDrag(QAbstractButton* icon, DragBegin begin, std::function<void(const QPoint&)> open) {
    if (!icon || !open) return;
    IconDragHooks hooks;
    hooks.start = started(std::move(begin));
    hooks.drop = [open](const IconDragPoint& p) { open(p.global); };
    support::installIconDrag(icon, std::move(hooks));
  }

  void installCanvasDrag(QAbstractButton* icon, DragBegin begin, std::function<QWidget*()> canvas,
                         std::function<void()> apply) {
    if (!icon || !canvas || !apply) return;
    auto lit = std::make_shared<QPointer<QWidget>>();   // the canvas wearing the glow
    const auto unlight = [lit] {
      if (*lit) support::markDropTarget(*lit, false);
      lit->clear();
    };
    IconDragHooks hooks;
    hooks.start = [begin, canvas, lit](const QPoint&, const QPoint&) {
      if (begin && !begin()) return false;
      *lit = canvas();
      if (*lit) support::markDropTarget(*lit, true);
      return true;
    };
    hooks.move = [lit](const IconDragPoint& p) {
      QWidget* area = *lit;
      if (area) support::markDropTarget(area, true, p.target && (p.target == area || area->isAncestorOf(p.target)));
    };
    hooks.drop = [lit, unlight, apply](const IconDragPoint& p) {
      const bool on = droppedOn(*lit, p.global);
      unlight();
      if (on) apply();
    };
    hooks.cancel = unlight;
    support::installIconDrag(icon, std::move(hooks));
  }

  void installChatDrag(QAbstractButton* icon, DragBegin begin, ChatPlacing chat) {
    if (!icon || !chat.showZones || !chat.zones || !chat.dock || !chat.floatAt) return;
    auto live = std::make_shared<bool>(false);   // the bands stay up until the drop has read them
    const auto hide = [live, zones = chat.zones] {
      *live = false;
      if (DockZonesOverlay* z = zones()) z->hide();
    };
    IconDragHooks hooks;
    hooks.start = [begin, show = chat.showZones, live](const QPoint&, const QPoint&) {
      if (begin && !begin()) return false;
      *live = true;
      show([live] { return *live; });
      return true;
    };
    hooks.move = [zones = chat.zones](const IconDragPoint& p) {
      if (DockZonesOverlay* z = zones()) z->dragTo(p.global);
    };
    hooks.drop = [chat, hide](const IconDragPoint& p) {
      DockZonesOverlay* z = chat.zones();
      const int zone = z ? z->zoneAt(p.global) : -1;
      hide();
      if (zone >= 0) chat.dock(DockZonesOverlay::area(zone));
      else chat.floatAt(p.global);
    };
    hooks.cancel = hide;
    support::installIconDrag(icon, std::move(hooks));
  }

}  // namespace stencil::gui
