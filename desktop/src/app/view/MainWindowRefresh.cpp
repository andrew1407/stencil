#include "mainWindowHelpers.hpp"
#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include <QActionGroup>
#include <QComboBox>
#include "CanvasWidget.hpp"
#include "RemoteSession.hpp"
#include "../../support/control/reveal/controlReveal.hpp"

#include <QAction>
#include <QToolButton>
#include <algorithm>

// refreshActions(): the single enable/visibility pass over every action and control.

namespace stencil::gui {

  void MainWindow::refreshActions() {
    // A compare view is read-only: every annotation-editing action is disabled while it is on.
    const bool ro = canvas->compareReadOnly();
    acts.undo->setEnabled(canvas->canUndo() && !ro);
    acts.redo->setEnabled(canvas->canRedo() && !ro);
    acts.saveProject->setEnabled(!activeProjectId.isEmpty());
    // Mirrors the browser HK_HANDLERS startDraw/stopDraw guards.
    const bool drawing = canvas->getIsDrawing();
    acts.startDraw->setEnabled(canvas->hasImage() && !drawing && !ro);
    acts.stopDraw->setEnabled(drawing && !ro);
    // One Draw button for both: handing it the other action carries icon, tooltip, state and
    // target across (browser: syncDrawToggleUI).
    if (tools.startDrawBtn) {
      // Pin the width to the wider label once (browser: .btn-draw-fixed); measured here because
      // the icon and padding exist only after the first show.
      if (tools.startDrawBtn->maximumWidth() == QWIDGETSIZE_MAX && tools.startDrawBtn->isVisible() &&
          !tools.startDrawBtn->icon().isNull()) {
        QAction* keep = tools.startDrawBtn->defaultAction();
        int widest = 0;
        for (QAction* state : {acts.startDraw, acts.stopDraw}) {
          tools.startDrawBtn->setDefaultAction(state);
          const int w = tools.startDrawBtn->sizeHint().width();
          tools.startDrawBtn->setProperty(faceHintKey(state->iconText()).constData(), w);
          widest = std::max(widest, w);
        }
        tools.startDrawBtn->setDefaultAction(keep);
        tools.startDrawBtn->setFixedWidth(widest);
      }
      // Animated, and a no-op when the state has not moved, so refreshActions can call this
      // freely.
      parts.theme.syncDrawToggleFace(drawing, true);
    }
    // Same gate as the browser's #draw-mode-toggle and the same one-shot width pin as Start.
    if (tools.drawModeBtn) {
      tools.drawModeBtn->setEnabled(canvas->hasImage() && !ro);
      if (tools.drawModeBtn->maximumWidth() == QWIDGETSIZE_MAX && tools.drawModeBtn->isVisible() &&
          !tools.drawModeBtn->icon().isNull()) {
        const QString keep = tools.drawModeBtn->text();
        int widest = 0;
        for (const char* t : {"Line", "Rect"}) {
          tools.drawModeBtn->setText(t);
          const int w = tools.drawModeBtn->sizeHint().width();
          tools.drawModeBtn->setProperty(faceHintKey(QLatin1String(t)).constData(), w);
          widest = std::max(widest, w);
        }
        tools.drawModeBtn->setText(keep);
        tools.drawModeBtn->setFixedWidth(widest);
      }
    }
    acts.newLine->setEnabled(!ro);
    acts.deleteLast->setEnabled(!ro);
    // Clear All Lines greys with nothing to clear (browser setDisabled('clear-all-lines')).
    acts.clearAll->setEnabled(!ro && !canvas->allLines().empty());
    acts.deleteLine->setEnabled(!ro);
    acts.deletePoint->setEnabled(!ro);
    acts.incognito->setEnabled(!canvas->hasImage());
    // Paste stays enabled so the Ctrl+V dispatch can still notify "Load an image first".
    const bool hasImg = canvas->hasImage();
    const bool hasLines = !canvas->allLines().empty();
    // Crop and rotate gate on image presence only (browser drawingApp.updateButtons): the
    // "Original" compare view still reflects them.
    acts.crop->setEnabled(hasImg);
    acts.rotateLeft->setEnabled(hasImg);
    acts.rotateRight->setEnabled(hasImg);
    // Nothing to zoom without an image (browser: zoom-in / zoom-out / zoom-fit / zoom-input).
    acts.zoomIn->setEnabled(hasImg);
    acts.zoomOut->setEnabled(hasImg);
    acts.fit->setEnabled(hasImg);
    if (zoom) zoom->setEnabled(hasImg);
    // The tint is chosen ahead of a picture too (browser control/state.js), so it never greys.
    if (tools.imageFilter) tools.imageFilter->setEnabled(true);
    if (tools.filterColorBtn) tools.filterColorBtn->setEnabled(true);
    if (acts.cycleFilter) acts.cycleFilter->setEnabled(true);
    // restoreSession() ignores a session with no image and no lines, so saving one is a true no-
    // op.
    acts.saveSession->setEnabled(hasImg || hasLines);
    acts.downloadJson->setEnabled(hasLines);
    acts.copyLayout->setEnabled(hasLines);
    acts.uploadJson->setEnabled(hasImg);
    acts.saveProjectFile->setEnabled(hasImg);
    acts.pasteLayout->setEnabled(hasImg);
    parts.exportMenus.syncExportActions();
    // Empty state (browser #load-image-btn <-> #image-actions): the BUTTONS are toggled, never the
    // actions, which also back menu entries. Half sand (state.js parity).
    const auto swapShown = [](QWidget* w, bool show) {
      revealControls(w, show, /*dust=*/show);   // arrivals ride the sand; leaving is instant
    };
    if (tools.openImageBtn) swapShown(tools.openImageBtn, !hasImg);
    if (tools.imageSection) {
      for (QToolButton* b : tools.imageSection->findChildren<QToolButton*>()) {
        if (b == tools.openImageBtn) continue;
        swapShown(b, parts.toolbarBuilder.sectionButtonVisible(b->defaultAction(), b));
      }
    }
    if (tools.compareCombo) tools.compareCombo->setEnabled(hasImg);
    if (acts.cycleCompare) acts.cycleCompare->setEnabled(hasImg);
    if (ctxMenu.compareGroup) ctxMenu.compareGroup->setEnabled(hasImg);
    // "Open in…" mirrors the browser's #open-in-btn gating: hidden with no target, else enabled
    // only with an image.
    if (acts.openIn) {
      const bool serverProj = !remote.session->getLink().address.isEmpty() && !remote.session->getLink().id.isEmpty();
      const bool browserAvail = !settings.browserBaseUrl.trimmed().isEmpty();
      const bool telegramAvail = !settings.telegramBotUsername.trimmed().isEmpty() && serverProj;
      const bool anyAvail = browserAvail || telegramAvail;
      acts.openIn->setVisible(anyAvail);
      acts.openIn->setEnabled(hasImg && anyAvail);
    }
    // Hidden whenever the session is server-linked (browser updateButtons: clearBtn hides for
    // remoteLink).
    if (acts.clearProject) {
      acts.clearProject->setVisible(remote.session->getLink().address.isEmpty());
      acts.clearProject->setEnabled(canvas->hasImage());
    }
    projectTitle->updateProjectTitle();   // keep the window title + toolbar name field in sync
    // Rename follows the name field itself, so the menu entry and the ✎ agree.
    if (acts.renameProject) acts.renameProject->setEnabled(nameBar.field && nameBar.field->isEnabled());
  }

}  // namespace stencil::gui
