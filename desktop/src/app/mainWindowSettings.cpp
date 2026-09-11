#include "mainWindow.hpp"
#include <QToolButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "stayOpenMenu.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "chatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/wrapRow.hpp"

#include <QCloseEvent>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QSignalBlocker>

// Show/close, the theme toggle and applying the Settings dialog's result.

namespace stencil::gui {

  // One-shot first-show fade-in (browser appReveal counterpart). Ramps window
  // opacity — no per-child graphics effect, so the canvas paint path is untouched.
  void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (!firstShow_) return;
    firstShow_ = false;
    setWindowOpacity(0.0);
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(240);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
  }

  // Closing is IMMEDIATE on every path — ⌘Q / app-menu Quit / Dock Quit /
  // window ✕ / Alt+F4 / window-manager close — with no confirmation modal
  // (deliberate user decision); autosave/session persistence below preserves
  // the work regardless.
  void MainWindow::closeEvent(QCloseEvent* event) {
    // Persist the dock/toolbar layout (selection-panel area etc.). The chat
    // dock rides along in the blob but is reset to hidden/default on boot —
    // it is session-transient like the browser panel. Incognito never writes
    // (persistSettings gates it).
    settings_.windowState = QString::fromLatin1(saveState(kToolbarLayoutVersion).toBase64());
    persistSettings();
    fileStore::flushWrites();   // any debounced registry write still inside its window
    QMainWindow::closeEvent(event);
  }

  // Ctrl+D sets an explicit light/dark and stops following the OS (browser
  // behavior: a manual toggle overrides the system preference).
  void MainWindow::toggleTheme() {
    // One flip at a time: a second press mid-wipe restyles the window under an overlay
    // holding the PREVIOUS snapshot, and the two palettes tear across each other. The
    // browser gets this from the View Transitions API (a new transition supersedes the
    // one in flight); here the press is simply dropped until the wipe has finished.
    if (themeSwapping()) return;
    settings_.themeMode = resolveDark(settings_.themeMode) ? "light" : "dark";
    applySettings(settings_, true);
  }

  void MainWindow::applySettings(const Settings& s, bool persist) {
    // Re-probe the provider (a network round-trip) only when its config moved — the
    // live-apply Settings dialog routes every unrelated control click through here.
    const bool llmChanged = settings_.llmProvider != s.llmProvider
        || settings_.llmBaseUrl != s.llmBaseUrl || settings_.llmModel != s.llmModel
        || settings_.llmApiKey != s.llmApiKey || settings_.llmServerUrl != s.llmServerUrl;
    settings_ = s;
    // Motion, before anything below can play: the two switches every animation in the
    // app asks (support/modalReveal.hpp). The dialog live-applies, so flipping the mode
    // there takes effect on that dialog's own closing flight.
    support::setMotionMode(support::motionModeFromKey(s.motionMode));
    support::setDrawingAnimations(s.drawingAnimations);
    canvas_->setDefaults(s.defaultColor, s.defaultThickness, s.defaultPointSize,
                         s.defaultStyle, s.defaultPointColor);
    canvas_->setHoldDrawDelay(s.holdDrawDelay);
    canvas_->setHighlightColors(QColor(s.selGlowColor), QColor(s.hoverRingColor),
                                QColor(s.focusRingColor));
    selectedLineBar_->setDefaultFillColor(QColor(s.defaultFillColor));
    {
      QSignalBlocker bp(actShowPoints_);
      QSignalBlocker bl(actShowLines_);
      QSignalBlocker bt(actTooltip_);
      actShowPoints_->setChecked(s.showPoints);
      actShowLines_->setChecked(s.showLines);
      actTooltip_->setChecked(s.tooltipEnabled);
    }
    syncUnitControls();
    applyUnitToPageCombo();  // page-format labels in the restored unit
    canvas_->setShowPoints(s.showPoints);
    canvas_->setShowLines(s.showLines);
    {
      QSignalBlocker b(pageSize_);
      const int idx = pageSize_->findData(s.pageSize);
      if (idx >= 0) pageSize_->setCurrentIndex(idx);
    }
    // Sync custom page-size inputs in the active display unit.
    if (customW_) {
      applyUnitToPageInputs();
      revealControls(customGroup_, s.pageSize == "custom");
    }
    if (allowFormulas_) {
      QSignalBlocker ba(allowFormulas_);
      QSignalBlocker bx(formulaX_);
      QSignalBlocker by(formulaY_);
      allowFormulas_->setChecked(s.allowFormulas);
      formulaX_->setText(s.formulaX);
      formulaY_->setText(s.formulaY);
      revealControls(formulaGroup_, s.allowFormulas);
      formulaError_->setVisible(false);
      if (actAllowFormulas_) {
        QSignalBlocker baf(actAllowFormulas_);
        actAllowFormulas_->setChecked(s.allowFormulas);
      }
    }
    // Seed the Style toolbar row: line defaults, image filter + tint. All
    // under signal blockers so seeding doesn't re-trigger the change handlers /
    // re-persist. The filter is applied to the canvas once at the end.
    lineColorValue_ = QColor(s.defaultColor);
    filterColorValue_ = QColor(s.filterColor);
    // The chips themselves are painted by applyTheme() at the end of this function, not
    // here: their frame is palette-coloured, and `settings_ = s` above has ALREADY handed
    // them the new theme — so repainting them now bakes the new border into the snapshot
    // the wipe is about to take, and the pickers sit there light while the window around
    // them is still dark until the circle finally reaches them. Only the values change here.
    if (lineThickness_) {
      QSignalBlocker bt(lineThickness_);
      lineThickness_->setValue(qRound(s.defaultThickness));
    }
    if (pointSize_) {
      QSignalBlocker bm(pointSize_);
      pointSize_->setValue(qRound(s.defaultPointSize));
    }
    if (lineStyle_) {
      QSignalBlocker bs(lineStyle_);
      const int idx = lineStyle_->findData(s.defaultStyle);
      lineStyle_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (imageFilter_) {
      QSignalBlocker bf(imageFilter_);
      const int idx = imageFilter_->findData(s.imageFilter);
      imageFilter_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (filterColorBtn_) filterColorBtn_->setVisible(s.imageFilter == "custom");
    canvas_->setImageFilter(s.imageFilter, filterColorValue_);
    if (chatDock_ && llmChanged) refreshLlmStatus();  // re-describe + re-probe the AI provider
    applyTheme();
    // Even an explicit Settings-dialog save is suppressed in incognito.
    if (persist && !incognito_) fileStore::saveSettings(settings_);
  }

}  // namespace stencil::gui
