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

// The check-selection batch bar: what it offers and what it runs.

namespace stencil::gui {

  // A row's checkbox toggled → update the checked set + the batch toolbar.
  void ProjectsDialog::onItemChanged(QListWidgetItem* it) {
    if (building_ || !it || it->data(Qt::UserRole).isNull()) return;
    const QString key = QString("%1|%2").arg(it->data(Qt::UserRole + 1).toString(),
                                             it->data(Qt::UserRole).toString());
    if (it->checkState() == Qt::Checked) checked_.insert(key);
    else checked_.remove(key);
    updateBatchBar();
  }

  // The pure homogeneity rule behind the batch bar (headless-testable without a server).
  BatchDirections batchDirectionsFor(int locals, int remotes, bool haveServers) {
    BatchDirections d;
    d.toServer = locals > 0 && remotes == 0 && haveServers;
    d.toLocal = remotes > 0 && locals == 0;
    return d;
  }

  // Show/hide the batch toolbar. Inapplicable directions are HIDDEN, not greyed: a
  // local-only selection never moves "to local", so a disabled button is just noise
  // (browser parity: updateBatchBar in projectsModal.js).
  void ProjectsDialog::updateBatchBar() {
    if (!batchBar_) return;
    int locals = 0, remotes = 0;
    for (const QString& k : checked_) {
      if (k.startsWith('|')) ++locals; else ++remotes;
    }
    const int n = checked_.size();
    // The bar hosts Select all too, so it shows whenever the filtered view HAS
    // selectable rows — the selection-only controls inside it come and go with the
    // checked set (browser parity: projectsModal.js updateBatchBar).
    const auto anySelectableNow = [this] {
      for (int i = 0; i < list_->count(); ++i) {
        const QListWidgetItem* it = list_->item(i);
        if (filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
            (it->flags() & Qt::ItemIsUserCheckable))
          return true;
      }
      return false;
    };
    // Opens at once, closes only once its contents have flown (support/controlReveal) —
    // taking the strip away outright took Select all's own out-flight off the screen
    // before a frame of it showed (the connections dialog's twin).
    revealBar(batchBar_, [this, anySelectableNow] {
      return !checked_.isEmpty() || anySelectableNow();
    });
    // The count rides the same swap as the buttons: a hard show/hide on the FIRST thing in
    // the row shoved everything after it sideways in one frame, which is most of what read
    // as "jumping". Text first, so it is right before the slot opens.
    if (batchCount_) {
      batchCount_->setText(tr("%1 selected").arg(n));
      revealControls(batchCount_, n > 0);
    }
    const bool haveServers = connections_ && !connections_->urls().isEmpty();
    const auto dir = batchDirectionsFor(locals, remotes, haveServers);
    // Which directions apply is a plain visibility flip INSIDE the group — it is the group
    // that flies, so these never carry a cloud of their own.
    if (batchToServer_) batchToServer_->setVisible(dir.toServer);
    if (batchCopyServer_) batchCopyServer_->setVisible(dir.toServer);
    if (batchToLocal_) batchToLocal_->setVisible(dir.toLocal);
    if (batchCopyLocal_) batchCopyLocal_->setVisible(dir.toLocal);
    // …and the GROUP comes and goes as the app's control swap (support/controlReveal,
    // browser motion.js revealControls). A no-op when the state is already right, so an
    // unrelated refresh() plays nothing. Laid out first: the swap photographs the group
    // as it stands, and the flips above have only QUEUED its re-flow — the picture would
    // still hold the hidden buttons' gaps and the old wrap.
    if (batchSelectedGroup_) {
      if (QLayout* gl = batchSelectedGroup_->layout()) gl->activate();
      revealControls(batchSelectedGroup_, n > 0);
    }
    updateSelectAll();
  }

  // True when the filtered view has selectable rows and every one of them is checked.
  bool ProjectsDialog::allFilteredChecked() const {
    bool any = false;
    for (int i = 0; i < list_->count(); ++i) {
      const QListWidgetItem* it = list_->item(i);
      // filteredIn, not isHidden: a row still fading OUT has already left the pool.
      if (!filteredIn(it) || it->data(Qt::UserRole).isNull() ||
          !(it->flags() & Qt::ItemIsUserCheckable))
        continue;   // placeholders, filtered-out and doomed rows are not selectable
      if (!checked_.contains(rowKeyAt(i))) return false;
      any = true;
    }
    return any;
  }

  // Shown whenever the filtered view has selectable rows; the label flips to
  // "Deselect all" once everything visible is checked (browser: updateSelectAll).
  void ProjectsDialog::updateSelectAll() {
    if (!selectAllBtn_) return;
    bool any = false;
    for (int i = 0; i < list_->count() && !any; ++i) {
      const QListWidgetItem* it = list_->item(i);
      any = filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
            (it->flags() & Qt::ItemIsUserCheckable);
    }
    revealControls(selectAllBtn_, any);
    // Label AND glyph say which way it goes: a check gathers, a cross lets go
    // (browser icons.js setSelectAllFace).
    const bool all = allFilteredChecked();
    selectAllBtn_->setText(all ? tr("Deselect all") : tr("Select all"));
    selectAllBtn_->setIcon(labelIcon(all ? "x" : "check", QColor("#ffffff"), 13));
  }

  // Select-all toggles over the CURRENT filtered view, so a filtered "select all" never
  // sweeps up projects the user cannot see; deselect clears the WHOLE selection.
  void ProjectsDialog::toggleSelectAll() {
    if (allFilteredChecked()) {
      checked_.clear();
      refresh();   // re-sync every row's checkbox (also off-filter ones)
      return;      // refresh() already ran updateBatchBar
    }
    for (int i = 0; i < list_->count(); ++i) {
      QListWidgetItem* it = list_->item(i);
      if (filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
          (it->flags() & Qt::ItemIsUserCheckable))
        it->setCheckState(Qt::Checked);   // onItemChanged maintains checked_
    }
    updateBatchBar();
  }

  // Resolve the checked rows into (id, serverUrl) pairs, pick a target server for the
  // to-server actions, then accept() so the main window applies the batch.
  void ProjectsDialog::runBatch(Action act) {
    batchItems_.clear();
    for (const QString& k : checked_) {
      const int bar = k.indexOf('|');
      batchItems_.append({ k.mid(bar + 1), k.left(bar) });  // (id, serverUrl)
    }
    if (batchItems_.isEmpty()) return;
    if (act == Action::BatchMoveToServer || act == Action::BatchCopyToServer) {
      if (!connections_ || connections_->urls().isEmpty()) return;
      const QString target = pickServer(
          this, connections_->urls(),
          act == Action::BatchMoveToServer ? tr("Move the selected projects to which server?")
                                           : tr("Copy the selected projects to which server?"));
      if (target.isEmpty()) return;
      selectedServerUrl_ = target;
    }
    if (act == Action::BatchRemove) {
      // Confirm HERE (like Clear All): the question sits over the still-open list, the
      // owner removes on removeRequested and repaints via setProjects() — no close.
      ConfirmSpec spec;
      spec.title = tr("Remove projects");
      spec.message = tr("Remove %1 selected project(s)? Server projects are deleted from "
                        "the server.")
                         .arg(batchItems_.size());
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) return;
      scatterRows(checked_);   // they come apart on the way out — bar and rows together
      // A checked row the current filter HIDES has no dust to leave with, so scatterRows
      // never saw it; batchItems_ is already captured, and the whole selection is going.
      checked_.clear();
      updateBatchBar();
      emit removeRequested(batchItems_);
      return;
    }
    action_ = act;
    accept();
  }

}  // namespace stencil::gui
