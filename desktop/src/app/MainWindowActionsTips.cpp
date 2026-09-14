// MainWindow's rebind map and action tooltips. Part of the buildActions() phase chain.
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasTooltip.hpp"
#include "IncognitoOverlay.hpp"
#include "Notifications.hpp"
#include "../support/shareImage.hpp"   // isShareSheetAvailable — no Share tooltip on Linux
#include <QAction>

namespace stencil::gui {

  void MainWindow::mapHotkeyActions() {
    // Only ids present in hotkeysConfig.json are rebindable.
    hotkeyActions_["rotateImageLeft"] = actRotateLeft_;
    hotkeyActions_["rotateImageRight"] = actRotateRight_;
    hotkeyActions_["startDraw"] = actStartDraw_;
    hotkeyActions_["stopDraw"] = actStopDraw_;
    hotkeyActions_["clearAllLines"] = actClearAll_;
    hotkeyActions_["deleteLine"] = actDeleteLine_;
    hotkeyActions_["deletePoint"] = actDeletePoint_;
    hotkeyActions_["togglePoints"] = actShowPoints_;
    hotkeyActions_["toggleLines"] = actShowLines_;
    hotkeyActions_["togglePointsList"] = actPanel_;
    hotkeyActions_["toggleControls"] = actToolbars_;
    hotkeyActions_["fullscreen"] = actFullscreen_;
    hotkeyActions_["openVisuals"] = actSettings_;
    hotkeyActions_["openHotkeys"] = actShortcuts_;
    hotkeyActions_["openAssistantSettings"] = actAssistantSettings_;
    hotkeyActions_["contextMenu"] = actContextMenu_;
    hotkeyActions_["resetZoom"] = actFit_;
    hotkeyActions_["zoomIn"] = actZoomIn_;
    hotkeyActions_["zoomOut"] = actZoomOut_;
    hotkeyActions_["undo"] = actUndo_;
    hotkeyActions_["redo"] = actRedo_;
    hotkeyActions_["copyImage"] = actCopyImage_;
    hotkeyActions_["copyImageOriginal"] = actCopyImageOriginal_;
    hotkeyActions_["copyImageTint"] = actCopyImageTint_;
    hotkeyActions_["copyLayout"] = actCopyLayout_;
    hotkeyActions_["paste"] = actPasteImage_;
    // Defaults from the shared hotkeysConfig.json, so a rebind re-applies live and the Shortcuts dialog lists them.
    hotkeyActions_["cropImage"] = actCrop_;
    hotkeyActions_["saveImage"] = actSaveImage_;
    hotkeyActions_["saveImageOriginal"] = actSaveImageOriginal_;
    hotkeyActions_["saveImageTint"] = actSaveImageTint_;
    hotkeyActions_["shareImage"] = actShareImage_;
    hotkeyActions_["downloadJson"] = actDownloadJson_;
    hotkeyActions_["uploadJson"] = actUploadJson_;
    hotkeyActions_["saveProject"] = actSaveProjectFile_;
    hotkeyActions_["openProject"] = actOpenProjectFile_;
    hotkeyActions_["openServers"] = actConnect_;
    hotkeyActions_["openLinks"] = actLinks_;
    hotkeyActions_["openDescription"] = actDescription_;
    hotkeyActions_["openKeywords"] = actKeywords_;
    hotkeyActions_["toggleIncognito"] = actIncognito_;
    hotkeyActions_["loadImage"] = actOpen_;
    hotkeyActions_["openAnotherImage"] = actOpenAnother_;
    hotkeyActions_["openProjects"] = actProjects_;
    hotkeyActions_["clearProject"] = actClearProject_;
    hotkeyActions_["renameProject"] = actRenameProject_;
    hotkeyActions_["toggleTheme"] = actTheme_;
    hotkeyActions_["openHelp"] = actInfo_;
    hotkeyActions_["toggleLiveSync"] = actStencilLiveSync_;
    hotkeyActions_["deleteProject"] = actDeleteProjectFile_;
  }

