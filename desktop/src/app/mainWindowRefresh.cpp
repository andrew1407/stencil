#include "mainWindow.hpp"
#include <QActionGroup>
#include <QComboBox>
#include <QLineEdit>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "chatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "qtLlmTransport.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "controlsPill.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/wrapRow.hpp"
#include "../support/modalChrome.hpp"

#include <QAction>
#include <QToolButton>
#include <algorithm>

// refreshActions(): the single enable/visibility pass over every action and control.

namespace stencil::gui {

  void MainWindow::refreshActions() {
    // A compare view is read-only — every annotation-editing action (and thus its keyboard
    // shortcut) is disabled while it's active. Only navigation + compare controls stay live.
    const bool ro = canvas_->compareReadOnly();
    actUndo_->setEnabled(canvas_->canUndo() && !ro);
    actRedo_->setEnabled(canvas_->canRedo() && !ro);
    actSaveProject_->setEnabled(!activeProjectId_.isEmpty());
    // Start only when an image is loaded and not already drawing; Stop only while
    // drawing (mirrors the browser HK_HANDLERS startDraw/stopDraw guards).
    const bool drawing = canvas_->isDrawing();
    actStartDraw_->setEnabled(canvas_->hasImage() && !drawing && !ro);
    actStopDraw_->setEnabled(drawing && !ro);
    // The toolbar shows ONE Draw button for both. Handing it the other action carries the
    // icon, tooltip, enabled state and click target across in one move — so it reads Stop
    // exactly while a session is live, and the two actions keep their own menu entries and
    // shortcuts (browser: DrawingApp.syncDrawToggleUI).
    if (startDrawBtn_) {
      // Pin the width to the wider of the two labels, once — otherwise "Start" → "Stop"
      // resizes the button and shifts the whole row (browser parity: .btn-draw-fixed).
      // Deferred to here because the themed icon and the stylesheet padding only exist
      // after the toolbar has been built and shown; measuring earlier comes out short.
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
      // Hand the button the state's action + face. The accent treatment (outlined while
      // idle, filled while drawing) rides along, and the change is animated — a no-op
      // when the state hasn't actually moved, so refreshActions can call this freely.
      syncDrawToggleFace(drawing, true);
    }
    // Its Draw-section neighbour, the Line/Rect toggle: same gate as the browser's
    // #draw-mode-toggle (drawingApp.js:2217 — needs an image, not while read-only), and the
    // same one-shot width pin as Start, so relabelling Line <-> Rect doesn't shift the row.
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
    // These are otherwise always enabled (they no-op internally when nothing applies);
    // the only gate is the read-only compare view.
    actNewLine_->setEnabled(!ro);
    actDeleteLast_->setEnabled(!ro);
    // …except Clear All Lines, which the browser greys with nothing to clear
    // (setDisabled('clear-all-lines', !hasLines || ro)) — and it is a loud red button.
    actClearAll_->setEnabled(!ro && !canvas_->allLines().empty());
    actDeleteLine_->setEnabled(!ro);
    actDeletePoint_->setEnabled(!ro);
    // Incognito can only be toggled before an image exists.
    actIncognito_->setEnabled(!canvas_->hasImage());
    // Data actions: layout export/copy need lines; importing a layout and
    // every image action need an image first (mirrors the browser guards). Paste
    // stays enabled so the Ctrl+V dispatch can still notify "Load an image first".
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();
    // Crop + the two rotations act on the loaded image, so grey them out without
    // one (parity with the browser's crop-image / rotate-left / rotate-right gating
    // in drawingApp.updateButtons — which gates on image presence only, since the
    // "Original" compare view still reflects crop + rotation, so no read-only gate).
    actCrop_->setEnabled(hasImg);
    actRotateLeft_->setEnabled(hasImg);
    actRotateRight_->setEnabled(hasImg);
    // Nothing to zoom without an image, so the whole ZOOM cluster goes dead — the two
    // step actions, the % field and Fit (browser: setDisabled over zoom-in / zoom-out /
    // zoom-fit / zoom-input). Alt+0 and the zoom shortcuts fall silent with them.
    actZoomIn_->setEnabled(hasImg);
    actZoomOut_->setEnabled(hasImg);
    actFit_->setEnabled(hasImg);
    if (zoom_) zoom_->setEnabled(hasImg);
    // The filter recolours the loaded image — nothing to apply it to without one
    // (browser: setDisabled('image-filter', !hasImage)). The tint swatch rides along.
    if (imageFilter_) imageFilter_->setEnabled(hasImg);
    if (filterColorBtn_) filterColorBtn_->setEnabled(hasImg);
    if (actCycleFilter_) actCycleFilter_->setEnabled(hasImg);
    // Save Session persists the whole blob (image, page, lines, filter, crop…), not
    // just the image — but restoreSession() ignores a session with no image AND no
    // lines, so saving in that state is a true no-op. Gate it on the same condition.
    actSaveSession_->setEnabled(hasImg || hasLines);
    actDownloadJson_->setEnabled(hasLines);
    actCopyLayout_->setEnabled(hasLines);
    actUploadJson_->setEnabled(hasImg);
    actSaveProjectFile_->setEnabled(hasImg);
    actPasteLayout_->setEnabled(hasImg);
    syncExportActions();
    // IMAGE cluster empty state (browser #load-image-btn ↔ #image-actions): one labelled
    // Open button with no image, the per-image icon row once there is one. The BUTTONS are
    // toggled, never the actions — those also back menu entries, which must stay listed.
    // The swap is HALF sand (user decision, browser controlState.js parity): the
    // LEAVING side goes at once — no dust-out, no collapse — and only the ARRIVING
    // side slides its slot open under gathering motes; an unchanged state costs nothing.
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
    // Compare view needs an image to compare against (parity with the browser gating).
    if (compareCombo_) compareCombo_->setEnabled(hasImg);
    if (actCycleCompare_) actCycleCompare_->setEnabled(hasImg);
    if (compareGroup_) compareGroup_->setEnabled(hasImg);
    // Description · Keywords · Links gate on a SAVED project — in updateProjectTitle below.
    // "Open in…" mirrors the browser's #open-in-btn gating: hidden entirely when no
    // target is available (no browser URL, and no Telegram bot / not a server project),
    // otherwise enabled only with an image loaded.
    if (actOpenIn_) {
      const bool serverProj = !remoteSession_->link().address.isEmpty() && !remoteSession_->link().id.isEmpty();
      const bool browserAvail = !settings_.browserBaseUrl.trimmed().isEmpty();
      const bool telegramAvail = !settings_.telegramBotUsername.trimmed().isEmpty() && serverProj;
      const bool anyAvail = browserAvail || telegramAvail;
      actOpenIn_->setVisible(anyAvail);
      actOpenIn_->setEnabled(hasImg && anyAvail);
    }
    // Clear (remove) current project — mirrors the browser's updateButtons() gating
    // (clearBtn.style.display = remoteLink ? 'none' : ''): hidden whenever the current
    // session is server-linked (those are removed only from the projects dialog),
    // shown for local/temporary editors.
    if (actClearProject_) {
      actClearProject_->setVisible(remoteSession_->link().address.isEmpty());
      // No image ⇒ nothing to clear (browser: setDisabled('clear-storage', !hasImage)).
      actClearProject_->setEnabled(canvas_->hasImage());
    }
    updateProjectTitle();   // keep the window title + toolbar name field in sync
    // Rename follows the name field itself: only a project that CAN be renamed offers it
    // (the field is disabled for no project / incognito), so the menu entry and the ✎ agree.
    if (actRenameProject_) actRenameProject_->setEnabled(nameBar_.field && nameBar_.field->isEnabled());
  }

}  // namespace stencil::gui
