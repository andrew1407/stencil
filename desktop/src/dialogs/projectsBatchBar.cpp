#include "projectsDialog.hpp"
#include <QPushButton>

#include "projectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "projectsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/controlReveal.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/filterFade.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/modalChrome.hpp"
#include "appTooltip.hpp"


namespace stencil::gui {

  void ProjectsDialog::onItemChanged(QListWidgetItem* it) {
    if (building_ || !it || it->data(Qt::UserRole).isNull()) return;
    const QString key = QString("%1|%2").arg(it->data(Qt::UserRole + 1).toString(),
                                             it->data(Qt::UserRole).toString());
    if (it->checkState() == Qt::Checked) checked_.insert(key);
    else checked_.remove(key);
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
    if (!batchBar_) return;
    int locals = 0, remotes = 0;
    for (const QString& k : checked_) {
      if (k.startsWith('|')) ++locals; else ++remotes;
    }
    const int n = checked_.size();
    // The bar hosts Select all, so it shows whenever the filtered view HAS selectable rows.
    const auto anySelectableNow = [this] {
      for (int i = 0; i < list_->count(); ++i) {
        const QListWidgetItem* it = list_->item(i);
        if (filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
            (it->flags() & Qt::ItemIsUserCheckable))
          return true;
      }
      return false;
    };
    // Closes only once its contents have flown (support/controlReveal) — the connections dialog's twin.
    revealBar(batchBar_, [this, anySelectableNow] {
      return !checked_.isEmpty() || anySelectableNow();
    });
    // A hard show/hide on the FIRST thing in the row shoved everything after it sideways in one frame.
    if (batchCount_) {
      batchCount_->setText(tr("%1 selected").arg(n));
      revealControls(batchCount_, n > 0);
    }
    const bool haveServers = connections_ && !connections_->urls().isEmpty();
    const auto dir = batchDirectionsFor(locals, remotes, haveServers);
    // The GROUP flies; these visibility flips never carry a cloud of their own.
    if (batchToServer_) batchToServer_->setVisible(dir.toServer);
    if (batchCopyServer_) batchCopyServer_->setVisible(dir.toServer);
    if (batchToLocal_) batchToLocal_->setVisible(dir.toLocal);
    if (batchCopyLocal_) batchCopyLocal_->setVisible(dir.toLocal);
    // Browser motion.js revealControls. Laid out first: the flips above have only QUEUED the re-flow,
    // and the swap photographs the group as it stands.
    if (batchSelectedGroup_) {
      if (QLayout* gl = batchSelectedGroup_->layout()) gl->activate();
      revealControls(batchSelectedGroup_, n > 0);
    }
    updateSelectAll();
  }

  bool ProjectsDialog::allFilteredChecked() const {
    bool any = false;
    for (int i = 0; i < list_->count(); ++i) {
      const QListWidgetItem* it = list_->item(i);
      if (!filteredIn(it) || it->data(Qt::UserRole).isNull() ||
          !(it->flags() & Qt::ItemIsUserCheckable))
        continue;
      if (!checked_.contains(rowKeyAt(i))) return false;
      any = true;
    }
    return any;
  }

  // Browser updateSelectAll.
  void ProjectsDialog::updateSelectAll() {
    if (!selectAllBtn_) return;
    bool any = false;
    for (int i = 0; i < list_->count() && !any; ++i) {
      const QListWidgetItem* it = list_->item(i);
      any = filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
            (it->flags() & Qt::ItemIsUserCheckable);
    }
    revealControls(selectAllBtn_, any);
    // Browser icons.js setSelectAllFace.
    const bool all = allFilteredChecked();
    selectAllBtn_->setText(all ? tr("Deselect all") : tr("Select all"));
    selectAllBtn_->setIcon(labelIcon(all ? "x" : "check", QColor("#ffffff"), 13));
  }

  // Over the CURRENT filtered view; deselect clears the WHOLE selection.
  void ProjectsDialog::toggleSelectAll() {
    if (allFilteredChecked()) {
      checked_.clear();
      refresh();
      return;
    }
    for (int i = 0; i < list_->count(); ++i) {
      QListWidgetItem* it = list_->item(i);
      if (filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
          (it->flags() & Qt::ItemIsUserCheckable))
        it->setCheckState(Qt::Checked);
    }
    updateBatchBar();
  }

  void ProjectsDialog::runBatch(Action act) {
    batchItems_.clear();
    for (const QString& k : checked_) {
      const int bar = k.indexOf('|');
      batchItems_.append({ k.mid(bar + 1), k.left(bar) });
    }
    if (batchItems_.isEmpty()) return;
    if (act == Action::BATCH_MOVE_TO_SERVER || act == Action::BATCH_COPY_TO_SERVER) {
      if (!connections_ || connections_->urls().isEmpty()) return;
      const QString target = pickServer(
          this, connections_->urls(),
          act == Action::BATCH_MOVE_TO_SERVER ? tr("Move the selected projects to which server?")
                                           : tr("Copy the selected projects to which server?"));
      if (target.isEmpty()) return;
      selectedServerUrl_ = target;
    }
    if (act == Action::BATCH_REMOVE) {
      // Confirm HERE (like Clear All): the owner removes on removeRequested and repaints via setProjects().
      ConfirmSpec spec;
      spec.title = tr("Remove projects");
      spec.message = tr("Remove %1 selected project(s)? Server projects are deleted from "
                        "the server.")
                         .arg(batchItems_.size());
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) return;
      scatterRows(checked_);
      // A checked row the filter HIDES has no dust to leave with; batchItems_ is already captured.
      checked_.clear();
      updateBatchBar();
      emit removeRequested(batchItems_);
      return;
    }
    action_ = act;
    accept();
  }

}  // namespace stencil::gui
