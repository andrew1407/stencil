#include "MainWindow.hpp"
#include "ChatSessionController.hpp"
#include "Notifications.hpp"
#include "PlanAwait.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include "mainWindowShared.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "RemoteSyncController.hpp"
#include "SelectedLineBar.hpp"
#include "../../support/skinPrefs.hpp"
#include "../../support/control/reveal/controlReveal.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>

// Show/close, the theme toggle and applying the Settings dialog's result.

namespace stencil::gui {

  // A boot restore still decoding holds the window unseen until it lands, as the constructor's own
  // decode once held the first show; restoreSession then fades it in.
  void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (!firstShow) return;
    firstShow = false;
    setWindowOpacity(0.0);
    docSource.revealHeld = parts.persistence.sessionRestorePending();
    if (!docSource.revealHeld) fadeInWindow(this);
  }

  // Closing is immediate on every path, no confirmation; autosave preserves the work.
  void MainWindow::closeEvent(QCloseEvent* event) {
    // Inside a nested loop or mid-plan the close is re-posted once the loops have unwound and the
    // lapsed awaits have answered, both queued ahead of it (see PopoverHost::loops and ::awaits).
    QWidget* modal = QApplication::activeModalWidget();
    bool ownModal = false;   // isAncestorOf stops at a window edge, and a dialog is its own window
    for (QWidget* p = modal ? modal->parentWidget() : nullptr; p && !ownModal; p = p->parentWidget())
      ownModal = p == this;
    if (!pop.loops.isEmpty() || !pop.awaits.isEmpty() || ownModal) {
      event->ignore();
      for (const QPointer<QEventLoop>& loop : pop.loops)
        if (loop) loop->quit();
      for (PlanAwait* await : QList<PlanAwait*>(pop.awaits)) await->lapse();
      if (auto* dlg = ownModal ? qobject_cast<QDialog*>(modal) : nullptr) dlg->reject();
      QTimer::singleShot(0, this, [self = QPointer<MainWindow>(this)] { if (self) self->close(); });
      return;
    }
    // The server's baked result lags the layout by its throttle; the window waits for the last one.
    if (remoteSync && remoteSync->holdCloseForResult([self = QPointer<MainWindow>(this)] {
          if (self) self->close();
        })) {
      event->ignore();
      hide();
      return;
    }
    // The chat dock rides along but resets on boot (session-transient); incognito never writes
    // (persistSettings).
    settings.windowState = QString::fromLatin1(editor->saveState(TOOLBAR_LAYOUT_VERSION).toBase64());
    persistSettings();
    fileStore::flushWrites();   // any debounced registry write still inside its window
    QMainWindow::closeEvent(event);
  }


  void MainWindow::applySettings(const Settings& s, bool persist) {
    // Re-probe the provider only when its config moved; the live-apply dialog routes every click
    // through here.
    const bool llmChanged = settings.llmProvider != s.llmProvider
        || settings.llmBaseUrl != s.llmBaseUrl || settings.llmModel != s.llmModel
        || settings.llmApiKey != s.llmApiKey || settings.llmServerUrl != s.llmServerUrl;
    // A theme the user chooses ends a skin's forced one; a motion switch the user moves ends its override.
    if (s.themeMode != settings.themeMode) support::clearForcedDark();
    const bool channelMoved = s.notifyChannel != settings.notifyChannel;
    const bool motionMoved = s.motionMode != settings.motionMode
        || s.drawingAnimations != settings.drawingAnimations || s.modalBackdrop != settings.modalBackdrop;
    settings = s;
    // Motion first: every animation asks these switches (support/modal/modalReveal.hpp), the dialog's
    // own closing flight included.
    support::setMotionMode(support::motionModeFromKey(s.motionMode));
    support::setDrawingAnimations(s.drawingAnimations);
    support::setModalBackdrop(s.modalBackdrop);
    if (motionMoved) support::clearMotionOverride();
    if (notify) {
      const NotifyChannel channel = notifyChannelFromKey(s.notifyChannel);
      notify->setChannel(channel);
      if (channelMoved && channel == NotifyChannel::SYSTEM && !notify->isSystemAvailable())
        notify->info(tr("System notifications are not available here — showing them in the app"));
    }
    canvas->setDefaults(s.defaultColor, s.defaultThickness, s.defaultPointSize,
                         s.defaultStyle, s.defaultPointColor);
    canvas->setHoldDrawDelay(s.holdDrawDelay);
    canvas->setHighlightColors(QColor(s.selGlowColor), QColor(s.hoverRingColor),
                                QColor(s.focusRingColor));
    selectedLineBar->setDefaultFillColor(QColor(s.defaultFillColor));
    {
      QSignalBlocker bp(acts.showPoints);
      QSignalBlocker bl(acts.showLines);
      QSignalBlocker bt(acts.tooltip);
      acts.showPoints->setChecked(s.showPoints);
      acts.showLines->setChecked(s.showLines);
      acts.tooltip->setChecked(s.tooltipEnabled);
      // The blocked actions never tell the toolbar's bound checkboxes, so they are set here too.
      for (auto [box, on] : {std::pair{tools.showPointsCheck, s.showPoints}, std::pair{tools.showLinesCheck, s.showLines}})
        if (box) { QSignalBlocker bb(box); box->setChecked(on); }
    }
    parts.view.syncUnitControls();
    parts.view.applyUnitToPageCombo();  // page-format labels in the restored unit
    canvas->setShowPoints(s.showPoints);
    canvas->setShowLines(s.showLines);
    {
      QSignalBlocker b(units.pageSize);
      const int idx = units.pageSize->findData(s.pageSize);
      if (idx >= 0) units.pageSize->setCurrentIndex(idx);
    }
    if (units.customW) {
      parts.view.applyUnitToPageInputs();
      revealControls(units.customGroup, s.pageSize == "custom");
    }
    if (tools.allowFormulas) {
      QSignalBlocker ba(tools.allowFormulas);
      QSignalBlocker bx(tools.formulaX);
      QSignalBlocker by(tools.formulaY);
      tools.allowFormulas->setChecked(s.allowFormulas);
      tools.formulaX->setText(s.formulaX);
      tools.formulaY->setText(s.formulaY);
      revealControls(tools.formulaGroup, s.allowFormulas);
      tools.formulaError->setVisible(false);
      if (acts.allowFormulas) {
        QSignalBlocker baf(acts.allowFormulas);
        acts.allowFormulas->setChecked(s.allowFormulas);
      }
    }
    // Under signal blockers so seeding does not re-persist; the filter is applied once at the end.
    tools.lineColorValue = QColor(s.defaultColor);
    tools.filterColorValue = QColor(s.filterColor);
    // The chips are repainted by applyTheme() at the end, not here: `settings = s` already holds
    // the new theme, and painting now bakes it into the wipe's snapshot.
    if (tools.lineThickness) {
      QSignalBlocker bt(tools.lineThickness);
      tools.lineThickness->setValue(qRound(s.defaultThickness));
    }
    if (tools.pointSize) {
      QSignalBlocker bm(tools.pointSize);
      tools.pointSize->setValue(qRound(s.defaultPointSize));
    }
    if (tools.lineStyle) {
      QSignalBlocker bs(tools.lineStyle);
      const int idx = tools.lineStyle->findData(s.defaultStyle);
      tools.lineStyle->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (tools.imageFilter) {
      QSignalBlocker bf(tools.imageFilter);
      const int idx = tools.imageFilter->findData(s.imageFilter);
      tools.imageFilter->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (tools.filterColorBtn) tools.filterColorBtn->setVisible(s.imageFilter == "custom");
    canvas->setImageFilter(s.imageFilter, tools.filterColorValue);
    if (chatDock && llmChanged) chatSession->refreshLlmStatus();  // re-describe + re-probe the AI provider
    applyTheme();
    if (persist && !incognito) fileStore::saveSettings(settings);
  }

}  // namespace stencil::gui
