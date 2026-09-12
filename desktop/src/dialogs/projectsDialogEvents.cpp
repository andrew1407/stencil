// The Projects dialog's event-filter chain minus the list viewport (projectsDialogViewport.cpp):
// the dispatcher itself, the hover-magnify preview's window/Alt handling, Return on a focused
// row, and the inline rename editor's Esc / click-away.
#include "projectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include "serverClient.hpp"
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QTimer>
#include <optional>
#include "../support/controlReveal.hpp"
namespace stencil::gui {

  // A chain of concern-sized handlers, run in THIS order. filterHoverPreview only observes,
  // so every later one still sees the event; an optional-returning handler that answers ends
  // the chain with that verdict, exactly as its own `return` did inline.
  bool ProjectsDialog::eventFilter(QObject* obj, QEvent* ev) {
    filterHoverPreview(obj, ev);
    if (const auto r = filterListKeys(obj, ev)) return *r;
    if (const auto r = filterListViewport(obj, ev)) return *r;
    if (const auto r = filterRenameBox(obj, ev)) return *r;
    return QDialog::eventFilter(obj, ev);
  }

  void ProjectsDialog::filterHoverPreview(QObject* obj, QEvent* ev) {
    // The hover preview is a ToolTip window: switching window or app delivers the list no
    // Leave, and the popup would float over whatever came forward. Verified against the
    // CURSOR — the preview materializing under a stationary pointer makes the platform
    // emit spurious Leave/deactivate, and an unconditional hide loops the dust.
    if (hoverPreview_ && obj == this &&
        (ev->type() == QEvent::WindowDeactivate || ev->type() == QEvent::ApplicationDeactivate) &&
        !(QGuiApplication::applicationState() == Qt::ApplicationActive &&
          pointerOverPreviewedIcon())) {
      hideHoverPreview();
    }
    // Alt pressed/released while the preview is up: re-scale it in place from the
    // source pixmap it carries (held = 2x, released = back to the glance size), and
    // replay its gather — the size change is a real appearance, same as landing on a
    // new row.
    if (hoverPreview_ && hoverPreview_->isVisible() && hoverItem_ &&
        (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) &&
        static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(ev)->isAutoRepeat()) {
      const QPixmap src = hoverPreview_->property("srcPixmap").value<QPixmap>();
      if (!src.isNull()) {
        const int edge = (ev->type() == QEvent::KeyPress) ? kHoverPreviewAltPx : kHoverPreviewPx;
        hoverPreview_->setPixmap(
            src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        hoverPreview_->adjustSize();
        // Re-clamped for the new size (the doubled glance can run off the screen) and
        // re-formed: the size change IS a re-appearance, dust and hold-fade included.
        placeHoverPreview(QCursor::pos());
        revealHoverPreview(hoverItem_);
      }
    }
  }

  std::optional<bool> ProjectsDialog::filterListKeys(QObject* obj, QEvent* ev) {
    // Return/Enter on the focused row = a plain open (with confirmation), and
    // consumed so the dialog's default button can't also fire.
    if (list_ && obj == list_ && ev->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(ev);
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
        if (clickTimer_) clickTimer_->stop();
        openRow(list_->currentItem(), isNewWindowMod(ke->modifiers()), /*confirm=*/true);
        return true;
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

  std::optional<bool> ProjectsDialog::filterRenameBox(QObject* obj, QEvent* ev) {
    // Inline rename editor: Esc cancels (and must NOT fall through to the dialog's
    // own reject), click-away (focus loss) discards — browser blur parity. The ✓/✗
    // buttons are NoFocus, so their clicks land before any focus-out can fire.
    if (renameBox_ && obj->parent() == renameBox_) {
      if (ev->type() == QEvent::KeyPress &&
          static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
        closeInlineRename();
        return true;
      }
      if (ev->type() == QEvent::FocusOut) closeInlineRename();
    }
    return {};   // nothing here answered — the chain goes on
  }

}  // namespace stencil::gui
