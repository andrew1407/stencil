#pragma once
// Shimmer for context-menu rows over the shared ShimmerOverlay (external-band mode). A
// QAction has no Enter/Leave — QMenu::hovered is the only signal — and each level is its
// own popup, so one overlay per QMenu, parented to it. Header-only, MOC-free.
#include "ShimmerOverlay.hpp"

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
    // Leaving or hiding a level clears its "already swept" mark, so re-entering sweeps again.
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
        // hovered(QAction*) re-fires for the row ALREADY being swept; only a new row restarts.
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
