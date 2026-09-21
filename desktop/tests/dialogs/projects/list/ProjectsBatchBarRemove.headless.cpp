// Batch remove: the confirm answered in-dialog, one dust cloud per VISIBLE checked row and none for
// the scrolled-out ones, every checked id retired in place. Then the checkbox strip, which selects.
#include "ProjectsBatchBarParts.hpp"

int batchRemoveAndCheckboxPress(const std::vector<Project>& locals) {
  // ── Batch remove: every checked row resolves; visible rows scatter inside the list ──
  {
    std::printf("batch remove scatter preconditions:\n");
    std::vector<Project> many;
    for (int i = 0; i < 14; ++i)
      many.push_back(makeLocal(QString("m%1").arg(i), QString("proj-%1").arg(i), 1000 + i));
    ProjectsDialog dlg(many, 5000);   // no server needed here
    dlg.resize(760, 320);             // small: a good half of the rows scroll out of view
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    check(list && list->count() == 14, "all 14 rows built");
    if (!list) return 1;

    // Check every row (also the scrolled-out ones), then count what is actually visible.
    for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Checked);
    const QRect view = list->viewport()->rect();
    int expectedOverlays = 0;
    for (int i = 0; i < list->count(); ++i) {
      const QRect r = list->visualItemRect(list->item(i)).intersected(view);
      if (r.width() >= 8 && r.height() >= 8) ++expectedOverlays;
    }
    check(expectedOverlays > 0, "some checked rows are on screen");
    check(expectedOverlays < list->count(), "some checked rows are scrolled out of view");
    const QRect viewInDlg(list->viewport()->mapTo(&dlg, QPoint(0, 0)), view.size());

    QPushButton* removeBtn = btnByText(&dlg, QStringLiteral("Remove selected"));
    check(removeBtn && removeBtn->isVisible(), "batch Remove available with 14 checked");
    if (!removeBtn) return 1;
    // The confirm shows INSIDE the still-open dialog (the chrome-styled confirmModal); answer Confirm from
    // a 0-timer, which fires within the modal's own event loop, and collect the emitted items.
    QVector<QPair<QString, QString>> removed;
    QObject::connect(&dlg, &ProjectsDialog::removeRequested,
                     [&removed](const QVector<QPair<QString, QString>>& items) {
                       removed = items;
                     });
    QTimer::singleShot(0, [] {
      for (int i = 0; i < 200; ++i) {
        QWidget* m = QApplication::activeModalWidget();
        if (m && m->objectName() == QLatin1String("stencilConfirmModal")) {
          for (QPushButton* b : m->findChildren<QPushButton*>())
            if (b->text() == QLatin1String("OK")) { b->click(); return; }
        }
        pumpFor(5);
      }
    });
    removeBtn->click();  // confirm → scatterRows + removeRequested; the dialog stays open
    check(dlg.isVisible(), "the dialog stays open through a batch remove");

    // The animation precondition: one overlay per VISIBLE checked row, each clipped
    // inside the list viewport — never dropped onto the dialog chrome.
    const auto overlays =
        dlg.findChildren<QWidget*>(QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME));
    check(overlays.size() == expectedOverlays,
          "one DisintegrateOverlay per visible checked row (scrolled-out rows spawn none)");
    bool inside = !overlays.isEmpty();
    for (QWidget* fx : overlays)
      if (!viewInDlg.contains(fx->geometry())) inside = false;
    check(inside, "every overlay sits inside the list's visible region");

    // Every checked id was resolved to its row: the whole set is retired in place
    // (blanked + unselectable while the dust falls — the dialog no longer closes).
    int liveRows = 0;
    for (int i = 0; i < list->count(); ++i)
      if (!list->item(i)->data(Qt::UserRole).isNull() &&
          list->item(i)->flags() != Qt::NoItemFlags)
        ++liveRows;
    check(liveRows == 0, "every checked row was found and retired on remove");
    check(dlg.getAction() == ProjectsDialog::Action::NONE && removed.size() == 14,
          "the remove was signalled (not accept()ed) with all 14 checked ids");
    dlg.reject();
  }

  // ── A press on the checkbox strip SELECTS — it must never open the row ──
  {
    std::printf("checkbox press selects, never opens:\n");
    ProjectsDialog dlg(locals, 5000);
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    check(list != nullptr, "finds the projects list");
    if (!list) return 1;
    QListWidgetItem* it = rowById(list, "l1", false);
    check(it != nullptr, "finds the local row");
    if (!it) { dlg.reject(); return 1; }
    const QRect vr = list->visualItemRect(it);
    const QPointF pos(vr.left() + 12, vr.center().y());
    const QPointF gpos(list->viewport()->mapToGlobal(pos.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, pos, gpos, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, gpos, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &release);
    // Wait out the single-click open timer: pre-fix this raised the open
    // confirm (and closed the dialog); now the dialog must simply stay.
    pumpFor(QApplication::doubleClickInterval() + 120);
    check(dlg.isVisible(), "checkbox-strip click never opens the row (dialog stays up)");
    dlg.reject();
  }
  return 0;
}
