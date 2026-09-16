#include "MainWindow.hpp"
#include <QToolButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include "mainWindowShared.hpp"
#include "MainWindow.hpp"
#include "StayOpenMenu.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "ChatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "ShortcutsDialog.hpp"
#include "theme.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/WrapRow.hpp"

#include <QCloseEvent>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QSignalBlocker>

// Show/close, the theme toggle and applying the Settings dialog's result.

namespace stencil::gui {

  // Browser appReveal counterpart: window opacity, no per-child effect, so the canvas paint path
  // is untouched.
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

  // Closing is immediate on every path, no confirmation; autosave preserves the work.
  void MainWindow::closeEvent(QCloseEvent* event) {
    // The chat dock rides along but resets on boot (session-transient); incognito never writes
    // (persistSettings).
    settings_.windowState = QString::fromLatin1(saveState(TOOLBAR_LAYOUT_VERSION).toBase64());
    persistSettings();
    fileStore::flushWrites();   // any debounced registry write still inside its window
    QMainWindow::closeEvent(event);
  }

  // A manual toggle stops following the OS (browser behaviour).
  void MainWindow::toggleTheme() {
    // One flip at a time: a second press mid-wipe tears the two palettes across each other. The
    // press is dropped until the wipe ends.
    if (themeSwapping()) return;
    settings_.themeMode = resolveDark(settings_.themeMode) ? "light" : "dark";
    applySettings(settings_, true);
  }

  void MainWindow::applySettings(const Settings& s, bool persist) {
    // Re-probe the provider only when its config moved; the live-apply dialog routes every click
    // through here.
    const bool llmChanged = settings_.llmProvider != s.llmProvider
        || settings_.llmBaseUrl != s.llmBaseUrl || settings_.llmModel != s.llmModel
        || settings_.llmApiKey != s.llmApiKey || settings_.llmServerUrl != s.llmServerUrl;
    settings_ = s;
    // Motion first: every animation asks these switches (support/modalReveal.hpp), the dialog's
    // own closing flight included.
    support::setMotionMode(support::motionModeFromKey(s.motionMode));
    support::setDrawingAnimations(s.drawingAnimations);
    support::setModalBackdrop(s.modalBackdrop);
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
      QSignalBlocker b(units_.pageSize);
      const int idx = units_.pageSize->findData(s.pageSize);
      if (idx >= 0) units_.pageSize->setCurrentIndex(idx);
    }
    if (units_.customW) {
      applyUnitToPageInputs();
      revealControls(units_.customGroup, s.pageSize == "custom");
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
    // Under signal blockers so seeding does not re-persist; the filter is applied once at the end.
    lineColorValue_ = QColor(s.defaultColor);
    filterColorValue_ = QColor(s.filterColor);
    // The chips are repainted by applyTheme() at the end, not here: `settings_ = s` already holds
    // the new theme, and painting now bakes it into the wipe's snapshot.
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
    if (persist && !incognito_) fileStore::saveSettings(settings_);
  }

}  // namespace stencil::gui
