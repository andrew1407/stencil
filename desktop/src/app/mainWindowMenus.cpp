#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"

#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

// MainWindow's menu bar: applyMenuBarPlacement() + buildMenus(). Split from
// mainWindow.cpp; same class, definitions only.

namespace stencil::gui {

  // Where the menu bar lives. Native means the platform's own bar — the macOS
  // global bar, or a Unity/GNOME appmenu; non-native keeps it inside the window.
  // Default native, because that is what each platform expects; the opt-out
  // exists because Qt's export leaves an EMPTY in-window bar on some GNOME setups.
  // Windows has no global bar, so Qt ignores the flag there.
  void MainWindow::applyMenuBarPlacement() {
    menuBar()->setNativeMenuBar(settings_.nativeMenuBar);
  }

  void MainWindow::buildMenus() {
    applyMenuBarPlacement();
    // Mnemonics avoid the Alt+letter combos bound to hotkeys (Alt+F fullscreen,
    // Alt+P points, Alt+L lines, etc.).
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
    // The instant line/rect actions belong beside Start/Stop, exactly as the browser's
    // Draw toolbar section pairs #draw-toggle with the two instant items. Both actions
    // already existed (rect) or are new (line) but were reachable only from the canvas
    // context menu, so the menu bar offered no way to draw a shape instantly. Sharing a
    // plain QAction across two menus is fine (the Image Filter rows below cannot be
    // shared — they are QWidgetActions).
    edit->addAction(actDrawLineNow_);
    edit->addAction(actDrawRectNow_);
    // Line-style radio set, mirroring the browser's Line Style toolbar select. Same
    // exclusive lineStyleGroup_ as the context menu's copy, so the two stay in sync.
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

    // Data menu: layout JSON file + clipboard, and image save/copy/paste.
    // Mirrors the browser toolbar's Image/Layout button cluster (toolbar.js).
    auto* data = menuBar()->addMenu("&Data");
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
    // Copy Image / Download Image: nested submenus of the same variant actions the
    // canvas context menu and the toolbar buttons' options popups use (context-menu
    // parity — see showContextMenu's copyImg/dlImg).
    QMenu* copyImgMenu = data->addMenu("Copy Image");
    populateExportVariantMenu(copyImgMenu, /*copy=*/true);
    QMenu* dlImgMenu = data->addMenu("Download Image");
    populateExportVariantMenu(dlImgMenu, /*copy=*/false);
    // Row polish (icon hover motion, keycap shake, hover sweep) — parity with the
    // canvas context menu's own copy/download submenus (showContextMenu) and the
    // toolbar's export-options popups (wireExportOptionsPopups). Both menus here
    // are built once and live for the app's whole life (menuRowPolish.hpp).
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
    // Image filter: the browser's View section carries an Image Filter control, but the menu
    // bar had none — it lived only on the toolbar and in the context menu. The context
    // menu's rich radio submenu is built from QWidgetActions, which host real widgets and
    // therefore cannot appear in a second menu without being moved out of the first, so the
    // menu bar gets the plain cycle action (Alt+B) instead of a duplicated submenu.
    view->addAction(actCycleFilter_);
    // Compare-with-original submenu: radio set kept in sync with the toolbar combo.
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
    units->addAction(actUnitCm_);
    units->addAction(actUnitIn_);
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
    // Per-project name colour lives here (not as a toolbar swatch): pick a custom colour or
    // revert to the theme default. Enabled only with an active project (see updateProjectTitle).
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

    // Remember WHICH ROW a menu command came out of, so a dialog opened from the menu bar
    // grows out of that row when its toolbar icon is hidden (see dialogAnchorRect_).
    // It has to be recorded on HOVER: Qt hides the menu before it emits triggered(), so by
    // the time the action fires there is no popup left to ask for the row's geometry — the
    // reveal then fell back to a box above the dialog and read as dropping in from the top.
    // Keyboard navigation emits hovered() on the selected row too, so Return is covered.
    for (QMenu* m : menuBar()->findChildren<QMenu*>()) {
      connect(m, &QMenu::hovered, this, [this, m](QAction* a) {
        const QRect r = m->actionGeometry(a);
        menuRowAction_ = a;
        menuRowRect_ = r.isValid() ? QRect(m->mapToGlobal(r.topLeft()), r.size()) : QRect();
      });
      // Drop it once the menu is gone — otherwise a row hovered earlier would still be
      // claiming to be the origin when the same command is later run from its icon or a
      // shortcut. Deferred by one cycle because triggered() lands AFTER the hide.
      connect(m, &QMenu::aboutToHide, this, [this] {
        QTimer::singleShot(0, this, [this] { menuRowAction_ = nullptr; menuRowRect_ = QRect(); });
      });
    }
  }

}  // namespace stencil::gui
