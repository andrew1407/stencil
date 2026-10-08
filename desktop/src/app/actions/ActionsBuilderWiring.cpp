// MainWindow's action wiring. Part of the buildActions() phase chain.
#include "MainWindow.hpp"
#include "ActionsBuilder.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "QuarterTurnOverlay.hpp"
#include "../../support/modal/modalChrome.hpp"   // confirmModal — the browser-styled question
#include "tipContent.hpp"

#include <QScrollArea>

namespace stencil::gui {

  void ActionsBuilder::wireActionHandlers() {
    // Incognito is togglable only before an image is loaded (browser behaviour).
    w.acts.incognito->setCheckable(true);
    // setActionTip(), not setToolTip(): a plain set drops the ⌥I newAction() put on it.
    w.setActionTip(w.acts.incognito, "Incognito — edit without saving");
    // The greyed-out reason as the amber reason line (browser: data-disabled-reason).
    setTipReason(w.acts.incognito, "Choose incognito before adding an image");

    // A toggle: the button shows the accent "active" fill while fullscreen is on.
    w.acts.fullscreen->setCheckable(true);
    w.acts.showPoints->setCheckable(true);
    w.acts.showLines->setCheckable(true);
    w.acts.panel->setCheckable(true);
    w.acts.panel->setChecked(true);
    w.acts.toolbars->setCheckable(true);
    w.acts.toolbars->setChecked(true);

    QObject::connect(w.acts.open, &QAction::triggered, &w, [this] { w.parts.sourceOpener.openImage(); });
    QObject::connect(w.acts.openAnother, &QAction::triggered, &w, [this] { w.parts.sourceOpener.openImage(); });
    QObject::connect(w.acts.crop, &QAction::triggered, &w, &MainWindow::openCropDialog);
    auto rotate = [this](bool clockwise) {
      if (!w.canvas->hasImage()) {
        w.notify->error("Open an image first");
        return;
      }
      QWidget* vp = w.scroll->viewport();
      const QRect from = QuarterTurnOverlay::boxIn(w.canvas, vp);
      // The centred point turns with the picture (core::rotateLinePointsQuarter), so the zoom survives it.
      EditorView::ViewAnchor a = w.parts.view.viewAnchor();
      const double ow = w.canvas->imageWidth(), oh = w.canvas->imageHeight();
      const double ax = a.x;
      a.x = clockwise ? oh - a.y : a.y;
      a.y = clockwise ? ax : ow - ax;
      w.canvas->rotateImage(clockwise);
      w.parts.view.restoreAnchor(a);
      w.refreshActions();
      QuarterTurnOverlay::play(w.canvas, vp, from, clockwise ? 1 : -1);
    };
    // Alt+R fires before keyPressEvent: with a line selected it arms the line-rotate chord instead of rotating the image.
    QObject::connect(w.acts.rotateLeft, &QAction::triggered, &w, [this, rotate] {
      if (w.canvas && w.canvas->selectionCount() >= 1) { w.held.r = true; return; }
      rotate(false);
    });
    QObject::connect(w.acts.rotateRight, &QAction::triggered, &w, [rotate] { rotate(true); });
    QObject::connect(w.acts.flipImage, &QAction::triggered, &w, [this] {
      if (!w.canvas->hasImage()) {
        w.notify->error("Open an image first");
        return;
      }
      QWidget* vp = w.scroll->viewport();
      const QRect from = QuarterTurnOverlay::boxIn(w.canvas, vp);
      EditorView::ViewAnchor a = w.parts.view.viewAnchor();
      a.x = w.canvas->imageWidth() - a.x;
      w.canvas->flipImage();
      w.parts.view.restoreAnchor(a);
      w.refreshActions();
      QuarterTurnOverlay::play(w.canvas, vp, from, 0);
    });
    // One step along none → bw → sepia → invert → contour → custom, wrapping (dir -1 = back; browser
    // hotkeyActions.js stepFilter). A tint is chosen ahead too.
    const auto stepFilter = [this](int dir) {
      static const QStringList order{"none",   "bw",      "sepia",
                                     "invert", "contour", "custom"};
      const int cur = order.indexOf(w.settings.imageFilter);
      w.applyImageFilter(order[(cur + dir + order.size()) % order.size()]);
    };
    QObject::connect(w.acts.cycleFilter, &QAction::triggered, &w, [stepFilter] { stepFilter(1); });
    QObject::connect(w.acts.cycleFilterPrev, &QAction::triggered, &w, [stepFilter] { stepFilter(-1); });
    QObject::connect(w.acts.cycleCompare, &QAction::triggered, &w, [this] {
      if (!w.canvas->hasImage()) return;
      static const QStringList order{"none", "original", "vertical", "horizontal"};
      const int cur = order.indexOf(w.canvas->getCompareMode());
      w.parts.styleControls.setCompareModeUi(order[(cur + 1) % order.size()]);
    });
    QObject::connect(w.acts.startDraw, &QAction::triggered, w.canvas,
                     &CanvasWidget::startDrawingMode);
    QObject::connect(w.acts.stopDraw, &QAction::triggered, w.canvas,
                     &CanvasWidget::stopDrawingMode);
    QObject::connect(w.acts.newLine, &QAction::triggered, w.canvas, &CanvasWidget::startNewLine);
    QObject::connect(w.acts.undo, &QAction::triggered, w.canvas, &CanvasWidget::undo);
    QObject::connect(w.acts.redo, &QAction::triggered, w.canvas, &CanvasWidget::redo);
    // Bare Backspace: delete the SELECTED line(s) when not mid-stroke, else the last point. On a MacBook "delete" IS Backspace.
    QObject::connect(w.acts.deleteLast, &QAction::triggered, &w, [this] {
      if (!w.canvas->getIsDrawing() && !w.canvas->selectedIndices().empty())
        w.canvas->deleteSelectedLine();
      else
        w.canvas->deleteLastPoint();
    });
    // Alt+Delete: a focused POINT narrows it to that point.
    QObject::connect(w.acts.deleteLine, &QAction::triggered, &w, [this] {
      if (w.canvas->getSelectedPoint() >= 0)
        w.canvas->deletePoint(w.canvas->getSelectedPoint());
      else
        w.canvas->deleteSelectedLine();
    });
    QObject::connect(w.acts.deletePoint, &QAction::triggered, &w,
                     [this] { w.canvas->deletePoint(w.canvas->getSelectedPoint()); });
    // Browser parity (drawingApp.js clearAllLines): a wipe asks first, in the same confirm the trash uses.
    QObject::connect(w.acts.clearAll, &QAction::triggered, &w, [this] {
      ConfirmSpec spec;
      spec.title = MainWindow::tr("Clear all lines");
      spec.message = MainWindow::tr("Wipe ALL lines from the canvas? This cannot be undone except via Undo.");
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(&w, spec)) {
        if (w.notify) w.notify->info(MainWindow::tr("Clear canceled"));   // declined = a notice, not a failure
        return;
      }
      w.canvas->clearAll();
      if (w.notify) w.notify->success(MainWindow::tr("All lines cleared"));
    });
    QObject::connect(w.acts.deselect, &QAction::triggered, w.canvas, &CanvasWidget::deselect);
    QObject::connect(w.acts.zoomIn, &QAction::triggered, &w, [this] { w.parts.view.zoomIn(); });
    QObject::connect(w.acts.zoomOut, &QAction::triggered, &w, [this] { w.parts.view.zoomOut(); });
    QObject::connect(w.acts.fit, &QAction::triggered, &w, &MainWindow::fitToWindow);
    QObject::connect(w.acts.showPoints, &QAction::toggled, &w, [this](bool on) {
      w.canvas->setShowPoints(on);
      w.settings.showPoints = on;
      fileStore::saveSettings(w.settings);
    });
    QObject::connect(w.acts.showLines, &QAction::toggled, &w, [this](bool on) {
      w.canvas->setShowLines(on);
      w.settings.showLines = on;
      fileStore::saveSettings(w.settings);
    });
    QObject::connect(w.acts.theme, &QAction::triggered, &w, [this] { w.parts.theme.toggleTheme(); });
    // Panel + toolbars show/hide, animated; overlays, View menu and hotkeys all route through these actions.
    QObject::connect(w.acts.panel, &QAction::toggled, &w, [this](bool on) { w.setPanelShown(on, true); });
    QObject::connect(w.acts.toolbars, &QAction::toggled, &w, [this](bool on) { w.parts.view.setToolbarsShown(on, true); });
    QObject::connect(w.acts.fullscreen, &QAction::triggered, &w, [this] { w.parts.view.toggleFullscreen(); });
    QObject::connect(w.acts.settings, &QAction::triggered, &w, [this] { w.parts.dialogs.openSettings(); });
    QObject::connect(w.acts.projects, &QAction::triggered, &w, [this] { w.parts.projects.openProjects(); });
    QObject::connect(w.acts.connect, &QAction::triggered, &w, [this] { w.parts.projects.openConnections(); });
    QObject::connect(w.acts.links, &QAction::triggered, &w, [this] { w.parts.sourceOpener.openLinks(); });
    QObject::connect(w.acts.description, &QAction::triggered, &w, [this] { w.parts.projects.openDescription(); });
    QObject::connect(w.acts.keywords, &QAction::triggered, &w, [this] { w.parts.projects.openKeywords(); });
    // Shared hotkeysConfig openAssistantSettings.
    w.acts.assistantSettings = newAction("AI Assistant Settings…", w.keys.value("openAssistantSettings", "Alt+Shift+G"));
    w.setActionTip(w.acts.assistantSettings, "AI assistant settings — provider, model & voice");
    QObject::connect(w.acts.assistantSettings, &QAction::triggered, &w, [this] { w.parts.dialogs.openAssistantSettings(); });
    QObject::connect(w.acts.openIn, &QAction::triggered, &w, [this] { w.parts.projects.openInAnotherApp(); });
    QObject::connect(w.acts.copyProject, &QAction::triggered, &w, [this] { w.parts.projectCopy.showToolbarMenu(); });
    QObject::connect(w.acts.newProject, &QAction::triggered, &w,
                     [this] { w.parts.projects.newProjectFromCanvas(); });
    QObject::connect(w.acts.saveProject, &QAction::triggered, &w,
                     &MainWindow::saveToActiveProject);
    QObject::connect(w.acts.clearProject, &QAction::triggered, &w,
                     [this] { w.parts.projects.clearCurrentProject(); });
    QObject::connect(w.acts.closeProject, &QAction::triggered, &w,
                     [this] { w.parts.projects.closeActiveProject(); });
    QObject::connect(w.acts.saveSession, &QAction::triggered, &w, [this] {
      w.saveSessionNow();
    });
    w.acts.shortcuts = newAction("Keyboard Shortcuts…", w.keys.value("openHotkeys", "Alt+K"));
    w.setActionTip(w.acts.shortcuts, "Keyboard shortcuts");   // the browser #settings-btn title
    QObject::connect(w.acts.shortcuts, &QAction::triggered, &w,
                     [this] { w.parts.dialogs.openShortcuts(); });
    // Shared hotkeysConfig contextMenu; no menu-bar entry, the menu IS the entry.
    w.acts.contextMenu = newAction("Canvas Context Menu", w.keys.value("contextMenu", "Shift+F10"));
    w.setActionTip(w.acts.contextMenu, "Open the canvas context menu at the pointer (or the canvas centre)");
    QObject::connect(w.acts.contextMenu, &QAction::triggered, &w, [this] { w.parts.canvasMenu.showContextMenuFromKeyboard(); });

  }

}  // namespace stencil::gui
