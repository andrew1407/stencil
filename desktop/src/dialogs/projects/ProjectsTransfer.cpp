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

#include <QPalette>
#include <QTimer>

// Deleting rows and moving or copying them between local and a server.

namespace stencil::gui {

  void ProjectsDialog::deleteSelected() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local only
    const QString id = it->data(Qt::UserRole).toString();
    const QString nm = support::shortName(it->data(Qt::UserRole + 3).toString());
    // Confirm on the NEXT turn, over the still-open dialog: the drag-out Remove zone
    // lands here from a drag release, which dismisses a box shown in the same turn.
    QTimer::singleShot(0, this, [this, id, nm, closeTo = hover.menuKebabRect] {
      ConfirmSpec spec;
      spec.flight.closeRect = closeTo;
      spec.title = tr("Remove project");
      spec.message = tr("Remove \"%1\"? This cannot be undone.").arg(nm);
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) return;
      // Re-found by id — the confirm ran an event loop, so a re-list may have happened.
      QListWidgetItem* row = nullptr;
      for (int i = 0; i < list->count() && !row; ++i) {
        QListWidgetItem* c = list->item(i);
        if (c->data(Qt::UserRole).toString() == id &&
            c->data(Qt::UserRole + 1).toString().isEmpty())
          row = c;
      }
      if (row) {
        // A painted row has no widget of its own, so its RECT is what comes apart
        // (support/DisintegrateOverlay.hpp). Clipped to the viewport.
        DisintegrateOverlay::overRect(
            list->viewport(),
            list->visualItemRect(row).intersected(list->viewport()->rect()),
            this, DisintegrateOverlay::Sweep::ROWS, /*dust=*/true,
            DisintegrateOverlay::DUST_MAX_CELLS, DisintegrateOverlay::DUST_MS,
            list->palette().color(QPalette::Text));   // lifted to the row's ink
        retireRow(row);  // blank the real row at once — the snapshot is what flies
        updateBatchBar();   // …and it leaves the checked set with its own dust, not after it
      }
      emit removeRequested({{id, QString()}});  // the owner removes, then setProjects()
    });
  }

  void ProjectsDialog::moveToServerSelected() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local rows only
    if (!connections || connections->urls().isEmpty()) return;
    // Which server (browser moveToServer): the picker shell, Move as the action.
    const QString nm = support::shortName(it->data(Qt::UserRole + 3).toString());
    const QString target = pickServer(
        this, connections->urls(),
        tr("Move \"%1\" to which server? It becomes a server-backed project.").arg(nm),
        tr("Move to server"), tr("Move"), QStringLiteral("upload"));
    if (target.isEmpty()) return;
    selectedId = it->data(Qt::UserRole).toString();
    selectedServerUrl = target;
    action = Action::MOVE_TO_SERVER;
    accept();
  }

  void ProjectsDialog::copyToServerSelected() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local rows only
    if (!connections || connections->urls().isEmpty()) return;
    const QString nm = support::shortName(it->data(Qt::UserRole + 3).toString());
    const QString target =
        pickServer(this, connections->urls(), tr("Copy \"%1\" to which server?").arg(nm));
    if (target.isEmpty()) return;
    const QString id = it->data(Qt::UserRole).toString();
    const auto cur = std::find_if(projects.begin(), projects.end(),
                                  [&](const Project& p) {
                                    return QString::fromStdString(p.meta.id) == id;
                                  });
    const QString base = cur != projects.end() ? QString::fromStdString(cur->meta.name)
                                                : QStringLiteral("Untitled");
    PromptSpec spec;
    spec.title = tr("Copy to server");
    spec.message = tr("Name for the server copy:");
    spec.confirmLabel = tr("Copy");
    spec.confirmIcon = QStringLiteral("copy");
    spec.defaultValue = base + "-copy";
    const auto name = promptModal(this, spec);
    if (!name || name->isEmpty()) return;
    selectedId = id;
    selectedServerUrl = target;
    newName = *name;
    action = Action::COPY_TO_SERVER;
    accept();
  }

  void ProjectsDialog::moveToLocalSelected() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (server.isEmpty()) return;  // server (golden) rows only
    selectedId = it->data(Qt::UserRole).toString();
    selectedServerUrl = server;
    action = Action::MOVE_TO_LOCAL;
    accept();
  }

  void ProjectsDialog::makeLocalCopySelected() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (server.isEmpty()) return;  // server (golden) rows only
    const QString id = it->data(Qt::UserRole).toString();
    QString base = QStringLiteral("Untitled");
    for (const auto& sp : remote)
      if (sp.id == id && sp.serverUrl == server) { base = sp.name.isEmpty() ? base : sp.name; break; }
    PromptSpec spec;
    spec.title = tr("Copy to local");
    spec.message = tr("Name for the local copy:");
    spec.confirmLabel = tr("Copy");
    spec.confirmIcon = QStringLiteral("copy");
    spec.defaultValue = base + "-copy";
    const auto name = promptModal(this, spec);
    if (!name || name->isEmpty()) return;
    selectedId = id;
    selectedServerUrl = server;
    newName = *name;
    action = Action::MAKE_LOCAL_COPY;
    accept();
  }

}  // namespace stencil::gui
