#include "ProjectTitleController.hpp"
#include "Notifications.hpp"
#include "ProjectNameBar.hpp"
#include "RemoteSession.hpp"
#include "RemoteState.hpp"
#include "ServerClient.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/drag/colorDrag.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/motionPrefs.hpp"

#include <QCursor>
#include <QGraphicsOpacityEffect>
#include <QLineEdit>

// Editing the project name in place: enter, commit, cancel, and the chips' hover.

namespace stencil::gui {

  // Qt has no `visibility: hidden` — a hidden widget leaves its layout; an opacity effect paints it out while it keeps its slot.
  void ProjectTitleController::setPaintedOut(QWidget* widget, bool out) {
    static constexpr const char* STATE_PROP = support::PAINTED_OUT_PROPERTY;
    if (!widget) return;
    // The LOGICAL state lives in a property: a veil mid-flight reads as "half out" and desynced the transitions.
    const QVariant prev = widget->property(STATE_PROP);
    const bool changed = !prev.isValid() || prev.toBool() != out;
    widget->setProperty(STATE_PROP, out);
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
    // Same state again: touch nothing, or an unrelated refresh snaps a forming veil to its end value.
    if (!changed && fx) return;
    if (!prev.isValid() || !changed || !support::isDustMotionOk() || !widget->isVisible()
        || widget->width() < 8 || widget->height() < 8) {
      if (!fx) fx = veilBehindDust(widget);
      fx->setOpacity(out ? 0.0 : 1.0);
      return;
    }
    // One flight per control; settleReveal drops the previous one first.
    paintRevealInPlace(widget, host, out);
  }

  // GEOMETRIC, not underMouse(): the three widgets have gaps between them, and every gap flickered the hover.
  void ProjectTitleController::updateNameHover() {
    // The container's own rect (browser .project-name-field parity), gaps INSIDE it.
    const bool over = nameBar.group && nameBar.group->isVisible()
        && nameBar.group->rect().contains(nameBar.group->mapFromGlobal(QCursor::pos()));
    if (over != nameBar.hover) {
      nameBar.hover = over;
      refreshProjectNameButtons();
    }
  }

  void ProjectTitleController::enterNameEdit() {
    if (!nameBar.field || !nameBar.field->isEnabled() || nameBar.editing) return;
    nameBar.editing = true;
    nameBar.field->setReadOnly(false);
    applyProjectNameStyle(true);  // show the accent-outlined input look
    nameBar.field->setFocus();
    nameBar.field->selectAll();
    refreshProjectNameButtons();  // reveal ✓/✗, hide ✎
  }

  void ProjectTitleController::commitProjectName() {
    const QString newName = nameBar.field->text().trimmed();
    // Server-linked session: push the rename to the server, version-guarded (mirrors setActiveProjectColor); else rename locally.
    if (!remote.session->getLink().id.isEmpty()) {
      stencil::net::ServerClient* c = remote.connections ? remote.connections->find(remote.session->getLink().address) : nullptr;
      if (!newName.isEmpty() && newName != remote.session->getLink().name && c) {
        QPointer<ProjectTitleController> self(this);
        const QString id = remote.session->getLink().id;
        remote.session->putVersionGuardedAsync(
            c, id,
            [c, id, newName](qint64 version, std::function<void(bool, qint64, bool)> cb) {
              c->updateProjectNameAsync(id, newName, version, cb);
            },
            [this, self, c, newName](bool ok, qint64 newVersion) {
              if (!self) return;
              if (ok) {
                remote.session->getLink().name = newName;
                remote.session->getLink().version = newVersion;
                notify->success(QString("Renamed to \"%1\"").arg(newName));
              } else {
                notify->error(QString("Rename failed: %1").arg(c->lastError()));
              }
              updateProjectTitle();   // reflect the stored name (renamed, or reverted on failure)
            });
      }
    } else if (!activeProjectId.isEmpty()) {
      h.renameLocal(activeProjectId, nameBar.field->text());
    }
    nameBar.editing = false;   // leave edit mode → field back to read-only, ✎ returns
    nameBar.field->clearFocus();
    updateProjectTitle();   // force the field/title back to the stored name
  }

  void ProjectTitleController::cancelProjectName() {
    nameBar.editing = false;   // leave edit mode
    nameBar.field->clearFocus();
    updateProjectTitle();   // revert the field to the stored name
  }
}  // namespace stencil::gui
