#include "ProjectsDialog.hpp"

#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ProjectsDialog.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/displayName.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "AppTooltip.hpp"
#include "ShimmerOverlay.hpp"

#include <QApplication>
#include <QPointer>
#include <QTimer>

// Opening a row: the click schedule, the selection paths and the hand-off.

namespace stencil::gui {

  void ProjectsDialog::scheduleRowOpen(QListWidgetItem* it) {
    if (press.rowDragging || press.pressOnCheck || !it || it->data(Qt::UserRole).isNull()) return;
    press.pendingRow = list->row(it);
    press.pendingNewWindow = isNewWindowMod(press.pressMods);
    if (!press.clickTimer) {
      press.clickTimer = new QTimer(this);
      press.clickTimer->setSingleShot(true);
      connect(press.clickTimer, &QTimer::timeout, this, &ProjectsDialog::fireRowOpen);
    }
    // Wait out the platform's double-click window before acting.
    press.clickTimer->start(QApplication::doubleClickInterval());
  }

  void ProjectsDialog::fireRowOpen() {
    // The double click that cancelled us may already have accepted the dialog
    // (the second release re-emits itemClicked, re-arming this timer).
    if (!isVisible()) return;
    openRow(list->item(press.pendingRow), press.pendingNewWindow, /*confirm=*/true);
  }

  void ProjectsDialog::openRow(QListWidgetItem* it, bool newWindow, bool confirm) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    list->setCurrentItem(it);
    confirmOpen = confirm;
    // Remote rows have no new-window path (same rule as the drag-out zones and
    // the ⋯ menu) — open them here instead of doing nothing.
    const bool remote = !it->data(Qt::UserRole + 1).toString().isEmpty();
    if (newWindow && !remote) openSelectedInNewWindow();
    else openSelected();
  }

  void ProjectsDialog::openSelected() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    selectedId = it->data(Qt::UserRole).toString();
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (!server.isEmpty()) {  // golden remote row → fetch + open from the server
      selectedServerUrl = server;
      finishOpen(Action::OPEN_REMOTE, /*newWindow=*/false,
                 it->data(Qt::UserRole + 3).toString());
      return;
    }
    finishOpen(Action::OPEN, /*newWindow=*/false, it->data(Qt::UserRole + 3).toString());
  }

  void ProjectsDialog::openSelectedInNewWindow() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    // New-window / delete / rename / renew apply to LOCAL projects only.
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;
    selectedId = it->data(Qt::UserRole).toString();
    finishOpen(Action::OPEN_IN_NEW_WINDOW, /*newWindow=*/true,
               it->data(Qt::UserRole + 3).toString());
  }

  // The open-confirm sits OVER the still-open dialog (browser parity). Deferred a turn so a drag
  // release or menu click in the same turn cannot dismiss the question. A double click skips it.
  void ProjectsDialog::finishOpen(Action act, bool newWindow, const QString& name) {
    if (!confirmOpen) {
      action = act;
      accept();
      return;
    }
    const QString nm = support::shortName(name.isEmpty() ? tr("Untitled") : name);
    QPointer<ProjectsDialog> self(this);
    QTimer::singleShot(0, this, [this, self, act, newWindow, nm, closeTo = hover.menuKebabRect] {
      if (!self) return;
      ConfirmSpec spec;
      spec.flight.closeRect = closeTo;
      spec.title = tr("Open project");
      spec.message = newWindow
          ? tr("Open \"%1\" in a new window?").arg(nm)
          : tr("Open \"%1\"? Any unsaved changes in the current window will be replaced.").arg(nm);
      spec.confirmLabel = tr("Open");
      spec.confirmIcon = QStringLiteral("folder");
      if (!confirmModal(this, spec)) return;   // cancelled — the list stays up
      if (!self) return;
      confirmOpen = false;   // answered here — the owner must not ask again
      action = act;
      accept();
    });
  }

}  // namespace stencil::gui
