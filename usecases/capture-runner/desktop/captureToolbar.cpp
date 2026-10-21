// The toolbar's lists and views over the dusk scene with two lines: the crop dialog, the filter
// list, the compare split, the export options (every variant shows once a filter and the split are
// on) and the page sizes. Browser twin: usecases/capture-runner/browser/editSteps.mjs.
#include "captureStrip.hpp"

#include "CanvasWidget.hpp"
#include "ProjectTitleController.hpp"
#include "SharedState.hpp"
#include "fileStore.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QMenu>
#include <QToolButton>

using namespace stencil::gui;

namespace {
  // A combo's list, opened as a click opens it and grabbed where it drops, the window dimmed.
  void comboList(MainWindow& win, QComboBox* combo, const QString& name) {
    QWidget* list = nullptr;
    if (combo) {
      combo->showPopup();
      waitUntil([&] { list = QApplication::activePopupWidget(); return list && list->isVisible(); }, 2000);
    }
    if (!list) { std::printf("  %s SKIPPED (the list did not open)\n", qPrintable(name)); return; }
    pumpFor(400);
    saveLayers(name, &win, {list}, false);
    combo->hidePopup();
    pumpFor(200);
  }

  // The Download button's options: its right-click menu, dropped under the button.
  void exportMenu(MainWindow& win, const QAction* download, QMenu* menu) {
    QToolButton* btn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == download && b->isVisible()) btn = b;
    if (!btn || !menu) { std::printf("  export-menu SKIPPED (no Download button)\n"); return; }
    menu->popup(btn->mapToGlobal(QPoint(0, btn->height())));
    waitUntil([menu] { return menu->isVisible(); }, 2000);
    pumpFor(400);
    saveLayers(QStringLiteral("export-menu"), &win, {menu}, false);
    menu->close();
    pumpFor(200);
  }
}  // namespace

void MainWindowGuiTest::useProject(MainWindow& win, const char* id, const char* name, const QImage& picture) {
  if (!win.findProject(id)) {
    Project pr;
    pr.meta.id = id;
    pr.meta.name = name;
    pr.meta.createdAt = pr.meta.updatedAt = QDateTime::currentMSecsSinceEpoch();
    pr.meta.expiresAt = pr.meta.createdAt + 7LL * 24 * 3'600'000;   // a week, the app's default period
    win.projectList.push_back(pr);
    SharedState::instance().saveProjects(&win);
  }
  win.activeProjectId = QString::fromLatin1(id);
  fileStore::markProjectOpened(win.activeProjectId, QDateTime::currentMSecsSinceEpoch());
  win.findChild<CanvasWidget*>()->loadFromImage(picture);
  win.docSource.blankColor.clear();
  win.applyImageFilter(QStringLiteral("none"));
  win.projectTitle->updateProjectTitle();
  win.refreshActions();
  win.updateImageSizeInfo();
}

void MainWindowGuiTest::toolbarShots(MainWindow& win, const ShotSet& shots) {
  if (!shots.hasAny({QStringLiteral("crop-dialog"), QStringLiteral("filter-menu"), QStringLiteral("compare-split"),
                     QStringLiteral("export-menu"), QStringLiteral("page-size")}))
    return;
  useProject(win, "sunset", "Sunset sketch", scene());
  win.findChild<CanvasWidget*>()->setLines({line({{140, 150}, {520, 260}, {330, 520}}, "#c81e1e", 4, "solid"),
                                            line({{610, 170}, {860, 540}}, "#1e63c8", 3, "dashed")});
  fit(win);
  pumpFor(300);
  if (shots.has("crop-dialog")) grabModal(win, actionNamed(win, QStringLiteral("Crop Image…")), "crop-dialog");
  if (shots.has("filter-menu")) comboList(win, win.tools.imageFilter, QStringLiteral("filter-menu"));

  // Sepia on, and the original beside the edit: the split the toolbar's Compare list names "Split ↔".
  win.applyImageFilter(QStringLiteral("sepia"));
  win.parts.styleControls.setCompareModeUi(QStringLiteral("vertical"));
  pumpFor(300);
  if (shots.has("compare-split")) save("compare-split", &win);
  if (shots.has("export-menu")) exportMenu(win, win.acts.saveImage, win.acts.saveImageOptionsMenu);
  win.parts.styleControls.setCompareModeUi(QStringLiteral("none"));
  win.applyImageFilter(QStringLiteral("none"));
  pumpFor(200);

  if (shots.has("page-size")) comboList(win, win.units.pageSize, QStringLiteral("page-size"));
}
