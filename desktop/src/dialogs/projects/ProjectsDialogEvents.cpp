// The Projects dialog's event-filter chain minus the list viewport (ProjectsDialogViewport.cpp).
#include "ProjectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include "ServerClient.hpp"
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QTimer>
#include <optional>
#include "../../support/control/reveal/controlReveal.hpp"
namespace stencil::gui {

  // Run in THIS order; filterHoverPreview only observes, an answering handler ends the chain.
  bool ProjectsDialog::eventFilter(QObject* obj, QEvent* ev) {
    filterHoverPreview(obj, ev);
    if (const auto r = filterListKeys(obj, ev)) return *r;
    if (const auto r = filterListViewport(obj, ev)) return *r;
    if (const auto r = filterRenameBox(obj, ev)) return *r;
    return QDialog::eventFilter(obj, ev);
  }

  void ProjectsDialog::filterHoverPreview(QObject* obj, QEvent* ev) {
    // A ToolTip window: switching window/app delivers the list no Leave. Verified against the CURSOR —
    // the preview materializing under a stationary pointer makes the platform emit spurious Leave/deactivate.
    if (hover.hoverPreview && obj == this &&
        (ev->type() == QEvent::WindowDeactivate || ev->type() == QEvent::ApplicationDeactivate) &&
        !(QGuiApplication::applicationState() == Qt::ApplicationActive &&
          pointerOverPreviewedIcon())) {
      hideHoverPreview();
    }
    // Alt while the preview is up re-scales it from the source pixmap it carries (held = 2x) and
    // replays its gather — the size change is a real appearance.
    if (hover.hoverPreview && hover.hoverPreview->isVisible() && hover.hoverItem &&
        (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) &&
        static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(ev)->isAutoRepeat()) {
      const QPixmap src = hover.hoverPreview->property("srcPixmap").value<QPixmap>();
      if (!src.isNull()) {
        const int edge = (ev->type() == QEvent::KeyPress) ? HOVER_PREVIEW_ALT_PX : HOVER_PREVIEW_PX;
        hover.hoverPreview->setPixmap(
            src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        hover.hoverPreview->adjustSize();
        placeHoverPreview(QCursor::pos());
        revealHoverPreview(hover.hoverItem);
      }
    }
  }

  std::optional<bool> ProjectsDialog::filterListKeys(QObject* obj, QEvent* ev) {
    // Consumed so the dialog's default button cannot also fire.
    if (list && obj == list && ev->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(ev);
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
        if (press.clickTimer) press.clickTimer->stop();
        openRow(list->currentItem(), isNewWindowMod(ke->modifiers()), /*confirm=*/true);
        return true;
      }
    }
    return {};
  }

  std::optional<bool> ProjectsDialog::filterRenameBox(QObject* obj, QEvent* ev) {
    // Esc must NOT fall through to the dialog's reject; focus loss discards (browser blur parity).
    // The ✓/✗ buttons are NoFocus, so their clicks land before any focus-out can fire.
    if (renameBox && obj->parent() == renameBox) {
      if (ev->type() == QEvent::KeyPress &&
          static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
        closeInlineRename();
        return true;
      }
      if (ev->type() == QEvent::FocusOut) closeInlineRename();
    }
    return {};
  }

}  // namespace stencil::gui
