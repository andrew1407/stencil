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
    const bool ro = canvas_->compareReadOnly();
    actUndo_->setEnabled(canvas_->canUndo() && !ro);
    actRedo_->setEnabled(canvas_->canRedo() && !ro);
    actSaveProject_->setEnabled(!activeProjectId_.isEmpty());
    // Mirrors the browser HK_HANDLERS startDraw/stopDraw guards.
    const bool drawing = canvas_->isDrawing();
    actStartDraw_->setEnabled(canvas_->hasImage() && !drawing && !ro);
    actStopDraw_->setEnabled(drawing && !ro);
    // One Draw button for both: handing it the other action carries icon, tooltip, state and
    // target across (browser: syncDrawToggleUI).
    if (startDrawBtn_) {
      // Pin the width to the wider label once (browser: .btn-draw-fixed); measured here because
      // the icon and padding exist only after the first show.
      if (startDrawBtn_->maximumWidth() == QWIDGETSIZE_MAX && startDrawBtn_->isVisible() &&
          !startDrawBtn_->icon().isNull()) {
        QAction* keep = startDrawBtn_->defaultAction();
        int widest = 0;
        for (QAction* state : {actStartDraw_, actStopDraw_}) {
          startDrawBtn_->setDefaultAction(state);
          widest = std::max(widest, startDrawBtn_->sizeHint().width());
        }
        startDrawBtn_->setDefaultAction(keep);
        startDrawBtn_->setFixedWidth(widest);
      }
      // Animated, and a no-op when the state has not moved, so refreshActions can call this
      // freely.
      syncDrawToggleFace(drawing, true);
    }
    // Same gate as the browser's #draw-mode-toggle and the same one-shot width pin as Start.
    if (drawModeBtn_) {
      drawModeBtn_->setEnabled(canvas_->hasImage() && !ro);
      if (drawModeBtn_->maximumWidth() == QWIDGETSIZE_MAX && drawModeBtn_->isVisible() &&
          !drawModeBtn_->icon().isNull()) {
        const QString keep = drawModeBtn_->text();
        int widest = 0;
        for (const char* t : {"Line", "Rect"}) {
          drawModeBtn_->setText(t);
          widest = std::max(widest, drawModeBtn_->sizeHint().width());
        }
        drawModeBtn_->setText(keep);
        drawModeBtn_->setFixedWidth(widest);
      }
    }
    actNewLine_->setEnabled(!ro);
    actDeleteLast_->setEnabled(!ro);
    // Clear All Lines greys with nothing to clear (browser setDisabled('clear-all-lines')).
    actClearAll_->setEnabled(!ro && !canvas_->allLines().empty());
    actDeleteLine_->setEnabled(!ro);
    actDeletePoint_->setEnabled(!ro);
    actIncognito_->setEnabled(!canvas_->hasImage());
    // Paste stays enabled so the Ctrl+V dispatch can still notify "Load an image first".
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();
    // Crop and rotate gate on image presence only (browser drawingApp.updateButtons): the
    // "Original" compare view still reflects them.
    actCrop_->setEnabled(hasImg);
    actRotateLeft_->setEnabled(hasImg);
    actRotateRight_->setEnabled(hasImg);
    // Nothing to zoom without an image (browser: zoom-in / zoom-out / zoom-fit / zoom-input).
    actZoomIn_->setEnabled(hasImg);
    actZoomOut_->setEnabled(hasImg);
    actFit_->setEnabled(hasImg);
    if (zoom_) zoom_->setEnabled(hasImg);
    // browser: setDisabled('image-filter', !hasImage); the tint swatch rides along.
    if (imageFilter_) imageFilter_->setEnabled(hasImg);
    if (filterColorBtn_) filterColorBtn_->setEnabled(hasImg);
    if (actCycleFilter_) actCycleFilter_->setEnabled(hasImg);
    // restoreSession() ignores a session with no image and no lines, so saving one is a true no-
    // op.
    actSaveSession_->setEnabled(hasImg || hasLines);
    actDownloadJson_->setEnabled(hasLines);
    actCopyLayout_->setEnabled(hasLines);
    actUploadJson_->setEnabled(hasImg);
    actSaveProjectFile_->setEnabled(hasImg);
    actPasteLayout_->setEnabled(hasImg);
    syncExportActions();
    // Empty state (browser #load-image-btn <-> #image-actions): the BUTTONS are toggled, never the
    // actions, which also back menu entries. Half sand (controlState.js parity).
    const auto swapShown = [](QWidget* w, bool show) {
      revealControls(w, show, /*dust=*/show);   // arrivals ride the sand; leaving is instant
    };
    if (openImageBtn_) swapShown(openImageBtn_, !hasImg);
    if (imageSection_) {
      for (QToolButton* b : imageSection_->findChildren<QToolButton*>()) {
        if (b == openImageBtn_) continue;
        swapShown(b, sectionButtonVisible(b->defaultAction(), b));
      }
    }
    if (compareCombo_) compareCombo_->setEnabled(hasImg);
    if (actCycleCompare_) actCycleCompare_->setEnabled(hasImg);
    if (compareGroup_) compareGroup_->setEnabled(hasImg);
    // "Open in…" mirrors the browser's #open-in-btn gating: hidden with no target, else enabled
    // only with an image.
    if (actOpenIn_) {
      const bool serverProj = !remoteSession_->link().address.isEmpty() && !remoteSession_->link().id.isEmpty();
      const bool browserAvail = !settings_.browserBaseUrl.trimmed().isEmpty();
      const bool telegramAvail = !settings_.telegramBotUsername.trimmed().isEmpty() && serverProj;
      const bool anyAvail = browserAvail || telegramAvail;
      actOpenIn_->setVisible(anyAvail);
      actOpenIn_->setEnabled(hasImg && anyAvail);
    }
    // Hidden whenever the session is server-linked (browser updateButtons: clearBtn hides for
    // remoteLink).
    if (actClearProject_) {
      actClearProject_->setVisible(remoteSession_->link().address.isEmpty());
      actClearProject_->setEnabled(canvas_->hasImage());
    }
    updateProjectTitle();   // keep the window title + toolbar name field in sync
    // Rename follows the name field itself, so the menu entry and the ✎ agree.
    if (actRenameProject_) actRenameProject_->setEnabled(nameBar_.field && nameBar_.field->isEnabled());
  }

}  // namespace stencil::gui
