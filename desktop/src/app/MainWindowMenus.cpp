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
    menuBar()->setNativeMenuBar(settings_.nativeMenuBar);
  }

  void MainWindow::buildMenus() {
    applyMenuBarPlacement();
    // Mnemonics avoid the Alt+letter hotkeys (Alt+F fullscreen, Alt+P points, Alt+L lines, …).
    auto* file = menuBar()->addMenu("F&ile");
    file->addAction(actOpen_);
    file->addAction(actCrop_);
    file->addAction(actRotateLeft_);
    file->addAction(actRotateRight_);
    file->addAction(actSaveSession_);
    file->addSeparator();
    file->addAction(actQuit_);
    support::revealMenuBarMenu(*file, *menuBar());

    auto* edit = menuBar()->addMenu("&Edit");
    edit->addAction(actStartDraw_);
    edit->addAction(actStopDraw_);
    // Instant line/rect sit beside Start/Stop, as the browser's Draw section pairs them. A plain QAction can be shared
    // across menus; the Image Filter rows below cannot (QWidgetActions).
    edit->addAction(actDrawLineNow_);
    edit->addAction(actDrawRectNow_);
    // Same exclusive lineStyleGroup_ as the context menu's copy, so the two stay in sync.
    auto* lineStyle = edit->addMenu("Line St&yle");
    lineStyle->addAction(actStyleSolid_);
    lineStyle->addAction(actStyleDashed_);
    lineStyle->addAction(actStyleDotted_);
    support::revealSubmenu(*lineStyle, *edit, *lineStyle->menuAction());
    edit->addSeparator();
    edit->addAction(actUndo_);
    edit->addAction(actRedo_);
    edit->addSeparator();
    edit->addAction(actNewLine_);
    edit->addAction(actDeleteLast_);
    edit->addAction(actDeleteLine_);
    edit->addAction(actDeletePoint_);
    edit->addAction(actClearAll_);
    edit->addAction(actDeselect_);
    support::revealMenuBarMenu(*edit, *menuBar());

    // Data menu (browser toolbar.js Image/Layout cluster).
    auto* data = menuBar()->addMenu("&Data");
    data->addAction(actScript_);
    data->addSeparator();
    data->addAction(actDownloadJson_);
    data->addAction(actUploadJson_);
    data->addSeparator();
    data->addAction(actOpenProjectFile_);
    data->addAction(actSaveProjectFile_);
    data->addAction(actStencilLiveSync_);
    data->addAction(actDeleteProjectFile_);
    data->addSeparator();
    data->addAction(actCopyLayout_);
    data->addAction(actPasteLayout_);
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
    data->addAction(actPasteImage_);
    support::revealMenuBarMenu(*data, *menuBar());

    auto* view = menuBar()->addMenu("&View");
    view->addAction(actZoomIn_);
    view->addAction(actZoomOut_);
    view->addAction(actFit_);
    view->addSeparator();
    view->addAction(actShowPoints_);
    view->addAction(actShowLines_);
    // The context menu's radio submenu is QWidgetActions, which cannot appear in a second menu, so the menu bar gets the plain cycle action (Alt+B).
    view->addAction(actCycleFilter_);
    auto* compareMenu = view->addMenu("&Compare");
    compareGroup_ = new QActionGroup(this);
    auto mkCompare = [&](const QString& text, const QString& value) {
      auto* a = compareMenu->addAction(text);
      a->setCheckable(true);
      a->setData(value);
      a->setChecked(value == "none");
      compareGroup_->addAction(a);
      connect(a, &QAction::triggered, this, [this, value] { setCompareModeUi(value); });
    };
    mkCompare("None", "none");
    mkCompare("Original only", "original");
    mkCompare("Vertical split (original | edit)", "vertical");
    mkCompare("Horizontal split (original / edit)", "horizontal");
    compareMenu->addSeparator();
    compareMenu->addAction(actCycleCompare_);
    support::revealSubmenu(*compareMenu, *view, *compareMenu->menuAction());
    view->addAction(actPanel_);
    view->addAction(actChat_);
    view->addAction(actAssistantSettings_);
    view->addAction(actToolbars_);
    view->addAction(actTooltip_);
    view->addAction(actAllowFormulas_);
    auto* units = view->addMenu("&Units");
    units->addAction(units_.unitCm);
    units->addAction(units_.unitIn);
    support::revealSubmenu(*units, *view, *units->menuAction());
    view->addSeparator();
    view->addAction(actTheme_);
    view->addAction(actFullscreen_);
    view->addSeparator();
    view->addAction(actIncognito_);
    view->addAction(actSettings_);
    support::revealMenuBarMenu(*view, *menuBar());

    auto* project = menuBar()->addMenu("P&roject");
    project->addAction(actProjects_);
    project->addAction(actConnect_);
    project->addAction(actNewProject_);
    project->addAction(actSaveProject_);
    project->addAction(actClearProject_);
    project->addSeparator();
    project->addAction(actRenameProject_);
    // Per-project name colour; enabled only with an active project (updateProjectTitle).
    actProjectColor_ = project->addAction("Project &color…", this, [this] { chooseProjectColor(); });
    actProjectColorClear_ =
        project->addAction("Use theme &default color", this, [this] { setActiveProjectColor(QString()); });
    actProjectColor_->setEnabled(false);
    actProjectColorClear_->setEnabled(false);
    project->addSeparator();
    project->addAction(actDescription_);
    project->addAction(actKeywords_);
    project->addAction(actLinks_);
    project->addAction(actOpenIn_);
    support::revealMenuBarMenu(*project, *menuBar());

    auto* help = menuBar()->addMenu("&Help");
    help->addAction(actInfo_);
    help->addAction(actShortcuts_);
    support::revealMenuBarMenu(*help, *menuBar());

    // Remember WHICH ROW a menu command came from, so its dialog grows out of that row when the toolbar icon is hidden.
    // Recorded on HOVER: Qt hides the menu before triggered(), so there is no popup left to ask then. Keyboard navigation emits hovered() too.
    for (QMenu* m : menuBar()->findChildren<QMenu*>()) {
      connect(m, &QMenu::hovered, this, [this, m](QAction* a) {
        const QRect r = m->actionGeometry(a);
        pop_.menuRowAction = a;
        pop_.menuRowRect = r.isValid() ? QRect(m->mapToGlobal(r.topLeft()), r.size()) : QRect();
      });
      // Drop it once the menu is gone, or a hovered row keeps claiming to be the origin; deferred because triggered() lands AFTER the hide.
      connect(m, &QMenu::aboutToHide, this, [this] {
        QTimer::singleShot(0, this, [this] { pop_.menuRowAction = nullptr; pop_.menuRowRect = QRect(); });
      });
    }
  }

}  // namespace stencil::gui
