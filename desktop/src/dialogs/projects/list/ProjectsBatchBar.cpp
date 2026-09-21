#include "ProjectsDialog.hpp"
#include <QPushButton>

#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ProjectsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../../support/control/reveal/controlReveal.hpp"
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "../../../support/theme/filterFade.hpp"
#include "../../../support/guiHelpers.hpp"
#include "../../../support/modal/modalChrome.hpp"
#include "AppTooltip.hpp"


namespace stencil::gui {

  void ProjectsDialog::onItemChanged(QListWidgetItem* it) {
    if (building || !it || it->data(Qt::UserRole).isNull()) return;
    const QString key = QString("%1|%2").arg(it->data(Qt::UserRole + 1).toString(),
                                             it->data(Qt::UserRole).toString());
    if (it->checkState() == Qt::Checked) batch.checked.insert(key);
    else batch.checked.remove(key);
    updateBatchBar();
  }

  BatchDirections batchDirectionsFor(int locals, int remotes, bool haveServers) {
    BatchDirections d;
    d.toServer = locals > 0 && remotes == 0 && haveServers;
    d.toLocal = remotes > 0 && locals == 0;
    return d;
  }

  // Inapplicable directions are HIDDEN, not greyed (browser projectsModal.js updateBatchBar).
  void ProjectsDialog::updateBatchBar() {
    if (!batch.batchBar) return;
    int locals = 0, remotes = 0;
    for (const QString& k : batch.checked) {
      if (k.startsWith('|')) ++locals; else ++remotes;
    }
    const int n = batch.checked.size();
    // The bar hosts Select all, so it shows whenever the filtered view HAS selectable rows.
    const auto anySelectableNow = [this] {
      for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem* it = list->item(i);
        if (filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
            (it->flags() & Qt::ItemIsUserCheckable))
          return true;
      }
      return false;
    };
    // Closes only once its contents have flown (support/controlReveal) — the connections dialog's twin.
    revealBar(batch.batchBar, [this, anySelectableNow] {
      return !batch.checked.isEmpty() || anySelectableNow();
    });
    // A hard show/hide on the FIRST thing in the row shoved everything after it sideways in one frame.
    if (batch.batchCount) {
      batch.batchCount->setText(tr("%1 selected").arg(n));
      revealControls(batch.batchCount, n > 0);
    }
    const bool haveServers = connections && !connections->urls().isEmpty();
    const auto dir = batchDirectionsFor(locals, remotes, haveServers);
    // The GROUP flies; these visibility flips never carry a cloud of their own.
    if (batch.batchToServer) batch.batchToServer->setVisible(dir.toServer);
    if (batch.batchCopyServer) batch.batchCopyServer->setVisible(dir.toServer);
    if (batch.batchToLocal) batch.batchToLocal->setVisible(dir.toLocal);
    if (batch.batchCopyLocal) batch.batchCopyLocal->setVisible(dir.toLocal);
    // Browser motion.js revealControls. Laid out first: the flips above have only QUEUED the re-flow,
    // and the swap photographs the group as it stands.
    if (batch.batchSelectedGroup) {
      if (QLayout* gl = batch.batchSelectedGroup->layout()) gl->activate();
      revealControls(batch.batchSelectedGroup, n > 0);
    }
    updateSelectAll();
  }

  bool ProjectsDialog::allFilteredChecked() const {
    bool any = false;
    for (int i = 0; i < list->count(); ++i) {
      const QListWidgetItem* it = list->item(i);
      if (!filteredIn(it) || it->data(Qt::UserRole).isNull() ||
          !(it->flags() & Qt::ItemIsUserCheckable))
        continue;
      if (!batch.checked.contains(rowKeyAt(i))) return false;
      any = true;
    }
    return any;
  }

  // Browser updateSelectAll.
  void ProjectsDialog::updateSelectAll() {
    if (!selectAllBtn) return;
    bool any = false;
    for (int i = 0; i < list->count() && !any; ++i) {
      const QListWidgetItem* it = list->item(i);
      any = filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
            (it->flags() & Qt::ItemIsUserCheckable);
    }
    revealControls(selectAllBtn, any);
    // Browser icons.js setSelectAllFace.
    const bool all = allFilteredChecked();
    selectAllBtn->setText(all ? tr("Deselect all") : tr("Select all"));
    selectAllBtn->setIcon(labelIcon(all ? "x" : "check", QColor("#ffffff"), 13));
  }

  // Over the CURRENT filtered view; deselect clears the WHOLE selection.
  void ProjectsDialog::toggleSelectAll() {
    if (allFilteredChecked()) {
      batch.checked.clear();
      refresh();
      return;
    }
    for (int i = 0; i < list->count(); ++i) {
      QListWidgetItem* it = list->item(i);
      if (filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
          (it->flags() & Qt::ItemIsUserCheckable))
        it->setCheckState(Qt::Checked);
    }
    updateBatchBar();
  }

  void ProjectsDialog::runBatch(Action act) {
    batch.batchItems.clear();
    for (const QString& k : batch.checked) {
      const int bar = k.indexOf('|');
      batch.batchItems.append({ k.mid(bar + 1), k.left(bar) });
    }
    if (batch.batchItems.isEmpty()) return;
    if (act == Action::BATCH_MOVE_TO_SERVER || act == Action::BATCH_COPY_TO_SERVER) {
      if (!connections || connections->urls().isEmpty()) return;
      const QString target = pickServer(
          this, connections->urls(),
          act == Action::BATCH_MOVE_TO_SERVER ? tr("Move the selected projects to which server?")
                                           : tr("Copy the selected projects to which server?"));
      if (target.isEmpty()) return;
      selectedServerUrl = target;
    }
    if (act == Action::BATCH_REMOVE) {
      // Confirm HERE (like Clear All): the owner removes on removeRequested and repaints via setProjects().
      ConfirmSpec spec;
      spec.title = tr("Remove projects");
      spec.message = tr("Remove %1 selected project(s)? Server projects are deleted from "
                        "the server.")
                         .arg(batch.batchItems.size());
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) return;
      scatterRows(batch.checked);
      // A checked row the filter HIDES has no dust to leave with; batch.batchItems is already captured.
      batch.checked.clear();
      updateBatchBar();
      emit removeRequested(batch.batchItems);
      return;
    }
    action = act;
    accept();
  }

}  // namespace stencil::gui
