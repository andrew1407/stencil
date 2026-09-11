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

  // Qt has no `visibility: hidden` — a hidden widget leaves its layout, taking its space with
  // it. An opacity effect paints the widget out while it keeps its slot, which is what the
  // browser's hover-revealed affordances do.
  void MainWindow::setPaintedOut(QWidget* w, bool out) {
    static constexpr const char* kStateProp = "stencilPaintedOut";
    if (!w) return;
    // The LOGICAL state lives in a property, not in the effect's opacity: a veil
    // animation mid-flight reads as "half out", and detecting transitions off that
    // desynced them so the dust only ever played once.
    const QVariant prev = w->property(kStateProp);
    const bool changed = !prev.isValid() || prev.toBool() != out;
    w->setProperty(kStateProp, out);
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    // Same state again (updateProjectTitle & co. refresh liberally): touch nothing — a
    // forming icon's veil must not be snapped to its end value by an unrelated refresh.
    if (!changed && fx) return;
    if (!prev.isValid() || !changed || !support::dustMotionOk() || !w->isVisible()
        || w->width() < 8 || w->height() < 8) {
      if (!fx) {
        fx = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(fx);
      }
      fx->setOpacity(out ? 0.0 : 1.0);
      return;
    }
    // The shared in-place mark flight: specks over a veiled face, one flight per
    // control (hover can flicker — settleReveal drops the previous one first).
    paintRevealInPlace(w, this, out);
  }

  // Recompute hover state over the name group (field + ✎ + 🎨). GEOMETRIC, not
  // underMouse(): the three are separate toolbar widgets with gaps between them, and
  // underMouse() dropped out in every gap — a slow sweep flickered the hover,
  // replaying the reveal and deleting each dust cloud before a frame of it painted
  // ("laggy, replaying, no dust"). One padded union rect is stable.
  void MainWindow::updateNameHover() {
    // The container's own rect (browser .project-name-field parity): one solid box with
    // the inter-widget gaps INSIDE it, so the sweep can never flicker the hover.
    const bool over = nameBar_.group && nameBar_.group->isVisible()
        && nameBar_.group->rect().contains(nameBar_.group->mapFromGlobal(QCursor::pos()));
    if (over != nameBar_.hover) {
      nameBar_.hover = over;
      refreshProjectNameButtons();
    }
  }

  // Exactly one override on the stack, ever: push/pop pairs are the whole risk of this
  // approach, so the flag — not the caller — decides whether anything happens.
  void MainWindow::setBlockedCursor(bool on) {
    if (on == blockedCursorOn_) return;
    if (on) QApplication::setOverrideCursor(Qt::ForbiddenCursor);
    else QApplication::restoreOverrideCursor();
    blockedCursorOn_ = on;
  }

  void MainWindow::setActionTip(QAction* a, const QString& desc) {
    // tipContent composes "desc (shortcut)" + the disabled reason, and keeps it composed
    // as the action's state or chord changes (browser composeControlTitle).
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
    // Server-linked session (no local id): push the rename straight to the server so peers see it
    // live, version-guarded — mirrors setActiveProjectColor's remote branch. Otherwise rename the
    // local project. (Previously a server project couldn't be renamed at all from the toolbar.)
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
