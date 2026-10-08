// MainWindow's rebind map and action tooltips. Part of the buildActions() phase chain.
#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include "modalReveal.hpp"
#include "ActionsBuilder.hpp"
#include "CanvasTooltip.hpp"
#include "IncognitoOverlay.hpp"
#include "Notifications.hpp"
#include "tipContent.hpp"

namespace stencil::gui {

  void ActionsBuilder::mapHotkeyActions() {
    // Only ids present in hotkeysConfig.json are rebindable.
    w.keys.actions["rotateImageLeft"] = w.acts.rotateLeft;
    w.keys.actions["rotateImageRight"] = w.acts.rotateRight;
    w.keys.actions["flipImageHorizontal"] = w.acts.flipImage;
    w.keys.actions["cycleFilter"] = w.acts.cycleFilter;
    w.keys.actions["cycleFilterPrev"] = w.acts.cycleFilterPrev;
    w.keys.actions["startDraw"] = w.acts.startDraw;
    w.keys.actions["clearAllLines"] = w.acts.clearAll;
    w.keys.actions["deleteLine"] = w.acts.deleteLine;
    w.keys.actions["deletePoint"] = w.acts.deletePoint;
    w.keys.actions["togglePoints"] = w.acts.showPoints;
    w.keys.actions["toggleLines"] = w.acts.showLines;
    w.keys.actions["togglePointsList"] = w.acts.panel;
    w.keys.actions["toggleControls"] = w.acts.toolbars;
    w.keys.actions["fullscreen"] = w.acts.fullscreen;
    w.keys.actions["openVisuals"] = w.acts.settings;
    w.keys.actions["openHotkeys"] = w.acts.shortcuts;
    w.keys.actions["openAssistantSettings"] = w.acts.assistantSettings;
    w.keys.actions["contextMenu"] = w.acts.contextMenu;
    w.keys.actions["resetZoom"] = w.acts.fit;
    w.keys.actions["zoomIn"] = w.acts.zoomIn;
    w.keys.actions["zoomOut"] = w.acts.zoomOut;
    w.keys.actions["undo"] = w.acts.undo;
    w.keys.actions["redo"] = w.acts.redo;
    w.keys.actions["copyImage"] = w.acts.copyImage;
    w.keys.actions["copyImageOriginal"] = w.acts.copyImageOriginal;
    w.keys.actions["copyImageTint"] = w.acts.copyImageTint;
    w.keys.actions["copyLayout"] = w.acts.copyLayout;
    w.keys.actions["paste"] = w.acts.pasteImage;
    // Defaults from the shared hotkeysConfig.json, so a rebind re-applies live and the Shortcuts dialog lists them.
    w.keys.actions["cropImage"] = w.acts.crop;
    w.keys.actions["saveImage"] = w.acts.saveImage;
    w.keys.actions["saveImageOriginal"] = w.acts.saveImageOriginal;
    w.keys.actions["saveImageTint"] = w.acts.saveImageTint;
    w.keys.actions["shareImage"] = w.acts.shareImage;
    w.keys.actions["downloadJson"] = w.acts.downloadJson;
    w.keys.actions["uploadJson"] = w.acts.uploadJson;
    w.keys.actions["saveProject"] = w.acts.saveProjectFile;
    w.keys.actions["openProject"] = w.acts.openProjectFile;
    w.keys.actions["openServers"] = w.acts.connect;
    w.keys.actions["openLinks"] = w.acts.links;
    w.keys.actions["openDescription"] = w.acts.description;
    w.keys.actions["openKeywords"] = w.acts.keywords;
    w.keys.actions["toggleIncognito"] = w.acts.incognito;
    w.keys.actions["loadImage"] = w.acts.open;
    w.keys.actions["openAnotherImage"] = w.acts.openAnother;
    w.keys.actions["openProjects"] = w.acts.projects;
    w.keys.actions["clearProject"] = w.acts.clearProject;
    w.keys.actions["closeProject"] = w.acts.closeProject;
    w.keys.actions["renameProject"] = w.acts.renameProject;
    w.keys.actions["toggleTheme"] = w.acts.theme;
    w.keys.actions["openHelp"] = w.acts.info;
    w.keys.actions["toggleLiveSync"] = w.acts.stencilLiveSync;
    w.keys.actions["deleteProject"] = w.acts.deleteProjectFile;
  }

