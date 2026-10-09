#include "MainWindow.hpp"
#include "MenuBuilder.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"

#include <QActionGroup>
#include <QMenuBar>

// MainWindow's menu bar: its placement and its menus.

namespace stencil::gui {

  void MenuBuilder::buildMenus() {
    // Native = the platform's own bar (macOS, Unity/GNOME appmenu). The opt-out exists because Qt's export
    // leaves an EMPTY in-window bar on some GNOME setups; Windows ignores the flag.
    w.menuBar()->setNativeMenuBar(w.settings.nativeMenuBar);
    // Mnemonics avoid the Alt+letter hotkeys (Alt+F fullscreen, Alt+P points, Alt+L lines, …).
    auto* file = w.menuBar()->addMenu("F&ile");
    file->addAction(w.acts.open);
    file->addAction(w.acts.crop);
    file->addAction(w.acts.flipImage);
    file->addAction(w.acts.rotateLeft);
    file->addAction(w.acts.rotateRight);
    file->addAction(w.acts.saveSession);
    file->addSeparator();
    file->addAction(w.acts.quit);
    support::revealMenuBarMenu(*file, *w.menuBar());

    auto* edit = w.menuBar()->addMenu("&Edit");
    edit->addAction(w.acts.startDraw);
    edit->addAction(w.acts.stopDraw);
    // Instant line/rect sit beside Start/Stop, as the browser's Draw section pairs them. A plain QAction can be shared
    // across menus; the Image Filter rows below cannot (QWidgetActions).
    edit->addAction(w.ctxMenu.drawLineNow);
    edit->addAction(w.ctxMenu.drawRectNow);
    // Same exclusive lineStyleGroup as the context menu's copy, so the two stay in sync.
    auto* lineStyle = edit->addMenu("Line St&yle");
    lineStyle->addAction(w.ctxMenu.styleSolid);
    lineStyle->addAction(w.ctxMenu.styleDashed);
    lineStyle->addAction(w.ctxMenu.styleDotted);
    support::revealSubmenu(*lineStyle, *edit, *lineStyle->menuAction());
    edit->addSeparator();
    edit->addAction(w.acts.undo);
    edit->addAction(w.acts.redo);
    edit->addSeparator();
    edit->addAction(w.acts.newLine);
    edit->addAction(w.acts.deleteLast);
    edit->addAction(w.acts.deleteLine);
    edit->addAction(w.acts.deletePoint);
    edit->addAction(w.acts.clearAll);
    edit->addAction(w.acts.deselect);
    support::revealMenuBarMenu(*edit, *w.menuBar());

    // Data menu (browser toolbar.js Image/Layout cluster).
    auto* data = w.menuBar()->addMenu("&Data");
    data->addAction(w.acts.script);
    data->addSeparator();
    data->addAction(w.acts.downloadJson);
    data->addAction(w.acts.uploadJson);
    data->addSeparator();
    data->addAction(w.acts.openProjectFile);
    data->addAction(w.acts.saveProjectFile);
    data->addAction(w.acts.stencilLiveSync);
    data->addAction(w.acts.deleteProjectFile);
    data->addSeparator();
    data->addAction(w.acts.copyLayout);
    data->addAction(w.acts.pasteLayout);
    data->addSeparator();
    // Same variant actions as the context menu and the toolbar options popups.
    QMenu* copyImgMenu = data->addMenu("Copy Image");
    w.parts.exportMenus.populateExportVariantMenu(copyImgMenu, /*copy=*/true);
    QMenu* dlImgMenu = data->addMenu("Download Image");
    w.parts.exportMenus.populateExportVariantMenu(dlImgMenu, /*copy=*/false);
    // Row polish, parity with the context menu and export-options popups; both menus live for the app's whole life (menuRowPolish.hpp).
    for (QMenu* m : {copyImgMenu, dlImgMenu}) support::wireMenuRowPolish(m, &w, /*compact=*/true);
    support::revealSubmenu(*copyImgMenu, *data, *copyImgMenu->menuAction());
    support::revealSubmenu(*dlImgMenu, *data, *dlImgMenu->menuAction());
    data->addAction(w.acts.pasteImage);
    support::revealMenuBarMenu(*data, *w.menuBar());

    auto* view = w.menuBar()->addMenu("&View");
    view->addAction(w.acts.zoomIn);
    view->addAction(w.acts.zoomOut);
    view->addAction(w.acts.fit);
    view->addSeparator();
    view->addAction(w.acts.showPoints);
    view->addAction(w.acts.showLines);
    // The context menu's radio submenu is QWidgetActions, which cannot appear in a second menu, so the menu bar gets the plain cycle actions (Alt+B, Alt+Shift+B).
    view->addAction(w.acts.cycleFilter);
    view->addAction(w.acts.cycleFilterPrev);
    auto* compareMenu = view->addMenu("&Compare");
    w.ctxMenu.compareGroup = new QActionGroup(&w);
    auto mkCompare = [&](const QString& text, const QString& value) {
      auto* a = compareMenu->addAction(text);
      a->setCheckable(true);
      a->setData(value);
      a->setChecked(value == "none");
      w.ctxMenu.compareGroup->addAction(a);
      QObject::connect(a, &QAction::triggered, &w, [this, value] { w.parts.styleControls.setCompareModeUi(value); });
    };
    mkCompare("None", "none");
    mkCompare("Original only", "original");
    mkCompare("Vertical split (original | edit)", "vertical");
    mkCompare("Horizontal split (original / edit)", "horizontal");
    compareMenu->addSeparator();
    compareMenu->addAction(w.acts.cycleCompare);
    support::revealSubmenu(*compareMenu, *view, *compareMenu->menuAction());
    view->addAction(w.acts.panel);
    view->addAction(w.acts.chat);
    view->addAction(w.acts.assistantSettings);
    view->addAction(w.acts.toolbars);
    view->addAction(w.acts.tooltip);
    view->addAction(w.acts.allowFormulas);
    auto* units = view->addMenu("&Units");
    units->addAction(w.units.unitCm);
    units->addAction(w.units.unitIn);
    support::revealSubmenu(*units, *view, *units->menuAction());
    view->addSeparator();
    view->addAction(w.acts.theme);
    view->addAction(w.acts.fullscreen);
    view->addSeparator();
    view->addAction(w.acts.incognito);
    view->addAction(w.acts.settings);
    support::revealMenuBarMenu(*view, *w.menuBar());

    auto* project = w.menuBar()->addMenu("P&roject");
    project->addAction(w.acts.projects);
    project->addAction(w.acts.connect);
    project->addAction(w.acts.newProject);
    project->addAction(w.acts.saveProject);
    project->addAction(w.acts.clearProject);
    project->addAction(w.acts.closeProject);
    project->addSeparator();
    project->addAction(w.acts.renameProject);
    // Per-project name colour; enabled only with an active project (updateProjectTitle).
    w.acts.projectColor = project->addAction("Project &color…", &w, [this] { w.parts.projects.chooseProjectColor(); });
    w.acts.projectColorClear =
        project->addAction("Use theme &default color", &w, [this] { w.parts.projects.setActiveProjectColor(QString()); });
    w.acts.projectColor->setEnabled(false);
    w.acts.projectColorClear->setEnabled(false);
    project->addSeparator();
    project->addAction(w.acts.description);
    project->addAction(w.acts.keywords);
    project->addAction(w.acts.links);
    project->addAction(w.acts.openIn);
    support::revealMenuBarMenu(*project, *w.menuBar());

    auto* help = w.menuBar()->addMenu("&Help");
    help->addAction(w.acts.info);
    help->addAction(w.acts.shortcuts);
    support::revealMenuBarMenu(*help, *w.menuBar());

    // Remember WHICH ROW a menu command came from, so its dialog grows out of that row when the toolbar icon is hidden.
    // Recorded on HOVER: Qt hides the menu before triggered(), so there is no popup left to ask then. Keyboard navigation emits hovered() too.
    for (QMenu* m : w.menuBar()->findChildren<QMenu*>()) {
      QObject::connect(m, &QMenu::hovered, &w, [this, m](QAction* a) {
        const QRect r = m->actionGeometry(a);
        w.pop.menuRowAction = a;
        w.pop.menuRowRect = r.isValid() ? QRect(m->mapToGlobal(r.topLeft()), r.size()) : QRect();
      });
      // Drop it once the menu is gone, or a hovered row keeps claiming to be the origin; deferred because triggered() lands AFTER the hide.
      QObject::connect(m, &QMenu::aboutToHide, &w, [this] {
        QTimer::singleShot(0, &w, [this] { w.pop.menuRowAction = nullptr; w.pop.menuRowRect = QRect(); });
      });
    }
  }

}  // namespace stencil::gui
