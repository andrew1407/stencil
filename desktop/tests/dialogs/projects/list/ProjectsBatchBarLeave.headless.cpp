// The Projects list's leaving rows: removed server rows leave at the owner's rebuild, the rows below
// a removed one slide up once, and Clear All Local scatters the local rows alone.
#include "ProjectsBatchBarParts.hpp"
#include "projectsRowChrome.hpp"   // DOOMED_ROLE

namespace {
  // Answers the in-dialog confirm with OK from inside its own loop.
  void confirmSoon() {
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
  }
}  // namespace

// Removed server rows leave the open list at its next rebuild, not on its next reopen (user report);
// a relist (a delete the server refused) brings back whatever the server still lists.
int removedServerRowsLeaveAtOnce(const std::vector<Project>& locals, ConnectionManager& mgr) {
  std::printf("removed server rows leave the open list:\n");
  ProjectsDialog dlg(locals, 5000, &mgr);
  dlg.show();
  pumpFor(50);
  auto* list = dlg.findChild<QListWidget*>("projectsList");
  if (!list) return 1;
  pumpUntil([&] { return rowById(list, "r1", true) && rowById(list, "r2", true); });
  QListWidgetItem* r1 = rowById(list, "r1", true);
  check(r1 && rowById(list, "r2", true), "both server rows listed");
  if (!r1) return 1;
  const QString server = r1->data(Qt::UserRole + 1).toString();
  dlg.dropServerRows({{QStringLiteral("r1"), server}});
  check(rowById(list, "r1", true) != nullptr, "dropping rebuilds nothing by itself (the leave is still playing)");
  dlg.setProjects(locals);   // the owner's rebuild after the dust
  check(!rowById(list, "r1", true), "the removed server row is gone at the rebuild");
  check(rowById(list, "r2", true) && rowById(list, "l1", false), "the others stay");
  dlg.relistServers();
  pumpUntil([&] { return rowById(list, "r1", true) != nullptr; });
  check(rowById(list, "r1", true) != nullptr, "a relist brings back what the server still lists");
  dlg.reject();
  return 0;
}

// A removed middle row: the rows below slide up ONCE — never back down, and nothing more at the owner's
// rebuild after the dust (user report: they slid, jumped below, then slid again).
int removedRowSlidesOnce() {
  std::printf("the rows below a removed row move once, upward:\n");
  std::vector<Project> four;
  for (int i = 0; i < 4; ++i) four.push_back(makeLocal(QString("s%1").arg(i), QString("slide-%1").arg(i), 4000 - i));
  ProjectsDialog dlg(four, 5000);
  dlg.resize(760, 520);
  dlg.show();
  pumpFor(80);
  auto* list = dlg.findChild<QListWidget*>("projectsList");
  if (!list || list->count() != 4) { check(false, "four rows built"); return 1; }
  const QString victim = list->item(1)->data(Qt::UserRole).toString();
  const QString below = list->item(2)->data(Qt::UserRole).toString();
  const auto topOf = [list](const QString& id) {
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == id) return list->visualItemRect(list->item(i)).top();
    return -1;
  };
  // The owner's own sequence: drop it from the server listing now, rebuild once the dust lands.
  QObject::connect(&dlg, &ProjectsDialog::removeRequested, [&](const QVector<QPair<QString, QString>>& items) {
    dlg.dropServerRows(items);
    std::vector<Project> rest;
    for (const Project& p : four) if (QString::fromStdString(p.meta.id) != victim) rest.push_back(p);
    QTimer::singleShot(DisintegrateOverlay::DUST_MS, &dlg, [&dlg, rest] { dlg.setProjects(rest); });
  });
  list->item(1)->setCheckState(Qt::Checked);
  pumpFor(20);
  confirmSoon();
  int last = topOf(below), downs = 0;
  QVector<int> seen{last};
  QTimer sample;
  QObject::connect(&sample, &QTimer::timeout, [&] {
    const int now = topOf(below);
    if (now == last) return;
    if (now > last) ++downs;
    seen << now;
    last = now;
  });
  sample.start(5);
  if (QPushButton* rm = btnByText(&dlg, QStringLiteral("Remove selected"))) rm->click();
  pumpFor(500);
  const int settled = topOf(below);
  pumpFor(DisintegrateOverlay::DUST_MS + 300);   // past the owner's rebuild
  sample.stop();
  QStringList path;
  for (int y : seen) path << QString::number(y);
  check(downs == 0, qPrintable("the row below never moves down (" + path.join(" ") + ")"));
  check(topOf(below) == settled, "the owner's rebuild moves nothing that already settled");
  check(list->count() == 3, "the removed row is gone");
  return 0;
}

// Clear All Local takes the local rows and leaves a server row where it is: no scatter, no re-arrival
// (user report: every row scattered and the server ones came back with an opening flight).
int clearAllLocalKeepsServerRows(const std::vector<Project>& locals, ConnectionManager& mgr) {
  std::printf("Clear All Local leaves the server rows alone:\n");
  ProjectsDialog dlg(locals, 5000, &mgr);
  dlg.show();
  pumpFor(50);
  auto* list = dlg.findChild<QListWidget*>("projectsList");
  if (!list) return 1;
  pumpUntil([&] { return rowById(list, "r1", true) != nullptr; });
  if (!rowById(list, "r1", true)) { check(false, "server rows listed"); return 1; }
  bool cleared = false;
  QObject::connect(&dlg, &ProjectsDialog::clearAllRequested, [&] { cleared = true; });
  confirmSoon();
  if (QPushButton* clear = btnByText(&dlg, QStringLiteral("Clear All Local"))) clear->click();
  check(cleared, "Clear All Local asked the owner to clear");
  check(rowById(list, "l1", false) && rowById(list, "l1", false)->data(stencil::gui::DOOMED_ROLE).toBool(),
        "a local row is leaving");
  QListWidgetItem* r1 = rowById(list, "r1", true);
  check(r1 && !r1->data(stencil::gui::DOOMED_ROLE).toBool() && !r1->isHidden(), "the server row stays as it was");
  dlg.reject();
  return 0;
}
