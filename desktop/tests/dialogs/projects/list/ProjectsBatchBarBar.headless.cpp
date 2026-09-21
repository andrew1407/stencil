// The batch bar itself: Move/Copy shown only for a homogeneous selection, the bar wrapping rather
// than clipping when squeezed, and Select all sweeping the CURRENT filtered view alone.
#include "ProjectsBatchBarParts.hpp"

int batchBarVisibility(const std::vector<Project>& locals, ConnectionManager& mgr) {
  {
    std::printf("batch bar visibility by selection composition:\n");
    ProjectsDialog dlg(locals, 5000, &mgr);
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    check(list != nullptr, "finds the projects list");
    if (!list) return 1;
    pumpUntil([&] { return rowById(list, "r1", true) != nullptr; });
    check(rowById(list, "r1", true) != nullptr, "server rows arrived from the mock listing");
    if (!rowById(list, "r1", true)) { dlg.reject(); return 1; }

    QPushButton* toServer = btnByText(&dlg, QStringLiteral("Move to server"));
    QPushButton* copyServer = btnByText(&dlg, QStringLiteral("Copy to server"));
    QPushButton* toLocal = btnByText(&dlg, QStringLiteral("Move to local"));
    QPushButton* copyLocal = btnByText(&dlg, QStringLiteral("Copy to local"));
    QPushButton* removeBtn = btnByText(&dlg, QStringLiteral("Remove selected"));
    check(toServer && copyServer && toLocal && copyLocal && removeBtn,
          "finds all five batch buttons");
    if (!(toServer && copyServer && toLocal && copyLocal && removeBtn)) return 1;
    check(!toServer->isVisible() && !toLocal->isVisible(),
          "no selection: batch bar (and its buttons) hidden");

    // Local-only selection: to-server pair shown, to-local pair hidden entirely.
    rowById(list, "l1", false)->setCheckState(Qt::Checked);
    rowById(list, "l2", false)->setCheckState(Qt::Checked);
    check(toServer->isVisible() && copyServer->isVisible(),
          "local-only: To server / Server copy visible");
    check(!toLocal->isVisible() && !copyLocal->isVisible(),
          "local-only: To local / Local copy hidden (not greyed)");
    check(removeBtn->isVisible(), "Remove stays available for any selection");

    // Squeezed narrow, the bar WRAPS (browser .projects-batch-bar / -selected flex-wrap):
    // "Remove selected" drops to a line of its own instead of being cut off at the edge.
    {
      pumpFor(stencil::gui::CONTROL_REVEAL_IN_MS + 100);   // the group's slot has slid fully open
      dlg.setMinimumSize(0, 0);
      dlg.resize(420, dlg.height());
      pumpFor(80);
      const auto inDlg = [&dlg](QWidget* w) { return QRect(w->mapTo(&dlg, QPoint(0, 0)), w->size()); };
      bool inside = true;
      for (QPushButton* b : {toServer, copyServer, removeBtn})
        inside = inside && dlg.rect().contains(inDlg(b));
      check(inside, "narrow: every batch button stays inside the dialog");
      check(inDlg(removeBtn).top() > inDlg(toServer).top(),
            "narrow: Remove selected wraps onto a line under the first");
      dlg.resize(560, dlg.height());
      pumpFor(80);
    }

    // Mixed selection: every direction hidden, the bar itself stays up.
    rowById(list, "r1", true)->setCheckState(Qt::Checked);
    // The bar's buttons come and go as the app's control swap (updateBatchBar → support/controlReveal),
    // and a hide only lands once its collapse has played, so the settled state is what gets asserted.
    pumpUntil([&] {
      return !toServer->isVisible() && !copyServer->isVisible() && !toLocal->isVisible() &&
             !copyLocal->isVisible();
    });
    check(!toServer->isVisible() && !copyServer->isVisible() && !toLocal->isVisible() &&
              !copyLocal->isVisible(),
          "mixed: all four direction buttons hidden");
    check(removeBtn->isVisible(), "mixed: Remove still visible");

    // Server-only selection: the to-local pair appears, to-server pair hides.
    rowById(list, "l1", false)->setCheckState(Qt::Unchecked);
    rowById(list, "l2", false)->setCheckState(Qt::Unchecked);
    check(toLocal->isVisible() && copyLocal->isVisible(),
          "server-only: To local / Local copy visible");
    pumpUntil([&] { return !toServer->isVisible() && !copyServer->isVisible(); });
    check(!toServer->isVisible() && !copyServer->isVisible(),
          "server-only: To server / Server copy hidden");

    // ── Select all / deselect all over the filtered view ──
    std::printf("select-all over the filtered view:\n");
    auto* selectAll = dlg.findChild<QPushButton*>("projectsSelectAll");
    auto* search = dlg.findChild<QLineEdit*>();
    check(selectAll && search, "finds the select-all button and the search box");
    if (!selectAll || !search) return 1;
    rowById(list, "r1", true)->setCheckState(Qt::Unchecked);  // start clean
    check(selectAll->isVisible(), "select-all visible while the list has selectable rows");
    check(selectAll->text() == QStringLiteral("Select all"), "label starts as Select all");

    selectAll->click();
    int checkedRows = 0;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->checkState() == Qt::Checked) ++checkedRows;
    check(checkedRows == 5, "select-all checks every row of the unfiltered view");
    check(selectAll->text() == QStringLiteral("Deselect all"),
          "label flips to Deselect all once everything visible is checked");

    selectAll->click();  // deselect clears the whole selection
    checkedRows = 0;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->checkState() == Qt::Checked) ++checkedRows;
    check(checkedRows == 0, "deselect-all clears the selection");
    check(selectAll->text() == QStringLiteral("Select all"), "label flips back to Select all");

    // A filtered select-all must not sweep up rows the user cannot see.
    search->setText("alp");  // matches only the local "alpha"
    pumpFor(20);
    check(selectAll->isVisible(), "select-all still shown for a non-empty filtered view");
    selectAll->click();
    check(rowById(list, "l1", false)->checkState() == Qt::Checked, "filtered row got checked");
    check(rowById(list, "l2", false)->checkState() == Qt::Unchecked &&
              rowById(list, "r1", true)->checkState() == Qt::Unchecked,
          "rows outside the filter stay unchecked");
    check(selectAll->text() == QStringLiteral("Deselect all"),
          "the filtered view counts as fully selected");
    search->clear();
    pumpFor(20);
    check(selectAll->text() == QStringLiteral("Select all"),
          "clearing the filter grows the pool — label back to Select all");
    check(rowById(list, "l1", false)->checkState() == Qt::Checked,
          "the filtered selection survives clearing the filter");

    search->setText("zzz-no-match");  // empty filtered view → nothing to select
    pumpFor(20);
    pumpUntil([&] { return !selectAll->isVisible(); });   // …once its swap has played
    check(!selectAll->isVisible(), "select-all hides when the filtered view is empty");
    dlg.reject();
  }
  return 0;
}
