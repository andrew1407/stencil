#pragma once
// Hover "glass shimmer" for context-menu rows — a thin QMenu adapter over the shared
// ShimmerOverlay (support/shimmerOverlay.hpp, external-band mode). A QAction has no
// Enter/Leave of its own — QMenu::hovered is the only signal, exactly the constraint
// menuHotkeys.hpp already works around — so each row's sweep is triggered off
// hovered(QAction*) and swept over its actionGeometry(); one overlay per menu level
// (submenus are their own popup widgets, not children of the root menu's rect).
//
// Header-only, MOC-free. Construct as menuHotkeys.hpp's stack-local sibling right
// after the menu, or heap-parented (wireMenuRowPolish passes the menu as parent).
// Each level's overlay is a plain child QWidget of that QMenu, so it lives and dies
// with it.
#include "shimmerOverlay.hpp"

#include <QAction>
#include <QEvent>
#include <QHash>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QWidgetAction>

namespace stencil::support {

  class MenuShimmer : public QObject {
   public:
    explicit MenuShimmer(QMenu* root, QObject* parent = nullptr) : QObject(parent) {
      wire(root);
    }

   protected:
    // A fast pass over many rows must not leave sweeps replay-blocked: leaving or
    // hiding a level clears its "already swept this row" mark, so re-entering the
    // same row sweeps again (the overlay itself cancels its animation on Leave/Hide).
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() == QEvent::Leave || e->type() == QEvent::Hide) current_.remove(o);
      return QObject::eventFilter(o, e);
    }

   private:
    void wire(QMenu* menu) {
      auto* overlay = new gui::ShimmerOverlay(menu, nullptr, /*externalBands=*/true);
      overlay->setObjectName(QStringLiteral("menuShimmerOverlay"));  // the GUI test's hook
      menu->installEventFilter(this);
      connect(menu, &QMenu::hovered, this, [this, overlay, menu](QAction* a) {
        // hovered(QAction*) can re-fire for the row ALREADY being swept (Qt's own
        // sloppy-hover bookkeeping) — only a genuinely new row restarts the sweep.
        QPointer<QAction>& cur = current_[menu];
        if (a == cur) return;
        cur = a;
        const QRect band = menu->actionGeometry(a);
        if (band.isValid()) overlay->sweepBand(band);
      });
      for (QAction* a : menu->actions()) {
        if (a->isSeparator() || qobject_cast<QWidgetAction*>(a)) continue;
        if (QMenu* sub = a->menu()) wire(sub);   // descend — every submenu too
      }
    }

    QHash<QObject*, QPointer<QAction>> current_;   // hovered row, per menu level
  };

}  // namespace stencil::support
