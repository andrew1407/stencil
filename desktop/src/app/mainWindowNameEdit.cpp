#include "mainWindow.hpp"
#include <QLineEdit>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "projectTransferController.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"

#include <QAction>
#include <QApplication>
#include <QVariant>

// Editing the project name in place: enter, commit, cancel.

namespace stencil::gui {

  // Qt has no `visibility: hidden` — a hidden widget leaves its layout; an opacity effect paints it out while it keeps its slot.
  void MainWindow::setPaintedOut(QWidget* w, bool out) {
    static constexpr const char* STATE_PROP = "stencilPaintedOut";
    if (!w) return;
    // The LOGICAL state lives in a property: a veil mid-flight reads as "half out" and desynced the transitions.
    const QVariant prev = w->property(STATE_PROP);
    const bool changed = !prev.isValid() || prev.toBool() != out;
    w->setProperty(STATE_PROP, out);
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    // Same state again: touch nothing, or an unrelated refresh snaps a forming veil to its end value.
    if (!changed && fx) return;
    if (!prev.isValid() || !changed || !support::isDustMotionOk() || !w->isVisible()
        || w->width() < 8 || w->height() < 8) {
      if (!fx) {
        fx = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(fx);
      }
      fx->setOpacity(out ? 0.0 : 1.0);
      return;
    }
    // One flight per control; settleReveal drops the previous one first.
    paintRevealInPlace(w, this, out);
  }

  // GEOMETRIC, not underMouse(): the three widgets have gaps between them, and every gap flickered the hover.
  void MainWindow::updateNameHover() {
    // The container's own rect (browser .project-name-field parity), gaps INSIDE it.
    const bool over = nameBar_.group && nameBar_.group->isVisible()
        && nameBar_.group->rect().contains(nameBar_.group->mapFromGlobal(QCursor::pos()));
    if (over != nameBar_.hover) {
      nameBar_.hover = over;
      refreshProjectNameButtons();
    }
  }

  // Exactly one override on the stack, ever: the flag, not the caller, decides.
  void MainWindow::setBlockedCursor(bool on) {
    if (on == blockedCursorOn_) return;
    if (on) QApplication::setOverrideCursor(Qt::ForbiddenCursor);
    else QApplication::restoreOverrideCursor();
    blockedCursorOn_ = on;
  }

  void MainWindow::setActionTip(QAction* a, const QString& desc) {
    // tipContent keeps "desc (shortcut)" + the disabled reason composed (browser composeControlTitle).
    setTipBase(a, desc);
  }

  void MainWindow::enterNameEdit() {
    if (!nameBar_.field || !nameBar_.field->isEnabled() || nameBar_.editing) return;
    nameBar_.editing = true;
    nameBar_.field->setReadOnly(false);
    applyProjectNameStyle(true);  // show the accent-outlined input look
    nameBar_.field->setFocus();
    nameBar_.field->selectAll();
    refreshProjectNameButtons();  // reveal ✓/✗, hide ✎
  }

  void MainWindow::commitProjectName() {
    const QString newName = nameBar_.field->text().trimmed();
    // Server-linked session: push the rename to the server, version-guarded (mirrors setActiveProjectColor); else rename locally.
    if (!remoteSession_->link().id.isEmpty()) {
      stencil::net::ServerClient* c = connections_ ? connections_->find(remoteSession_->link().address) : nullptr;
      if (!newName.isEmpty() && newName != remoteSession_->link().name && c) {
        QPointer<MainWindow> self(this);
        const QString id = remoteSession_->link().id;
        remoteSession_->putVersionGuardedAsync(
            c, id,
            [c, id, newName](qint64 version, std::function<void(bool, qint64, bool)> cb) {
              c->updateProjectNameAsync(id, newName, version, cb);
            },
            [this, self, c, newName](bool ok, qint64 newVersion) {
              if (!self) return;
              if (ok) {
                remoteSession_->link().name = newName;
                remoteSession_->link().version = newVersion;
                notify_->success(QString("Renamed to \"%1\"").arg(newName));
              } else {
                notify_->error(QString("Rename failed: %1").arg(c->lastError()));
              }
              updateProjectTitle();   // reflect the stored name (renamed, or reverted on failure)
            });
      }
    } else if (!activeProjectId_.isEmpty()) {
      renameProjectById(activeProjectId_, nameBar_.field->text());
    }
    nameBar_.editing = false;   // leave edit mode → field back to read-only, ✎ returns
    nameBar_.field->clearFocus();
    updateProjectTitle();   // force the field/title back to the stored name
  }

  void MainWindow::cancelProjectName() {
    nameBar_.editing = false;   // leave edit mode
    nameBar_.field->clearFocus();
    updateProjectTitle();   // revert the field to the stored name
  }

}  // namespace stencil::gui
