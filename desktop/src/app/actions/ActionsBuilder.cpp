#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include "ActionsBuilder.hpp"
#include "mainWindowHelpers.hpp"

// MainWindow's action set: newAction() and buildActions()'s phase chain; the other phases are the MainWindowActions*.cpp siblings.

namespace stencil::gui {

  // WindowShortcut scope, so Backspace/Esc stay usable inside a dialog.
  QAction* ActionsBuilder::newAction(const QString& text, const QString& seq) {
    auto* a = new QAction(text, &w);
    if (!seq.isEmpty()) a->setShortcut(QKeySequence(qtKeySeq(seq)));
    w.setActionTip(a, text);
    w.addAction(a);
    bindRevealAnchor(a);
    return a;
  }

  // The menus and toolbar iterate the actions in creation order — re-cut freely, reorder nothing.
  void ActionsBuilder::buildActions() {
    createCoreActions();
    createDataActions();
    wireActionHandlers();
    mapHotkeyActions();
    setActionTooltips();
  }

  void ActionsBuilder::createCoreActions() {
    // One Open dialog for local file, URL and blank canvas (browser: #load-image-btn and #open-image-btn share it).
    w.acts.open = newAction("Open Image…", w.keys.value("loadImage", "Ctrl+O"));
    w.setActionTip(w.acts.open,
        "Open an image — a local file, a web URL, or a new blank canvas");
    // Same dialog as acts.open, its own action for a distinct icon/shortcut (browser parity: #open-image-btn).
    w.acts.openAnother = newAction("Open Another Image…", w.keys.value("openAnotherImage", "Ctrl+Shift+O"));
    w.setActionTip(w.acts.openAnother,
        "Open another image — local file, URL, or new blank");
    // New Line has no hotkeysConfig.json entry, so its default is literal.
    w.acts.crop = newAction("Crop Image…", w.keys.value("cropImage", "Ctrl+Shift+X"));
    w.setActionTip(w.acts.crop,
        "Crop the image — pick the page-shaped region to show on the canvas");
    // Non-destructive 90° rotation; the crop window and lines follow the picture.
    w.acts.rotateLeft = newAction("Rotate Left", w.keys.value("rotateImageLeft", "Alt+R"));
    w.setActionTip(w.acts.rotateLeft, "Rotate the image left (counter-clockwise)");
    w.acts.rotateRight = newAction("Rotate Right", w.keys.value("rotateImageRight", "Alt+Shift+R"));
    w.setActionTip(w.acts.rotateRight, "Rotate the image right (clockwise)");
    w.acts.cycleFilter = newAction("Cycle Image Filter", w.keys.value("cycleFilter", "Alt+B"));
    w.setActionTip(w.acts.cycleFilter,
        "Cycle the image filter (none → B&W → sepia → invert → contour → tint)");
    // Compare view: cycle none → original → vertical split → horizontal split; hold Alt+Shift+O to peek.
    w.acts.cycleCompare = newAction("Cycle Compare View", w.keys.value("cycleCompare", "Alt+O"));
    w.setActionTip(w.acts.cycleCompare,
        "Cycle the compare view (none → original → vertical split → horizontal split); "
        "hold Alt+Shift+O to peek at the original");
    // hotkeysConfig startDraw=Alt+A, stopDraw=Alt+S; acts.newLine loses its shortcut to Stop.
    w.acts.startDraw = newAction("Start Drawing", w.keys.value("startDraw", "Alt+A"));
    w.acts.stopDraw = newAction("Stop Drawing", w.keys.value("stopDraw", "Alt+S"));
    // Short labels for the toolbar button (iconText()); the menu keeps the full text.
    w.acts.startDraw->setIconText("Start");
    w.acts.stopDraw->setIconText("Stop");
    w.acts.newLine = newAction("New Line", "Alt+N");
    w.acts.undo = newAction("Undo", w.keys.value("undo", "Ctrl+Z"));
    w.acts.redo = newAction("Redo", w.keys.value("redo", "Ctrl+Shift+Z"));
    w.acts.deleteLast = newAction("Delete Last Point", "Backspace");
    // hotkeysConfig deleteLine / deletePoint; on macOS Delete→Backspace so ⌥⌫ works.
    w.acts.deleteLine = newAction("Delete Selected Line (Point if focused)",
                        platformizeSeq(w.keys.value("deleteLine", "Alt+Delete")));
    w.acts.deletePoint = newAction("Delete Selected Point",
                         platformizeSeq(w.keys.value("deletePoint", "Alt+Shift+Delete")));
    w.acts.clearAll = newAction("Clear All Lines", w.keys.value("clearAllLines", "Alt+W"));
    w.acts.deselect = newAction("Deselect", "Esc");
    w.acts.zoomIn = newAction("Zoom In", w.keys.value("zoomIn", "Alt+Up"));
    w.acts.zoomOut = newAction("Zoom Out", w.keys.value("zoomOut", "Alt+Down"));
    w.acts.fit = newAction("Fit to Window", w.keys.value("resetZoom", "Alt+0"));
    w.acts.showPoints = newAction("Show Points", w.keys.value("togglePoints", "Alt+P"));
    w.acts.showLines = newAction("Show Lines", w.keys.value("toggleLines", "Alt+L"));
    w.acts.theme = newAction("Dark Theme", w.keys.value("toggleTheme", "Ctrl+D"));
    w.acts.panel = newAction("Selection Panel", w.keys.value("togglePointsList", "Alt+X"));
    w.acts.toolbars = newAction("Toolbars", w.keys.value("toggleControls", "Alt+C"));  // show/hide the top toolbars
    w.acts.fullscreen = newAction("Enter Fullscreen", w.keys.value("fullscreen", "Alt+F"));
    w.acts.settings = newAction("Visuals && Settings…", w.keys.value("openVisuals", "Alt+V"));
    w.setActionTip(w.acts.settings, "Default visuals & highlight styles");   // the browser #visuals-btn title
    w.acts.projects = newAction("Projects…", w.keys.value("openProjects", "Ctrl+Shift+P"));
    w.acts.connect = newAction("Servers…", w.keys.value("openServers", "Ctrl+Shift+K"));
    w.setActionTip(w.acts.connect,
        "Connect to collaboration servers — shared projects appear with a golden outline");
    w.acts.links = newAction("Image Links…", w.keys.value("openLinks", "Ctrl+Shift+L"));
    w.acts.description = newAction("Project Description…", w.keys.value("openDescription", "Alt+Shift+D"));
    w.acts.keywords = newAction("Project Keywords…", w.keys.value("openKeywords", "Alt+Shift+K"));
    w.acts.openIn = newAction("Open In…", w.keys.value("openIn", "Ctrl+Shift+E"));
    w.setActionTip(w.acts.openIn,
        "Open the current project in the browser app or the Telegram bot");
    w.acts.copyProject = newAction("Make a Copy…", QString());
    w.acts.chat = newAction("AI Assistant", w.keys.value("toggleChat", "Alt+G"));
    // Named to disambiguate from the dock's own toggleViewAction (same text) for the GUI e2e lookup.
    w.acts.chat->setObjectName("actChat");
    w.acts.chat->setCheckable(true);
    w.setActionTip(w.acts.chat, "Chat with the AI assistant — plan edits, variants, and layouts");
    // A popover gesture (armed pop.anchor) always means "show it compact here", so an unchecking trigger is re-checked.
    QObject::connect(w.acts.chat, &QAction::toggled, &w, [this](bool on) {
      QWidget* anchor = w.pop.anchor.data();
      w.pop.anchor.clear();
      if (anchor && w.chatDock) {
        if (!on) {
          QSignalBlocker b(w.acts.chat);
          w.acts.chat->setChecked(true);
        }
        w.openChatCompact(anchor);
        return;
      }
      w.parts.dockChrome.setChatShown(on, /*animate=*/true);
    });
    w.acts.newProject = newAction("New Project", "Ctrl+Shift+N");
    w.acts.saveProject = newAction("Save to Project", "Ctrl+Shift+S");
    // Browser's #clear-storage danger button; hidden for server projects (refreshActions).
    w.acts.clearProject = newAction("Clear Project", w.keys.value("clearProject", "Ctrl+Alt+R"));
    w.setActionTip(w.acts.clearProject, "Remove current project");
    // The same inline edit the ✎ beside the toolbar name opens.
    w.acts.renameProject = newAction("Rename Project", w.keys.value("renameProject", "Ctrl+Alt+N"));
    QObject::connect(w.acts.renameProject, &QAction::triggered, &w, [this] { w.projectTitle->enterNameEdit(); });
    w.acts.saveSession = newAction("Save Session", "Ctrl+S");
    w.acts.info = newAction("Controls && Shortcuts Info", w.keys.value("openHelp", "F1"));
    w.acts.incognito = newAction("Incognito", w.keys.value("toggleIncognito", "Alt+I"));
    w.acts.tooltip = newAction("Show Tooltips", QString());   // browser label parity (was "Hover Tooltip")
    w.acts.tooltip->setCheckable(true);
    // Two-way synced with the toolbar allowFormulas checkbox.
    w.acts.allowFormulas = newAction("Allow Formulas", QString());
    w.acts.allowFormulas->setCheckable(true);
    w.acts.quit = newAction("Quit", "Ctrl+Q");

  }

}  // namespace stencil::gui

