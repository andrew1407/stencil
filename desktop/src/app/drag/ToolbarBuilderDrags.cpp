#include "MainWindow.hpp"
#include "ToolbarBuilder.hpp"
#include "toolbarDrags.hpp"
#include "CanvasWidget.hpp"
#include "DockZonesOverlay.hpp"
#include "Notifications.hpp"
#include "modalReveal.hpp"   // DialogLanding

#include <QApplication>
#include <QDialog>
#include <QScrollArea>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

// The toolbar icons' drags reaching into the window: which icons drag, and what each drop opens,
// places or applies. Every other toolbar icon has no drag. Browser twin:
// browser/js/ui/bindings/controls/toolbarDrags.js.

namespace stencil::gui {

  void ToolbarBuilder::buildIconDrags() {
    QList<QToolButton*> icons;
    for (QToolBar* bar : w.findChildren<QToolBar*>()) icons += bar->findChildren<QToolButton*>();
    const auto iconFor = [&icons](QAction* act) -> QToolButton* {
      for (QToolButton* b : icons)
        if (act && b->defaultAction() == act) return b;
      return nullptr;
    };
    // The drag takes the gesture over: no deferred click or double-click fires after it, the icon's
    // own Alt peek no longer closes on Alt's release, and a popover icon's release clicks nothing.
    const auto begin = [this](QToolButton* icon) -> DragBegin {
      return [this, icon = QPointer<QToolButton>(icon)] {
        if (!icon || QApplication::activeModalWidget()) return false;
        QAction* own = w.pop.buttons.value(icon.data(), nullptr);
        if (w.pop.clickTimer) w.pop.clickTimer->stop();
        w.pop.pendingAction.clear();
        w.pop.dblClickAction.clear();
        if (own && w.pop.peekAction == own) w.pop.peekAction.clear();
        if (own) w.pop.swallowRelease = true;
        return true;
      };
    };

    // The icons a click opens a dialog with: the popover's set, but the chat, and crop. A popover
    // still up unwinds first, so the full window opens once its loop has returned.
    QSet<QAction*> dialogs = w.pop.dialogActions;
    dialogs.remove(w.acts.chat);
    dialogs.insert(w.acts.crop);
    for (QToolButton* icon : icons) {
      QAction* act = icon->defaultAction();
      if (!act || !dialogs.contains(act)) continue;
      installDialogDrag(icon, begin(icon), [this, act](const QPoint& at) {
        const auto open = [this, act = QPointer<QAction>(act), at] {
          if (w.tearingDown || !act || !act->isEnabled()) return;
          w.pop.anchor.clear();   // the full modal a click opens, never the compact popover
          const support::DialogLanding landing(at);
          act->trigger();
        };
        QDialog* up = w.pop.active.data();
        if (!up) return open();
        QObject::connect(up, &QObject::destroyed, &w, [this, open] { QTimer::singleShot(0, &w, open); },
                         Qt::SingleShotConnection);
        w.dismissPopover();
      });
    }

    if (QToolButton* chat = iconFor(w.acts.chat)) {
      ChatPlacing placing;
      placing.showZones = [this](std::function<bool()> live) {
        w.parts.dockChrome.showChatDockZones(std::move(live));
      };
      placing.zones = [this] { return static_cast<DockZonesOverlay*>(w.overlays.dockZones); };
      // A placement is the user's own layout: no lingering peek closes it after.
      placing.dock = [this](Qt::DockWidgetArea area) {
        w.parts.popoverGestures.stopLingerPoll();
        w.parts.dockChrome.openChatDocked(area);
      };
      placing.floatAt = [this](const QPoint& at) {
        w.parts.popoverGestures.stopLingerPoll();
        w.parts.dockChrome.openChatFloatingAt(at);
      };
      installChatDrag(chat, begin(chat), std::move(placing));
    }

    const auto canvas = [this]() -> QWidget* { return w.scroll; };
    QToolButton* clear = iconFor(w.acts.clearAll);
    // The drop is the decision: no confirm, still one undo step and the confirmed clear's notice.
    installCanvasDrag(clear, begin(clear), canvas, [this] {
      if (!w.acts.clearAll->isEnabled()) return;
      w.canvas->clearAll();
      if (w.notify) w.notify->success(MainWindow::tr("All lines cleared"));
    });
    for (QAction* act : {w.acts.rotateLeft, w.acts.rotateRight, w.acts.flipImage}) {
      QToolButton* icon = iconFor(act);
      installCanvasDrag(icon, begin(icon), canvas, [act] {
        if (act->isEnabled()) act->trigger();
      });
    }

    ZoomView view;
    view.zoom = [this] { return w.canvas->getScale(); };
    view.hold = [this] {
      const EditorView::ViewAnchor from = w.parts.view.viewAnchor();
      ZoomHold held;
      held.at = [this, from](double z) {
        EditorView::ViewAnchor at = from;
        at.scale = z;
        at.fit = false;
        w.parts.view.restoreAnchor(at);
      };
      held.restore = [this, from] { w.parts.view.restoreAnchor(from); };
      return held;
    };
    QToolButton* out = iconFor(w.acts.zoomOut);
    QToolButton* in = iconFor(w.acts.zoomIn);
    installZoomDrag(out, begin(out), -1, view);
    installZoomDrag(in, begin(in), +1, view);
    installFitDrag(w.tools.zoomFitBtn, out, in, begin(w.tools.zoomFitBtn), view);
  }

}  // namespace stencil::gui
