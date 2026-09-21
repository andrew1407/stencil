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
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/control/WrapRow.hpp"

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
    if (!firstShow) return;
    firstShow = false;
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
    settings.windowState = QString::fromLatin1(saveState(TOOLBAR_LAYOUT_VERSION).toBase64());
    persistSettings();
    fileStore::flushWrites();   // any debounced registry write still inside its window
    QMainWindow::closeEvent(event);
  }

  // A manual toggle stops following the OS (browser behaviour).
  void MainWindow::toggleTheme() {
    // One flip at a time: a second press mid-wipe tears the two palettes across each other. The
    // press is dropped until the wipe ends.
    if (themeSwapping()) return;
    settings.themeMode = resolveDark(settings.themeMode) ? "light" : "dark";
    applySettings(settings, true);
  }

  void MainWindow::applySettings(const Settings& s, bool persist) {
    // Re-probe the provider only when its config moved; the live-apply dialog routes every click
    // through here.
    const bool llmChanged = settings.llmProvider != s.llmProvider
        || settings.llmBaseUrl != s.llmBaseUrl || settings.llmModel != s.llmModel
        || settings.llmApiKey != s.llmApiKey || settings.llmServerUrl != s.llmServerUrl;
    settings = s;
    // Motion first: every animation asks these switches (support/modalReveal.hpp), the dialog's
    // own closing flight included.
    support::setMotionMode(support::motionModeFromKey(s.motionMode));
    support::setDrawingAnimations(s.drawingAnimations);
    support::setModalBackdrop(s.modalBackdrop);
    canvas->setDefaults(s.defaultColor, s.defaultThickness, s.defaultPointSize,
                         s.defaultStyle, s.defaultPointColor);
    canvas->setHoldDrawDelay(s.holdDrawDelay);
    canvas->setHighlightColors(QColor(s.selGlowColor), QColor(s.hoverRingColor),
                                QColor(s.focusRingColor));
    selectedLineBar->setDefaultFillColor(QColor(s.defaultFillColor));
    {
      QSignalBlocker bp(actShowPoints);
      QSignalBlocker bl(actShowLines);
      QSignalBlocker bt(actTooltip);
      actShowPoints->setChecked(s.showPoints);
      actShowLines->setChecked(s.showLines);
      actTooltip->setChecked(s.tooltipEnabled);
    }
    syncUnitControls();
    applyUnitToPageCombo();  // page-format labels in the restored unit
    canvas->setShowPoints(s.showPoints);
    canvas->setShowLines(s.showLines);
    {
      QSignalBlocker b(units.pageSize);
      const int idx = units.pageSize->findData(s.pageSize);
      if (idx >= 0) units.pageSize->setCurrentIndex(idx);
    }
    if (units.customW) {
      applyUnitToPageInputs();
      revealControls(units.customGroup, s.pageSize == "custom");
    }
    if (allowFormulas) {
      QSignalBlocker ba(allowFormulas);
      QSignalBlocker bx(formulaX);
      QSignalBlocker by(formulaY);
      allowFormulas->setChecked(s.allowFormulas);
      formulaX->setText(s.formulaX);
      formulaY->setText(s.formulaY);
      revealControls(formulaGroup, s.allowFormulas);
      formulaError->setVisible(false);
      if (actAllowFormulas) {
        QSignalBlocker baf(actAllowFormulas);
        actAllowFormulas->setChecked(s.allowFormulas);
      }
    }
    // Under signal blockers so seeding does not re-persist; the filter is applied once at the end.
    lineColorValue = QColor(s.defaultColor);
    filterColorValue = QColor(s.filterColor);
    // The chips are repainted by applyTheme() at the end, not here: `settings = s` already holds
    // the new theme, and painting now bakes it into the wipe's snapshot.
    if (lineThickness) {
      QSignalBlocker bt(lineThickness);
      lineThickness->setValue(qRound(s.defaultThickness));
    }
    if (pointSize) {
      QSignalBlocker bm(pointSize);
      pointSize->setValue(qRound(s.defaultPointSize));
    }
    if (lineStyle) {
      QSignalBlocker bs(lineStyle);
      const int idx = lineStyle->findData(s.defaultStyle);
      lineStyle->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (imageFilter) {
      QSignalBlocker bf(imageFilter);
      const int idx = imageFilter->findData(s.imageFilter);
      imageFilter->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (filterColorBtn) filterColorBtn->setVisible(s.imageFilter == "custom");
    canvas->setImageFilter(s.imageFilter, filterColorValue);
    if (chatDock && llmChanged) refreshLlmStatus();  // re-describe + re-probe the AI provider
    applyTheme();
    if (persist && !incognito) fileStore::saveSettings(settings);
  }

}  // namespace stencil::gui
