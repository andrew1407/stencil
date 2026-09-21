#include "MainWindow.hpp"
#include <QActionGroup>
#include <QComboBox>
#include <QLineEdit>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "ChatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "QtLlmTransport.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "ControlsPill.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "SelectionPanel.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/WrapRow.hpp"
#include "../support/modalChrome.hpp"

#include <QAction>
#include <QToolButton>
#include <algorithm>

// refreshActions(): the single enable/visibility pass over every action and control.

namespace stencil::gui {

  void MainWindow::refreshActions() {
    // A compare view is read-only: every annotation-editing action is disabled while it is on.
    const bool ro = canvas->compareReadOnly();
    actUndo->setEnabled(canvas->canUndo() && !ro);
    actRedo->setEnabled(canvas->canRedo() && !ro);
    actSaveProject->setEnabled(!activeProjectId.isEmpty());
    // Mirrors the browser HK_HANDLERS startDraw/stopDraw guards.
    const bool drawing = canvas->getIsDrawing();
    actStartDraw->setEnabled(canvas->hasImage() && !drawing && !ro);
    actStopDraw->setEnabled(drawing && !ro);
    // One Draw button for both: handing it the other action carries icon, tooltip, state and
    // target across (browser: syncDrawToggleUI).
    if (startDrawBtn) {
      // Pin the width to the wider label once (browser: .btn-draw-fixed); measured here because
      // the icon and padding exist only after the first show.
      if (startDrawBtn->maximumWidth() == QWIDGETSIZE_MAX && startDrawBtn->isVisible() &&
          !startDrawBtn->icon().isNull()) {
        QAction* keep = startDrawBtn->defaultAction();
        int widest = 0;
        for (QAction* state : {actStartDraw, actStopDraw}) {
          startDrawBtn->setDefaultAction(state);
          widest = std::max(widest, startDrawBtn->sizeHint().width());
        }
        startDrawBtn->setDefaultAction(keep);
        startDrawBtn->setFixedWidth(widest);
      }
      // Animated, and a no-op when the state has not moved, so refreshActions can call this
      // freely.
      syncDrawToggleFace(drawing, true);
    }
    // Same gate as the browser's #draw-mode-toggle and the same one-shot width pin as Start.
    if (drawModeBtn) {
      drawModeBtn->setEnabled(canvas->hasImage() && !ro);
      if (drawModeBtn->maximumWidth() == QWIDGETSIZE_MAX && drawModeBtn->isVisible() &&
          !drawModeBtn->icon().isNull()) {
        const QString keep = drawModeBtn->text();
        int widest = 0;
        for (const char* t : {"Line", "Rect"}) {
          drawModeBtn->setText(t);
          widest = std::max(widest, drawModeBtn->sizeHint().width());
        }
        drawModeBtn->setText(keep);
        drawModeBtn->setFixedWidth(widest);
      }
    }
    actNewLine->setEnabled(!ro);
    actDeleteLast->setEnabled(!ro);
    // Clear All Lines greys with nothing to clear (browser setDisabled('clear-all-lines')).
    actClearAll->setEnabled(!ro && !canvas->allLines().empty());
    actDeleteLine->setEnabled(!ro);
    actDeletePoint->setEnabled(!ro);
    actIncognito->setEnabled(!canvas->hasImage());
    // Paste stays enabled so the Ctrl+V dispatch can still notify "Load an image first".
    const bool hasImg = canvas->hasImage();
    const bool hasLines = !canvas->allLines().empty();
    // Crop and rotate gate on image presence only (browser drawingApp.updateButtons): the
    // "Original" compare view still reflects them.
    actCrop->setEnabled(hasImg);
    actRotateLeft->setEnabled(hasImg);
    actRotateRight->setEnabled(hasImg);
    // Nothing to zoom without an image (browser: zoom-in / zoom-out / zoom-fit / zoom-input).
    actZoomIn->setEnabled(hasImg);
    actZoomOut->setEnabled(hasImg);
    actFit->setEnabled(hasImg);
    if (zoom) zoom->setEnabled(hasImg);
    // browser: setDisabled('image-filter', !hasImage); the tint swatch rides along.
    if (imageFilter) imageFilter->setEnabled(hasImg);
    if (filterColorBtn) filterColorBtn->setEnabled(hasImg);
    if (actCycleFilter) actCycleFilter->setEnabled(hasImg);
    // restoreSession() ignores a session with no image and no lines, so saving one is a true no-
    // op.
    actSaveSession->setEnabled(hasImg || hasLines);
    actDownloadJson->setEnabled(hasLines);
    actCopyLayout->setEnabled(hasLines);
    actUploadJson->setEnabled(hasImg);
    actSaveProjectFile->setEnabled(hasImg);
    actPasteLayout->setEnabled(hasImg);
    syncExportActions();
    // Empty state (browser #load-image-btn <-> #image-actions): the BUTTONS are toggled, never the
    // actions, which also back menu entries. Half sand (controlState.js parity).
    const auto swapShown = [](QWidget* w, bool show) {
      revealControls(w, show, /*dust=*/show);   // arrivals ride the sand; leaving is instant
    };
    if (openImageBtn) swapShown(openImageBtn, !hasImg);
    if (imageSection) {
      for (QToolButton* b : imageSection->findChildren<QToolButton*>()) {
        if (b == openImageBtn) continue;
        swapShown(b, sectionButtonVisible(b->defaultAction(), b));
      }
    }
    if (compareCombo) compareCombo->setEnabled(hasImg);
    if (actCycleCompare) actCycleCompare->setEnabled(hasImg);
    if (compareGroup) compareGroup->setEnabled(hasImg);
    // "Open in…" mirrors the browser's #open-in-btn gating: hidden with no target, else enabled
    // only with an image.
    if (actOpenIn) {
      const bool serverProj = !remoteSession->getLink().address.isEmpty() && !remoteSession->getLink().id.isEmpty();
      const bool browserAvail = !settings.browserBaseUrl.trimmed().isEmpty();
      const bool telegramAvail = !settings.telegramBotUsername.trimmed().isEmpty() && serverProj;
      const bool anyAvail = browserAvail || telegramAvail;
      actOpenIn->setVisible(anyAvail);
      actOpenIn->setEnabled(hasImg && anyAvail);
    }
    // Hidden whenever the session is server-linked (browser updateButtons: clearBtn hides for
    // remoteLink).
    if (actClearProject) {
      actClearProject->setVisible(remoteSession->getLink().address.isEmpty());
      actClearProject->setEnabled(canvas->hasImage());
    }
    updateProjectTitle();   // keep the window title + toolbar name field in sync
    // Rename follows the name field itself, so the menu entry and the ✎ agree.
    if (actRenameProject) actRenameProject->setEnabled(nameBar.field && nameBar.field->isEnabled());
  }

}  // namespace stencil::gui
