#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"

#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

// MainWindow's menu bar: applyMenuBarPlacement() + buildMenus().

namespace stencil::gui {

  // Native = the platform's own bar (macOS, Unity/GNOME appmenu). The opt-out exists because Qt's export leaves
  // an EMPTY in-window bar on some GNOME setups; Windows ignores the flag.
  void MainWindow::applyMenuBarPlacement() {
    menuBar()->setNativeMenuBar(settings.nativeMenuBar);
  }

  void MainWindow::buildMenus() {
    applyMenuBarPlacement();
    // Mnemonics avoid the Alt+letter hotkeys (Alt+F fullscreen, Alt+P points, Alt+L lines, …).
    auto* file = menuBar()->addMenu("F&ile");
    file->addAction(actOpen);
    file->addAction(actCrop);
    file->addAction(actRotateLeft);
    file->addAction(actRotateRight);
    file->addAction(actSaveSession);
    file->addSeparator();
    file->addAction(actQuit);
    support::revealMenuBarMenu(*file, *menuBar());

    auto* edit = menuBar()->addMenu("&Edit");
    edit->addAction(actStartDraw);
    edit->addAction(actStopDraw);
    // Instant line/rect sit beside Start/Stop, as the browser's Draw section pairs them. A plain QAction can be shared
    // across menus; the Image Filter rows below cannot (QWidgetActions).
    edit->addAction(actDrawLineNow);
    edit->addAction(actDrawRectNow);
    // Same exclusive lineStyleGroup as the context menu's copy, so the two stay in sync.
    auto* lineStyle = edit->addMenu("Line St&yle");
    lineStyle->addAction(actStyleSolid);
    lineStyle->addAction(actStyleDashed);
    lineStyle->addAction(actStyleDotted);
    support::revealSubmenu(*lineStyle, *edit, *lineStyle->menuAction());
    edit->addSeparator();
    edit->addAction(actUndo);
    edit->addAction(actRedo);
    edit->addSeparator();
    edit->addAction(actNewLine);
    edit->addAction(actDeleteLast);
    edit->addAction(actDeleteLine);
    edit->addAction(actDeletePoint);
    edit->addAction(actClearAll);
    edit->addAction(actDeselect);
    support::revealMenuBarMenu(*edit, *menuBar());

    // Data menu (browser toolbar.js Image/Layout cluster).
    auto* data = menuBar()->addMenu("&Data");
    data->addAction(actScript);
    data->addSeparator();
    data->addAction(actDownloadJson);
    data->addAction(actUploadJson);
    data->addSeparator();
    data->addAction(actOpenProjectFile);
    data->addAction(actSaveProjectFile);
    data->addAction(actStencilLiveSync);
    data->addAction(actDeleteProjectFile);
    data->addSeparator();
    data->addAction(actCopyLayout);
    data->addAction(actPasteLayout);
    data->addSeparator();
    // Same variant actions as the context menu and the toolbar options popups.
    QMenu* copyImgMenu = data->addMenu("Copy Image");
    populateExportVariantMenu(copyImgMenu, /*copy=*/true);
    QMenu* dlImgMenu = data->addMenu("Download Image");
    populateExportVariantMenu(dlImgMenu, /*copy=*/false);
    // Row polish, parity with the context menu and export-options popups; both menus live for the app's whole life (menuRowPolish.hpp).
    for (QMenu* m : {copyImgMenu, dlImgMenu}) support::wireMenuRowPolish(m, this, /*compact=*/true);
    support::revealSubmenu(*copyImgMenu, *data, *copyImgMenu->menuAction());
    support::revealSubmenu(*dlImgMenu, *data, *dlImgMenu->menuAction());
    data->addAction(actPasteImage);
    support::revealMenuBarMenu(*data, *menuBar());

    auto* view = menuBar()->addMenu("&View");
    view->addAction(actZoomIn);
    view->addAction(actZoomOut);
    view->addAction(actFit);
    view->addSeparator();
    view->addAction(actShowPoints);
    view->addAction(actShowLines);
    // The context menu's radio submenu is QWidgetActions, which cannot appear in a second menu, so the menu bar gets the plain cycle action (Alt+B).
    view->addAction(actCycleFilter);
    auto* compareMenu = view->addMenu("&Compare");
    compareGroup = new QActionGroup(this);
    auto mkCompare = [&](const QString& text, const QString& value) {
      auto* a = compareMenu->addAction(text);
      a->setCheckable(true);
      a->setData(value);
      a->setChecked(value == "none");
      compareGroup->addAction(a);
      connect(a, &QAction::triggered, this, [this, value] { setCompareModeUi(value); });
    };
    mkCompare("None", "none");
    mkCompare("Original only", "original");
    mkCompare("Vertical split (original | edit)", "vertical");
    mkCompare("Horizontal split (original / edit)", "horizontal");
    compareMenu->addSeparator();
    compareMenu->addAction(actCycleCompare);
    support::revealSubmenu(*compareMenu, *view, *compareMenu->menuAction());
    view->addAction(actPanel);
    view->addAction(actChat);
    view->addAction(actAssistantSettings);
    view->addAction(actToolbars);
    view->addAction(actTooltip);
    view->addAction(actAllowFormulas);
    auto* units = view->addMenu("&Units");
    units->addAction(this->units.unitCm);
    units->addAction(this->units.unitIn);
    support::revealSubmenu(*units, *view, *units->menuAction());
    view->addSeparator();
    view->addAction(actTheme);
    view->addAction(actFullscreen);
    view->addSeparator();
    view->addAction(actIncognito);
    view->addAction(actSettings);
    support::revealMenuBarMenu(*view, *menuBar());

    auto* project = menuBar()->addMenu("P&roject");
    project->addAction(actProjects);
    project->addAction(actConnect);
    project->addAction(actNewProject);
    project->addAction(actSaveProject);
    project->addAction(actClearProject);
    project->addSeparator();
    project->addAction(actRenameProject);
    // Per-project name colour; enabled only with an active project (updateProjectTitle).
    actProjectColor = project->addAction("Project &color…", this, [this] { chooseProjectColor(); });
    actProjectColorClear =
        project->addAction("Use theme &default color", this, [this] { setActiveProjectColor(QString()); });
    actProjectColor->setEnabled(false);
    actProjectColorClear->setEnabled(false);
    project->addSeparator();
    project->addAction(actDescription);
    project->addAction(actKeywords);
    project->addAction(actLinks);
    project->addAction(actOpenIn);
    support::revealMenuBarMenu(*project, *menuBar());

    auto* help = menuBar()->addMenu("&Help");
    help->addAction(actInfo);
    help->addAction(actShortcuts);
    support::revealMenuBarMenu(*help, *menuBar());

    // Remember WHICH ROW a menu command came from, so its dialog grows out of that row when the toolbar icon is hidden.
    // Recorded on HOVER: Qt hides the menu before triggered(), so there is no popup left to ask then. Keyboard navigation emits hovered() too.
    for (QMenu* m : menuBar()->findChildren<QMenu*>()) {
      connect(m, &QMenu::hovered, this, [this, m](QAction* a) {
        const QRect r = m->actionGeometry(a);
        pop.menuRowAction = a;
        pop.menuRowRect = r.isValid() ? QRect(m->mapToGlobal(r.topLeft()), r.size()) : QRect();
      });
      // Drop it once the menu is gone, or a hovered row keeps claiming to be the origin; deferred because triggered() lands AFTER the hide.
      connect(m, &QMenu::aboutToHide, this, [this] {
        QTimer::singleShot(0, this, [this] { pop.menuRowAction = nullptr; pop.menuRowRect = QRect(); });
      });
    }
  }

}  // namespace stencil::gui