  void MainWindow::setActionTooltips() {
    // The browser's words, verbatim (toolbar.js data-title); menu LABELS stay untouched.
    setActionTip(actOpen_, "Open an image — local file, URL, or new blank");
    setActionTip(actOpenAnother_, "Open another image — local file, URL, or new blank");
    setActionTip(actSaveImage_, "Download image · Right-click for download options");
    setActionTip(actCopyImage_, "Copy image to clipboard · Right-click for copy options");
    setActionTip(actSaveImageSplit_, "Download the split compare view");
    setActionTip(actCopyImageSplit_, "Copy the split compare view");
    setActionTip(actSaveImageCurrentRow_, "Download image · Right-click for download options");
    setActionTip(actCopyImageCurrentRow_, "Copy image to clipboard · Right-click for copy options");
    setActionTip(actSaveImageOriginal_, "Download the original image — no tint, no lines/points");
    setActionTip(actSaveImageTint_, "Download the filtered image — no lines/points");
    setActionTip(actCopyImageOriginal_, "Copy the original image — no tint, no lines/points");
    setActionTip(actCopyImageTint_, "Copy the filtered image — no lines/points");
    setActionTip(actShareImage_, "Share image");
    setActionTip(actOpenIn_, "Open in another app");
    setActionTip(actProjects_, "Projects");
    // The browser's twin adds "(Shift+click: without theme)"; Save has no such modifier here.
    setActionTip(actSaveProjectFile_, "Save Project (.stencil) — image + layout + settings in one file");
    setActionTip(actOpenProjectFile_, "Open Project (.stencil)");
    setActionTip(actConnect_, "Servers — connect to share & co-edit projects");
    setActionTip(actLinks_, "Source & resource links for the current image");
    setActionTip(actDescription_, "Project description");
    setActionTip(actKeywords_, "Project keywords");
    setActionTip(actChat_, "AI assistant — chat to edit the image");
    setActionTip(actCrop_, "Crop image");
    setActionTip(actRotateLeft_, "Rotate image left");
    setActionTip(actRotateRight_, "Rotate image right");
    setActionTip(actFit_, "Fit to window");
    setActionTip(actDownloadJson_, "Download Layout JSON");
    setActionTip(actUploadJson_, "Upload Layout JSON");
    setActionTip(actScript_, "Stencil script (.stc) — write and run a script over this project");
    setActionTip(actCopyLayout_, "Copy full Layout JSON (lines + all applied edits)");
    setActionTip(actTheme_, "Toggle dark / light theme");
    setActionTip(actInfo_, "Controls & shortcuts help");
    // The "— reason" line (toolbar.js data-disabled-reason) joins the tooltip while disabled.
    const auto why = [](QAction* a, const char* reason) { setTipReason(a, reason); };
    why(actSaveImage_, "Load an image to download it");
    why(actSaveImageCurrentRow_, "Load an image to download it");
    why(actCopyImage_, "Load an image to copy it");
    why(actCopyImageCurrentRow_, "Load an image to copy it");
    why(actSaveProjectFile_, "Open an image first");
    why(actDeleteProjectFile_, "Open or save a .stencil file first");
    why(actStencilLiveSync_, "Open or save a .stencil file first");
    why(actDescription_, "Save the project first to add a description");
    why(actKeywords_, "Save the project first to add keywords");
    why(actLinks_, "Save the project first to add links");
    why(actCrop_, "Load an image to crop");
    why(actRotateLeft_, "Load an image to rotate");
    why(actRotateRight_, "Load an image to rotate");
    why(actUndo_, "Nothing to undo");
    why(actRedo_, "Nothing to redo");
    why(actStartDraw_, "Load an image to start drawing");
    why(actClearAll_, "No lines to clear");
    why(actZoomIn_, "Load an image to zoom");
    why(actZoomOut_, "Load an image to zoom");
    why(actFit_, "Load an image to zoom");
    why(actDownloadJson_, "Draw at least one line to export");
    why(actCopyLayout_, "Draw at least one line to copy");
    why(actUploadJson_, "Load an image first");
    why(actClearProject_, "Open an image first — nothing to remove");

    connect(actInfo_, &QAction::triggered, this, &MainWindow::openInfo);
    connect(actIncognito_, &QAction::toggled, this, [this](bool on) {
      incognito_ = on;
      incognitoOverlay_->setActive(on);
      notify_->info(on ? "Incognito mode — this editor won't be saved"
                       : "Incognito off");
      updateProjectTitle();
    });
    connect(actTooltip_, &QAction::toggled, this, [this](bool on) {
      settings_.tooltipEnabled = on;
      if (!on) tooltip_->hide();
      persistSettings();
    });
    connect(actQuit_, &QAction::triggered, this, &QWidget::close);
  }

}  // namespace stencil::gui