  void ActionsBuilder::setActionTooltips() {
    // The browser's words, verbatim (toolbar.js data-title); menu LABELS stay untouched.
    w.setActionTip(w.acts.open, "Open an image — local file, URL, or new blank");
    w.setActionTip(w.acts.openAnother, "Open another image — local file, URL, or new blank");
    w.setActionTip(w.acts.saveImage, "Download image · Right-click for download options");
    w.setActionTip(w.acts.copyImage, "Copy image to clipboard · Right-click for copy options");
    w.setActionTip(w.acts.saveImageSplit, "Download the split compare view");
    w.setActionTip(w.acts.copyImageSplit, "Copy the split compare view");
    w.setActionTip(w.acts.saveImageCurrentRow, "Download image · Right-click for download options");
    w.setActionTip(w.acts.copyImageCurrentRow, "Copy image to clipboard · Right-click for copy options");
    w.setActionTip(w.acts.saveImageOriginal, "Download the original image — no tint, no lines/points");
    w.setActionTip(w.acts.saveImageTint, "Download the filtered image — no lines/points");
    w.setActionTip(w.acts.copyImageOriginal, "Copy the original image — no tint, no lines/points");
    w.setActionTip(w.acts.copyImageTint, "Copy the filtered image — no lines/points");
    w.setActionTip(w.acts.shareImage, "Share image");
    w.setActionTip(w.acts.openIn, "Open in another app");
    w.setActionTip(w.acts.copyProject, "Make a copy — image only, image and layout, or the whole project");
    w.setActionTip(w.acts.projects, "Projects");
    // The browser's twin adds "(Shift+click: without theme)"; Save has no such modifier here.
    w.setActionTip(w.acts.saveProjectFile, "Save Project (.stencil) — image + layout + settings in one file");
    w.setActionTip(w.acts.openProjectFile, "Open Project (.stencil)");
    w.setActionTip(w.acts.connect, "Servers — connect to share & co-edit projects");
    w.setActionTip(w.acts.links, "Source & resource links for the current image");
    w.setActionTip(w.acts.description, "Project description");
    w.setActionTip(w.acts.keywords, "Project keywords");
    w.setActionTip(w.acts.chat, "AI assistant — chat to edit the image");
    w.setActionTip(w.acts.crop, "Crop image");
    w.setActionTip(w.acts.rotateLeft, "Rotate image left");
    w.setActionTip(w.acts.rotateRight, "Rotate image right");
    w.setActionTip(w.acts.flipImage, "Flip image horizontally");
    w.setActionTip(w.acts.fit, "Fit to window");
    w.setActionTip(w.acts.zoomIn, "Zoom in");
    w.setActionTip(w.acts.zoomOut, "Zoom out");
    w.setActionTip(w.acts.downloadJson, "Download Layout JSON");
    w.setActionTip(w.acts.uploadJson, "Upload Layout JSON");
    w.setActionTip(w.acts.script, "Stencil script (.stc) — write and run a script over this project");
    w.setActionTip(w.acts.copyLayout, "Copy full Layout JSON (lines + all applied edits)");
    w.setActionTip(w.acts.theme, "Toggle dark / light theme");
    w.setActionTip(w.acts.info, "Controls & shortcuts help");
    // The "— reason" line (toolbar.js data-disabled-reason) joins the tooltip while disabled.
    const auto why = [](QAction* a, const char* reason) { setTipReason(a, reason); };
    why(w.acts.saveImage, "Load an image to download it");
    why(w.acts.saveImageCurrentRow, "Load an image to download it");
    why(w.acts.copyImage, "Load an image to copy it");
    why(w.acts.copyImageCurrentRow, "Load an image to copy it");
    why(w.acts.saveProjectFile, "Open an image first");
    why(w.acts.deleteProjectFile, "Open or save a .stencil file first");
    why(w.acts.stencilLiveSync, "Open or save a .stencil file first");
    why(w.acts.description, "Save the project first to add a description");
    why(w.acts.keywords, "Save the project first to add keywords");
    why(w.acts.links, "Save the project first to add links");
    why(w.acts.crop, "Load an image to crop");
    why(w.acts.rotateLeft, "Load an image to rotate");
    why(w.acts.rotateRight, "Load an image to rotate");
    why(w.acts.flipImage, "Load an image to flip");
    why(w.acts.undo, "Nothing to undo");
    why(w.acts.redo, "Nothing to redo");
    why(w.acts.startDraw, "Load an image to start drawing");
    why(w.acts.clearAll, "No lines to clear");
    why(w.acts.zoomIn, "Load an image to zoom");
    why(w.acts.zoomOut, "Load an image to zoom");
    why(w.acts.fit, "Load an image to zoom");
    why(w.acts.downloadJson, "Draw at least one line to export");
    why(w.acts.copyLayout, "Draw at least one line to copy");
    why(w.acts.uploadJson, "Load an image first");
    why(w.acts.clearProject, "Open an image first — nothing to remove");

    QObject::connect(w.acts.info, &QAction::triggered, &w, [this] { w.parts.dialogs.openInfo(); });
    QObject::connect(w.acts.incognito, &QAction::toggled, &w, [this](bool on) {
      w.incognito = on;
      w.overlays.incognito->setActive(on);
      w.notify->info(on ? "Incognito mode — this editor won't be saved"
                       : "Incognito off");
      w.projectTitle->updateProjectTitle();
    });
    QObject::connect(w.acts.tooltip, &QAction::toggled, &w, [this](bool on) {
      w.settings.tooltipEnabled = on;
      if (!on) w.overlays.tooltip->hide();
      w.persistSettings();
    });
    QObject::connect(w.acts.quit, &QAction::triggered, &w, &QWidget::close);
  }

  // Every action records its own origin when it fires; clearing it for a button-less action stops a dialog flying out of the last icon used.
  void ActionsBuilder::bindRevealAnchors() {
    for (QAction* a : w.findChildren<QAction*>()) bindRevealAnchor(a);
  }

  // Bound at creation so it is the first triggered() slot; bound after the handlers it ran only once exec() had returned.
  void ActionsBuilder::bindRevealAnchor(QAction* a) {
    if (!a || a->property("revealBound").toBool()) return;
    a->setProperty("revealBound", true);
    QObject::connect(a, &QAction::triggered, &w, [this, a] {
      w.pop.dialogAnchor = w.buttonForAction(a);   // resolved at trigger time; buttons come later
      support::noteActionAnchor(w.pop.dialogAnchor);   // a confirm raised by its shortcut grows from it
      w.pop.dialogAnchorRect = (w.pop.menuRowAction == a) ? w.pop.menuRowRect : QRect();
      w.pop.dialogCloseRect = nullptr;   // the handler about to run names its own, or there is none
    });
  }
}  // namespace stencil::gui
