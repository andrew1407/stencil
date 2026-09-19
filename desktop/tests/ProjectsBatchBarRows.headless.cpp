// The composition rule behind the batch bar, and the pinned temporary row: that it stands at the
// top unselectable, renames itself for an incognito editor, and — on a list already on screen —
// ARRIVES out of the filter's own sand rather than a removal's scatter.
#include "ProjectsBatchBarParts.hpp"

int batchDirectionMatrix() {
  // ── Pure composition rule (no widgets needed) ──
  std::printf("batch direction matrix:\n");
  check(batchDirectionsFor(2, 0, true).toServer && !batchDirectionsFor(2, 0, true).toLocal,
        "local-only + server: to-server only");
  check(!batchDirectionsFor(2, 0, false).toServer, "local-only, no server: to-server hidden");
  check(batchDirectionsFor(0, 2, false).toLocal && !batchDirectionsFor(0, 2, false).toServer,
        "server-only: to-local only (needs no connection gate)");
  check(!batchDirectionsFor(1, 1, true).toServer && !batchDirectionsFor(1, 1, true).toLocal,
        "mixed: both directions hidden");
  check(!batchDirectionsFor(0, 0, true).toServer && !batchDirectionsFor(0, 0, true).toLocal,
        "empty selection: both hidden");
  return 0;
}

int pinnedRows(const std::vector<Project>& locals) {
  {
    std::printf("the pinned temporary row:\n");
    ProjectsDialog dlg(locals, 5000);
    dlg.setTemporary(true);
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    check(list && list->count() == int(locals.size()) + 1, "one row more than the projects");
    QListWidgetItem* top = list ? list->item(0) : nullptr;
    check(top && top->text() == QStringLiteral("Temporary (unsaved)"), "…pinned at the top, named as the browser's");
    check(top && top->data(Qt::UserRole).isNull() && top->data(Qt::UserRole + 11).toBool(),
          "no project id (every action ignores it), marked as the temporary row");
    check(top && !(top->flags() & Qt::ItemIsSelectable) && !(top->flags() & Qt::ItemIsUserCheckable),
          "not selectable, no checkbox");
    check(top && top->data(Qt::UserRole + 10).toString() == QStringLiteral("Current window · not saved to storage"),
          "carries the browser's note");
    check(top && !top->icon().isNull(), "wears the pencil tile");
    dlg.setTemporary(true, /*incognito=*/true);
    pumpFor(20);
    top = list ? list->item(0) : nullptr;
    check(top && top->text() == QStringLiteral("Incognito (unsaved)"), "an incognito editor names itself so");
    dlg.setTemporary(false);
    pumpFor(20);
    check(list && list->count() == int(locals.size()) && !list->item(0)->data(Qt::UserRole + 11).toBool(),
          "a project open here: no pinned row");
    // With nothing saved yet the pinned row alone stands — never beside "No projects yet".
    std::vector<Project> none;
    ProjectsDialog empty(none, 5000);
    empty.setTemporary(true);
    empty.show();
    pumpFor(30);
    auto* elist = empty.findChild<QListWidget*>("projectsList");
    check(elist && elist->count() == 1 && elist->item(0)->data(Qt::UserRole + 11).toBool(),
          "an empty store shows the temporary row alone");
    empty.reject();
    dlg.reject();
  }

  {
    // …and when it turns up on a list ALREADY on screen, the row ARRIVES: it forms out of the filter's own
    // sand, like any row a re-answered list brings in (user report). Browser twin: playFilterEnter.
    std::printf("the pinned row arrives out of sand, and lands whole:\n");
    ProjectsDialog dlg(locals, 5000);
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    check(list && list->count() == int(locals.size()), "opens on the saved rows alone");
    if (!list) return 1;
    // A removal's own handover: new list + session in ONE repaint, arrival a beat later.
    dlg.setProjects(locals, /*temporary=*/true, /*incognito=*/false);
    QListWidgetItem* top = list->item(0);
    check(top && top->data(Qt::UserRole + 11).toBool(), "the pinned row is in the list at once");
    if (!top) return 1;
    pumpUntil([&] { return dlg.findChild<QWidget*>(QString::fromLatin1(FILTER_DUST_OBJECT_NAME)) != nullptr; });
    check(top->data(FILTER_DUST_ROLE).toDouble() == 0.0,
          "…veiled while its motes gather — the sand IS the row arriving");
    check(dlg.findChild<QWidget*>(QString::fromLatin1(FILTER_DUST_OBJECT_NAME)) != nullptr,
          "…out of the FILTER's light sand");
    check(dlg.findChild<QWidget*>(DisintegrateOverlay::OBJECT_NAME) == nullptr,
          "…never the removal's destructive scatter");
    const QVariant settled = list->item(1)->data(FILTER_DUST_ROLE);
    check(!settled.isValid() || settled.toDouble() >= 1.0,
          "…while a row that was already listed does not replay its own arrival");
    // …and those motes are made of the ROW: a delegate-painted row draws nothing while it is veiled, so
    // the picture must be taken before the veil or the cloud is a flat slab of list background.
    auto* cloud = static_cast<DisintegrateOverlay*>(
        dlg.findChild<QWidget*>(QString::fromLatin1(FILTER_DUST_OBJECT_NAME)));
    check(cloud != nullptr, "the arrival's cloud is on the dialog");
    if (cloud) {
      const QImage shot = cloud->snapshot().toImage();
      int varied = 0;
      const QRgb corner = shot.isNull() ? 0 : shot.pixel(1, 1);
      for (int y = 0; y < shot.height(); ++y)
        for (int x = 0; x < shot.width(); ++x)
          if (shot.pixel(x, y) != corner) ++varied;
      check(varied > 200, "…and it is made of the row's picture, not the bare background");
    }
    pumpUntil([&] { return top->data(FILTER_DUST_ROLE).toDouble() >= 1.0; });
    check(top->data(FILTER_DUST_ROLE).toDouble() >= 1.0 && !top->isHidden(),
          "…and the row is left whole once the motes have landed");

    // A repaint that adds nothing (a rename, a poll that changed no row) plays nothing.
    pumpUntil([&] { return dlg.findChild<QWidget*>(
                        QString::fromLatin1(FILTER_DUST_OBJECT_NAME)) == nullptr; });
    dlg.setProjects(locals, /*temporary=*/true, /*incognito=*/false);
    check(dlg.findChild<QWidget*>(QString::fromLatin1(FILTER_DUST_OBJECT_NAME)) == nullptr,
          "an unchanged repaint animates nothing");
    dlg.reject();
  }
  return 0;
}
