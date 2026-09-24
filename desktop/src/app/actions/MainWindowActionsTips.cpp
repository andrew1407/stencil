// MainWindow's rebind map and action tooltips. Part of the buildActions() phase chain.
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasTooltip.hpp"
#include "IncognitoOverlay.hpp"
#include "Notifications.hpp"
#include "../../support/share/shareImage.hpp"   // isShareSheetAvailable — no Share tooltip on Linux
#include <QAction>

namespace stencil::gui {

  void MainWindow::mapHotkeyActions() {
    // Only ids present in hotkeysConfig.json are rebindable.
    hotkeyActions["rotateImageLeft"] = actRotateLeft;
    hotkeyActions["rotateImageRight"] = actRotateRight;
    hotkeyActions["startDraw"] = actStartDraw;
    hotkeyActions["stopDraw"] = actStopDraw;
    hotkeyActions["clearAllLines"] = actClearAll;
    hotkeyActions["deleteLine"] = actDeleteLine;
    hotkeyActions["deletePoint"] = actDeletePoint;
    hotkeyActions["togglePoints"] = actShowPoints;
    hotkeyActions["toggleLines"] = actShowLines;
    hotkeyActions["togglePointsList"] = actPanel;
    hotkeyActions["toggleControls"] = actToolbars;
    hotkeyActions["fullscreen"] = actFullscreen;
    hotkeyActions["openVisuals"] = actSettings;
    hotkeyActions["openHotkeys"] = actShortcuts;
    hotkeyActions["openAssistantSettings"] = actAssistantSettings;
    hotkeyActions["contextMenu"] = actContextMenu;
    hotkeyActions["resetZoom"] = actFit;
    hotkeyActions["zoomIn"] = actZoomIn;
    hotkeyActions["zoomOut"] = actZoomOut;
    hotkeyActions["undo"] = actUndo;
    hotkeyActions["redo"] = actRedo;
    hotkeyActions["copyImage"] = actCopyImage;
    hotkeyActions["copyImageOriginal"] = actCopyImageOriginal;
    hotkeyActions["copyImageTint"] = actCopyImageTint;
    hotkeyActions["copyLayout"] = actCopyLayout;
    hotkeyActions["paste"] = actPasteImage;
    // Defaults from the shared hotkeysConfig.json, so a rebind re-applies live and the Shortcuts dialog lists them.
    hotkeyActions["cropImage"] = actCrop;
    hotkeyActions["saveImage"] = actSaveImage;
    hotkeyActions["saveImageOriginal"] = actSaveImageOriginal;
    hotkeyActions["saveImageTint"] = actSaveImageTint;
    hotkeyActions["shareImage"] = actShareImage;
    hotkeyActions["downloadJson"] = actDownloadJson;
    hotkeyActions["uploadJson"] = actUploadJson;
    hotkeyActions["saveProject"] = actSaveProjectFile;
    hotkeyActions["openProject"] = actOpenProjectFile;
    hotkeyActions["openServers"] = actConnect;
    hotkeyActions["openLinks"] = actLinks;
    hotkeyActions["openDescription"] = actDescription;
    hotkeyActions["openKeywords"] = actKeywords;
    hotkeyActions["toggleIncognito"] = actIncognito;
    hotkeyActions["loadImage"] = actOpen;
    hotkeyActions["openAnotherImage"] = actOpenAnother;
    hotkeyActions["openProjects"] = actProjects;
    hotkeyActions["clearProject"] = actClearProject;
    hotkeyActions["renameProject"] = actRenameProject;
    hotkeyActions["toggleTheme"] = actTheme;
    hotkeyActions["openHelp"] = actInfo;
    hotkeyActions["toggleLiveSync"] = actStencilLiveSync;
    hotkeyActions["deleteProject"] = actDeleteProjectFile;
  }

