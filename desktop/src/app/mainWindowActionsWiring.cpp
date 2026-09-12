// MainWindow's action wiring: the checkable flags, then every triggered/toggled connect.
// Part of the buildActions() phase chain (mainWindowActions.cpp).
#include "mainWindow.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "incognitoOverlay.hpp"
#include "notifications.hpp"
#include "../support/modalChrome.hpp"   // confirmModal — the browser-styled question
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QSignalBlocker>
#include <QTimer>

namespace stencil::gui {

  void MainWindow::wireActionHandlers() {
    // Incognito: edit without saving. Togglable only before an image is
    // loaded (browser behavior), so it gets disabled once content exists.
    actIncognito_->setCheckable(true);
    // Through setActionTip(), not setToolTip(): a plain set drops the ⌥I newAction() put on
    // it, and the icon-only toolbar is the only place that shortcut is written down.
    setActionTip(actIncognito_, "Incognito — edit without saving");
    // …and WHY it is greyed out, as the amber reason line (browser: data-disabled-reason) —
    // inside the title it read as part of what the button does.
    setTipReason(actIncognito_, "Choose incognito before adding an image");

    // Fullscreen is a toggle: its toolbar button shows the accent "active" fill
    // (QToolButton:checked) while fullscreen is on, mirroring the browser.
    actFullscreen_->setCheckable(true);
    actShowPoints_->setCheckable(true);
    actShowLines_->setCheckable(true);
    actPanel_->setCheckable(true);
    actPanel_->setChecked(true);
    actToolbars_->setCheckable(true);
    actToolbars_->setChecked(true);

    connect(actOpen_, &QAction::triggered, this, &MainWindow::openImage);
    connect(actOpenAnother_, &QAction::triggered, this, &MainWindow::openImage);
    connect(actCrop_, &QAction::triggered, this, &MainWindow::openCropDialog);
    auto rotate = [this](bool clockwise) {
      if (!canvas_->hasImage()) {
        notify_->error("Open an image first");
        return;
      }
      canvas_->rotateImage(clockwise);
      fitToWindow();
      refreshActions();
    };
    // Alt+R is a global shortcut, so it fires (and consumes the key) before keyPressEvent —
    // with a line selected we arm the line-rotate chord here instead of rotating the image, so
    // the following ←/→ rotates the selection (keyPressEvent). Deselect to rotate the image.
    connect(actRotateLeft_, &QAction::triggered, this, [this, rotate] {
      if (canvas_ && canvas_->selectionCount() >= 1) { rKeyHeld_ = true; return; }
      rotate(false);
    });
    connect(actRotateRight_, &QAction::triggered, this, [rotate] { rotate(true); });
    // Cycle the image filter (Alt+B) — mirrors the browser's cycleFilter hotkey:
    // none → bw → sepia → invert → contour → custom(tint). applyImageFilter
    // marks it dirty + syncs.
    connect(actCycleFilter_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) return;
      static const QStringList order{"none",   "bw",      "sepia",
                                     "invert", "contour", "custom"};
      const int cur = order.indexOf(settings_.imageFilter);
      applyImageFilter(order[(cur + 1) % order.size()]);
    });
    // Cycle the compare view (Alt+O): none → original → vertical → horizontal.
    connect(actCycleCompare_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) return;
      static const QStringList order{"none", "original", "vertical", "horizontal"};
      const int cur = order.indexOf(canvas_->compareMode());
      setCompareModeUi(order[(cur + 1) % order.size()]);
    });
    connect(actStartDraw_, &QAction::triggered, canvas_,
            &CanvasWidget::startDrawingMode);
    connect(actStopDraw_, &QAction::triggered, canvas_,
            &CanvasWidget::stopDrawingMode);
    connect(actNewLine_, &QAction::triggered, canvas_, &CanvasWidget::startNewLine);
    connect(actUndo_, &QAction::triggered, canvas_, &CanvasWidget::undo);
    connect(actRedo_, &QAction::triggered, canvas_, &CanvasWidget::redo);
    // Bare Backspace routes by selection: delete the SELECTED line(s) when not
    // mid-stroke, else "delete last point". On a MacBook "delete" IS Backspace,
    // so selected-line deletion must ride this key (browser controlsBinder twin).
    connect(actDeleteLast_, &QAction::triggered, this, [this] {
      if (!canvas_->isDrawing() && !canvas_->selectedIndices().empty())
        canvas_->deleteSelectedLine();
      else
        canvas_->deleteLastPoint();
    });
    // Alt+Delete routes by selection: a focused POINT narrows it to that point (the
    // line survives) — same shared-chord pattern as the bare Backspace above.
    connect(actDeleteLine_, &QAction::triggered, this, [this] {
      if (canvas_->selectedPoint() >= 0)
        canvas_->deletePoint(canvas_->selectedPoint());
      else
        canvas_->deleteSelectedLine();
    });
    connect(actDeletePoint_, &QAction::triggered, this,
            [this] { canvas_->deletePoint(canvas_->selectedPoint()); });
    // Browser parity (drawingApp.js clearAllLines): a wipe of every line asks first, in
    // the same styled confirm the Clear-project trash uses, and says so when declined.
    connect(actClearAll_, &QAction::triggered, this, [this] {
      ConfirmSpec spec;
      spec.title = tr("Clear all lines");
      spec.message = tr("Wipe ALL lines from the canvas? This cannot be undone except via Undo.");
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) {
        if (notify_) notify_->info(tr("Clear canceled"));   // declined = a notice, not a failure
        return;
      }
      canvas_->clearAll();
    });
    connect(actDeselect_, &QAction::triggered, canvas_, &CanvasWidget::deselect);
    connect(actZoomIn_, &QAction::triggered, this, &MainWindow::zoomIn);
    connect(actZoomOut_, &QAction::triggered, this, &MainWindow::zoomOut);
    connect(actFit_, &QAction::triggered, this, &MainWindow::fitToWindow);
    connect(actShowPoints_, &QAction::toggled, this, [this](bool on) {
      canvas_->setShowPoints(on);
      settings_.showPoints = on;
      fileStore::saveSettings(settings_);
    });
    connect(actShowLines_, &QAction::toggled, this, [this](bool on) {
      canvas_->setShowLines(on);
      settings_.showLines = on;
      fileStore::saveSettings(settings_);
    });
    connect(actTheme_, &QAction::triggered, this, &MainWindow::toggleTheme);
    // Points panel + top-menu (toolbars) show/hide, animated (slide). The floating arrow overlays
    // and the View-menu/hotkey both route through these actions.
    connect(actPanel_, &QAction::toggled, this, [this](bool on) { setPanelShown(on, true); });
    connect(actToolbars_, &QAction::toggled, this, [this](bool on) { setToolbarsShown(on, true); });
    connect(actFullscreen_, &QAction::triggered, this,
            &MainWindow::toggleFullscreen);
    // Escape-leaves-fullscreen is handled in the app-wide eventFilter (reliable across focus).
    fs_.hoverTimer = new QTimer(this);   // drives the fullscreen edge-hover reveal
    connect(fs_.hoverTimer, &QTimer::timeout, this, &MainWindow::fsHoverTick);
    connect(actSettings_, &QAction::triggered, this, &MainWindow::openSettings);
    connect(actProjects_, &QAction::triggered, this, &MainWindow::openProjects);
    connect(actConnect_, &QAction::triggered, this, &MainWindow::openConnections);
    connect(actLinks_, &QAction::triggered, this, &MainWindow::openLinks);
    connect(actDescription_, &QAction::triggered, this, &MainWindow::openDescription);
    connect(actKeywords_, &QAction::triggered, this, &MainWindow::openKeywords);
    // The assistant's own settings dialog (the chat's … menu ▸ Settings) on a chord of its
    // own — shared hotkeysConfig openAssistantSettings, so the browser answers the same keys.
    actAssistantSettings_ = newAction("AI Assistant Settings…", hotkey("openAssistantSettings", "Alt+Shift+G"));
    setActionTip(actAssistantSettings_, "AI assistant settings — provider, model & voice");
    connect(actAssistantSettings_, &QAction::triggered, this, &MainWindow::openAssistantSettings);
    connect(actOpenIn_, &QAction::triggered, this, &MainWindow::openInAnotherApp);
    connect(actNewProject_, &QAction::triggered, this,
            &MainWindow::newProjectFromCanvas);
    connect(actSaveProject_, &QAction::triggered, this,
            &MainWindow::saveToActiveProject);
    connect(actClearProject_, &QAction::triggered, this,
            &MainWindow::clearCurrentProject);
    connect(actSaveSession_, &QAction::triggered, this, [this] {
      saveSessionNow();
    });
    actShortcuts_ = newAction("Keyboard Shortcuts…", hotkey("openHotkeys", "Alt+K"));
    setActionTip(actShortcuts_, "Keyboard shortcuts");   // the browser #settings-btn title
    connect(actShortcuts_, &QAction::triggered, this,
            &MainWindow::openShortcuts);
    // Keyboard route to the canvas right-click menu (shared hotkeysConfig contextMenu).
    // Registered on the window only — no menu-bar entry, the menu IS the entry.
    actContextMenu_ = newAction("Canvas Context Menu", hotkey("contextMenu", "Shift+F10"));
    setActionTip(actContextMenu_, "Open the canvas context menu at the pointer (or the canvas centre)");
    connect(actContextMenu_, &QAction::triggered, this, &MainWindow::showContextMenuFromKeyboard);

  }

}  // namespace stencil::gui
