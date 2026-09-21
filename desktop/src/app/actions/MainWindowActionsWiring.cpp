// MainWindow's action wiring. Part of the buildActions() phase chain.
#include "MainWindow.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "IncognitoOverlay.hpp"
#include "Notifications.hpp"
#include "../../support/modal/modalChrome.hpp"   // confirmModal — the browser-styled question
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QSignalBlocker>
#include <QTimer>

namespace stencil::gui {

  void MainWindow::wireActionHandlers() {
    // Incognito is togglable only before an image is loaded (browser behaviour).
    actIncognito->setCheckable(true);
    // setActionTip(), not setToolTip(): a plain set drops the ⌥I newAction() put on it.
    setActionTip(actIncognito, "Incognito — edit without saving");
    // The greyed-out reason as the amber reason line (browser: data-disabled-reason).
    setTipReason(actIncognito, "Choose incognito before adding an image");

    // A toggle: the button shows the accent "active" fill while fullscreen is on.
    actFullscreen->setCheckable(true);
    actShowPoints->setCheckable(true);
    actShowLines->setCheckable(true);
    actPanel->setCheckable(true);
    actPanel->setChecked(true);
    actToolbars->setCheckable(true);
    actToolbars->setChecked(true);

    connect(actOpen, &QAction::triggered, this, &MainWindow::openImage);
    connect(actOpenAnother, &QAction::triggered, this, &MainWindow::openImage);
    connect(actCrop, &QAction::triggered, this, &MainWindow::openCropDialog);
    auto rotate = [this](bool clockwise) {
      if (!canvas->hasImage()) {
        notify->error("Open an image first");
        return;
      }
      canvas->rotateImage(clockwise);
      fitToWindow();
      refreshActions();
    };
    // Alt+R fires before keyPressEvent: with a line selected it arms the line-rotate chord instead of rotating the image.
    connect(actRotateLeft, &QAction::triggered, this, [this, rotate] {
      if (canvas && canvas->selectionCount() >= 1) { rKeyHeld = true; return; }
      rotate(false);
    });
    connect(actRotateRight, &QAction::triggered, this, [rotate] { rotate(true); });
    // Cycle the image filter (Alt+B; browser cycleFilter): none → bw → sepia → invert → contour → custom.
    connect(actCycleFilter, &QAction::triggered, this, [this] {
      if (!canvas->hasImage()) return;
      static const QStringList order{"none",   "bw",      "sepia",
                                     "invert", "contour", "custom"};
      const int cur = order.indexOf(settings.imageFilter);
      applyImageFilter(order[(cur + 1) % order.size()]);
    });
    connect(actCycleCompare, &QAction::triggered, this, [this] {
      if (!canvas->hasImage()) return;
      static const QStringList order{"none", "original", "vertical", "horizontal"};
      const int cur = order.indexOf(canvas->getCompareMode());
      setCompareModeUi(order[(cur + 1) % order.size()]);
    });
    connect(actStartDraw, &QAction::triggered, canvas,
            &CanvasWidget::startDrawingMode);
    connect(actStopDraw, &QAction::triggered, canvas,
            &CanvasWidget::stopDrawingMode);
    connect(actNewLine, &QAction::triggered, canvas, &CanvasWidget::startNewLine);
    connect(actUndo, &QAction::triggered, canvas, &CanvasWidget::undo);
    connect(actRedo, &QAction::triggered, canvas, &CanvasWidget::redo);
    // Bare Backspace: delete the SELECTED line(s) when not mid-stroke, else the last point. On a MacBook "delete" IS Backspace.
    connect(actDeleteLast, &QAction::triggered, this, [this] {
      if (!canvas->getIsDrawing() && !canvas->selectedIndices().empty())
        canvas->deleteSelectedLine();
      else
        canvas->deleteLastPoint();
    });
    // Alt+Delete: a focused POINT narrows it to that point.
    connect(actDeleteLine, &QAction::triggered, this, [this] {
      if (canvas->getSelectedPoint() >= 0)
        canvas->deletePoint(canvas->getSelectedPoint());
      else
        canvas->deleteSelectedLine();
    });
    connect(actDeletePoint, &QAction::triggered, this,
            [this] { canvas->deletePoint(canvas->getSelectedPoint()); });
    // Browser parity (drawingApp.js clearAllLines): a wipe asks first, in the same confirm the trash uses.
    connect(actClearAll, &QAction::triggered, this, [this] {
      ConfirmSpec spec;
      spec.title = tr("Clear all lines");
      spec.message = tr("Wipe ALL lines from the canvas? This cannot be undone except via Undo.");
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) {
        if (notify) notify->info(tr("Clear canceled"));   // declined = a notice, not a failure
        return;
      }
      canvas->clearAll();
    });
    connect(actDeselect, &QAction::triggered, canvas, &CanvasWidget::deselect);
    connect(actZoomIn, &QAction::triggered, this, &MainWindow::zoomIn);
    connect(actZoomOut, &QAction::triggered, this, &MainWindow::zoomOut);
    connect(actFit, &QAction::triggered, this, &MainWindow::fitToWindow);
    connect(actShowPoints, &QAction::toggled, this, [this](bool on) {
      canvas->setShowPoints(on);
      settings.showPoints = on;
      fileStore::saveSettings(settings);
    });
    connect(actShowLines, &QAction::toggled, this, [this](bool on) {
      canvas->setShowLines(on);
      settings.showLines = on;
      fileStore::saveSettings(settings);
    });
    connect(actTheme, &QAction::triggered, this, &MainWindow::toggleTheme);
    // Panel + toolbars show/hide, animated; overlays, View menu and hotkeys all route through these actions.
    connect(actPanel, &QAction::toggled, this, [this](bool on) { setPanelShown(on, true); });
    connect(actToolbars, &QAction::toggled, this, [this](bool on) { setToolbarsShown(on, true); });
    connect(actFullscreen, &QAction::triggered, this,
            &MainWindow::toggleFullscreen);
    fs.hoverTimer = new QTimer(this);   // drives the fullscreen edge-hover reveal
    connect(fs.hoverTimer, &QTimer::timeout, this, &MainWindow::fsHoverTick);
    connect(actSettings, &QAction::triggered, this, &MainWindow::openSettings);
    connect(actProjects, &QAction::triggered, this, &MainWindow::openProjects);
    connect(actConnect, &QAction::triggered, this, &MainWindow::openConnections);
    connect(actLinks, &QAction::triggered, this, &MainWindow::openLinks);
    connect(actDescription, &QAction::triggered, this, &MainWindow::openDescription);
    connect(actKeywords, &QAction::triggered, this, &MainWindow::openKeywords);
    // Shared hotkeysConfig openAssistantSettings.
    actAssistantSettings = newAction("AI Assistant Settings…", hotkey("openAssistantSettings", "Alt+Shift+G"));
    setActionTip(actAssistantSettings, "AI assistant settings — provider, model & voice");
    connect(actAssistantSettings, &QAction::triggered, this, &MainWindow::openAssistantSettings);
    connect(actOpenIn, &QAction::triggered, this, &MainWindow::openInAnotherApp);
    connect(actNewProject, &QAction::triggered, this,
            &MainWindow::newProjectFromCanvas);
    connect(actSaveProject, &QAction::triggered, this,
            &MainWindow::saveToActiveProject);
    connect(actClearProject, &QAction::triggered, this,
            &MainWindow::clearCurrentProject);
    connect(actSaveSession, &QAction::triggered, this, [this] {
      saveSessionNow();
    });
    actShortcuts = newAction("Keyboard Shortcuts…", hotkey("openHotkeys", "Alt+K"));
    setActionTip(actShortcuts, "Keyboard shortcuts");   // the browser #settings-btn title
    connect(actShortcuts, &QAction::triggered, this,
            &MainWindow::openShortcuts);
    // Shared hotkeysConfig contextMenu; no menu-bar entry, the menu IS the entry.
    actContextMenu = newAction("Canvas Context Menu", hotkey("contextMenu", "Shift+F10"));
    setActionTip(actContextMenu, "Open the canvas context menu at the pointer (or the canvas centre)");
    connect(actContextMenu, &QAction::triggered, this, &MainWindow::showContextMenuFromKeyboard);

  }

}  // namespace stencil::gui