  void MainWindow::setActionTooltips() {
    // The browser's words, verbatim (toolbar.js data-title); menu LABELS stay untouched.
    setActionTip(actOpen, "Open an image — local file, URL, or new blank");
    setActionTip(actOpenAnother, "Open another image — local file, URL, or new blank");
    setActionTip(actSaveImage, "Download image · Right-click for download options");
    setActionTip(actCopyImage, "Copy image to clipboard · Right-click for copy options");
    setActionTip(actSaveImageSplit, "Download the split compare view");
    setActionTip(actCopyImageSplit, "Copy the split compare view");
    setActionTip(actSaveImageCurrentRow, "Download image · Right-click for download options");
    setActionTip(actCopyImageCurrentRow, "Copy image to clipboard · Right-click for copy options");
    setActionTip(actSaveImageOriginal, "Download the original image — no tint, no lines/points");
    setActionTip(actSaveImageTint, "Download the filtered image — no lines/points");
    setActionTip(actCopyImageOriginal, "Copy the original image — no tint, no lines/points");
    setActionTip(actCopyImageTint, "Copy the filtered image — no lines/points");
    setActionTip(actShareImage, "Share image");
    setActionTip(actOpenIn, "Open in another app");
    setActionTip(actProjects, "Projects");
    // The browser's twin adds "(Shift+click: without theme)"; Save has no such modifier here.
    setActionTip(actSaveProjectFile, "Save Project (.stencil) — image + layout + settings in one file");
    setActionTip(actOpenProjectFile, "Open Project (.stencil)");
    setActionTip(actConnect, "Servers — connect to share & co-edit projects");
    setActionTip(actLinks, "Source & resource links for the current image");
    setActionTip(actDescription, "Project description");
    setActionTip(actKeywords, "Project keywords");
    setActionTip(actChat, "AI assistant — chat to edit the image");
    setActionTip(actCrop, "Crop image");
    setActionTip(actRotateLeft, "Rotate image left");
    setActionTip(actRotateRight, "Rotate image right");
    setActionTip(actFit, "Fit to window");
    setActionTip(actZoomIn, "Zoom in");
    setActionTip(actZoomOut, "Zoom out");
    setActionTip(actDownloadJson, "Download Layout JSON");
    setActionTip(actUploadJson, "Upload Layout JSON");
    setActionTip(actScript, "Stencil script (.stc) — write and run a script over this project");
    setActionTip(actCopyLayout, "Copy full Layout JSON (lines + all applied edits)");
    setActionTip(actTheme, "Toggle dark / light theme");
    setActionTip(actInfo, "Controls & shortcuts help");
    // The "— reason" line (toolbar.js data-disabled-reason) joins the tooltip while disabled.
    const auto why = [](QAction* a, const char* reason) { setTipReason(a, reason); };
    why(actSaveImage, "Load an image to download it");
    why(actSaveImageCurrentRow, "Load an image to download it");
    why(actCopyImage, "Load an image to copy it");
    why(actCopyImageCurrentRow, "Load an image to copy it");
    why(actSaveProjectFile, "Open an image first");
    why(actDeleteProjectFile, "Open or save a .stencil file first");
    why(actStencilLiveSync, "Open or save a .stencil file first");
    why(actDescription, "Save the project first to add a description");
    why(actKeywords, "Save the project first to add keywords");
    why(actLinks, "Save the project first to add links");
    why(actCrop, "Load an image to crop");
    why(actRotateLeft, "Load an image to rotate");
    why(actRotateRight, "Load an image to rotate");
    why(actUndo, "Nothing to undo");
    why(actRedo, "Nothing to redo");
    why(actStartDraw, "Load an image to start drawing");
    why(actClearAll, "No lines to clear");
    why(actZoomIn, "Load an image to zoom");
    why(actZoomOut, "Load an image to zoom");
    why(actFit, "Load an image to zoom");
    why(actDownloadJson, "Draw at least one line to export");
    why(actCopyLayout, "Draw at least one line to copy");
    why(actUploadJson, "Load an image first");
    why(actClearProject, "Open an image first — nothing to remove");

    connect(actInfo, &QAction::triggered, this, &MainWindow::openInfo);
    connect(actIncognito, &QAction::toggled, this, [this](bool on) {
      incognito = on;
      incognitoOverlay->setActive(on);
      notify->info(on ? "Incognito mode — this editor won't be saved"
                       : "Incognito off");
      updateProjectTitle();
    });
    connect(actTooltip, &QAction::toggled, this, [this](bool on) {
      settings.tooltipEnabled = on;
      if (!on) tooltip->hide();
      persistSettings();
    });
    connect(actQuit, &QAction::triggered, this, &QWidget::close);
  }

}  // namespace stencil::gui
