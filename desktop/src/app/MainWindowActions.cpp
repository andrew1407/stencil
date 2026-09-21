#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "DataExportController.hpp"
#include "IncognitoOverlay.hpp"
#include "modalReveal.hpp"
#include "Notifications.hpp"
#include "numericInput.hpp"
#include "exportPreview.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"
#include "../support/modalChrome.hpp"   // confirmModal — the browser-styled question
#include "../support/shareImage.hpp"    // isShareSheetAvailable — no Share button on Linux

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QActionGroup>
#include <QButtonGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QMenu>
#include <QMouseEvent>
#include <QGuiApplication>
#include <functional>

// MainWindow's action set: newAction() and buildActions()'s phase chain; the other phases are the MainWindowActions*.cpp siblings.

namespace stencil::gui {

  // WindowShortcut scope, so Backspace/Esc stay usable inside a dialog.
  QAction* MainWindow::newAction(const QString& text, const QString& seq) {
    auto* a = new QAction(text, this);
    if (!seq.isEmpty()) a->setShortcut(QKeySequence(seq));
    setActionTip(a, text);
    addAction(a);
    bindRevealAnchor(a);
    return a;
  }

  // The menus and toolbar iterate the actions in creation order — re-cut freely, reorder nothing.
  void MainWindow::buildActions() {
    createCoreActions();
    createDataActions();
    wireActionHandlers();
    mapHotkeyActions();
    setActionTooltips();
  }

  void MainWindow::createCoreActions() {
    // One Open dialog for local file, URL and blank canvas (browser: #load-image-btn and #open-image-btn share it).
    actOpen = newAction("Open Image…", hotkey("loadImage", "Ctrl+O"));
    setActionTip(actOpen,
        "Open an image — a local file, a web URL, or a new blank canvas");
    // Same dialog as actOpen, its own action for a distinct icon/shortcut (browser parity: #open-image-btn).
    actOpenAnother = newAction("Open Another Image…", hotkey("openAnotherImage", "Ctrl+Shift+O"));
    setActionTip(actOpenAnother,
        "Open another image — local file, URL, or new blank");
    // New Line has no hotkeysConfig.json entry, so its default is literal.
    actCrop = newAction("Crop Image…", hotkey("cropImage", "Ctrl+Shift+X"));
    setActionTip(actCrop,
        "Crop the image — pick the page-shaped region to show on the canvas");
    // Non-destructive 90° rotation; the crop window and lines follow the picture.
    actRotateLeft = newAction("Rotate Left", hotkey("rotateImageLeft", "Alt+R"));
    setActionTip(actRotateLeft, "Rotate the image left (counter-clockwise)");
    actRotateRight = newAction("Rotate Right", hotkey("rotateImageRight", "Alt+Shift+R"));
    setActionTip(actRotateRight, "Rotate the image right (clockwise)");
    actCycleFilter = newAction("Cycle Image Filter", hotkey("cycleFilter", "Alt+B"));
    setActionTip(actCycleFilter,
        "Cycle the image filter (none → B&W → sepia → invert → contour → tint)");
    // Compare view: cycle none → original → vertical split → horizontal split; hold Alt+Shift+O to peek.
    actCycleCompare = newAction("Cycle Compare View", hotkey("cycleCompare", "Alt+O"));
    setActionTip(actCycleCompare,
        "Cycle the compare view (none → original → vertical split → horizontal split); "
        "hold Alt+Shift+O to peek at the original");
    // hotkeysConfig startDraw=Alt+A, stopDraw=Alt+S; actNewLine loses its shortcut to Stop.
    actStartDraw = newAction("Start Drawing", hotkey("startDraw", "Alt+A"));
    actStopDraw = newAction("Stop Drawing", hotkey("stopDraw", "Alt+S"));
    // Short labels for the toolbar button (iconText()); the menu keeps the full text.
    actStartDraw->setIconText("Start");
    actStopDraw->setIconText("Stop");
    actNewLine = newAction("New Line", "Alt+N");
    actUndo = newAction("Undo", hotkey("undo", "Ctrl+Z"));
    actRedo = newAction("Redo", hotkey("redo", "Ctrl+Shift+Z"));
    actDeleteLast = newAction("Delete Last Point", "Backspace");
    // hotkeysConfig deleteLine / deletePoint; on macOS Delete→Backspace so ⌥⌫ works.
    actDeleteLine = newAction("Delete Selected Line (Point if focused)",
                        platformizeSeq(hotkey("deleteLine", "Alt+Delete")));
    actDeletePoint = newAction("Delete Selected Point",
                         platformizeSeq(hotkey("deletePoint", "Alt+Shift+Delete")));
    actClearAll = newAction("Clear All Lines", hotkey("clearAllLines", "Alt+W"));
    actDeselect = newAction("Deselect", "Esc");
    actZoomIn = newAction("Zoom In", hotkey("zoomIn", "Alt+Up"));
    actZoomOut = newAction("Zoom Out", hotkey("zoomOut", "Alt+Down"));
    actFit = newAction("Fit to Window", hotkey("resetZoom", "Alt+0"));
    actShowPoints = newAction("Show Points", hotkey("togglePoints", "Alt+P"));
    actShowLines = newAction("Show Lines", hotkey("toggleLines", "Alt+L"));
    actTheme = newAction("Dark Theme", hotkey("toggleTheme", "Ctrl+D"));
    actPanel = newAction("Selection Panel", hotkey("togglePointsList", "Alt+X"));
    actToolbars = newAction("Toolbars", hotkey("toggleControls", "Alt+C"));  // show/hide the top toolbars
    actFullscreen = newAction("Enter Fullscreen", hotkey("fullscreen", "Alt+F"));
    actSettings = newAction("Visuals && Settings…", hotkey("openVisuals", "Alt+V"));
    setActionTip(actSettings, "Default visuals & highlight styles");   // the browser #visuals-btn title
    actProjects = newAction("Projects…", hotkey("openProjects", "Ctrl+Shift+P"));
    actConnect = newAction("Servers…", hotkey("openServers", "Ctrl+Shift+K"));
    setActionTip(actConnect,
        "Connect to collaboration servers — shared projects appear with a golden outline");
    actLinks = newAction("Image Links…", hotkey("openLinks", "Ctrl+Shift+L"));
    actDescription = newAction("Project Description…", hotkey("openDescription", "Alt+Shift+D"));
    actKeywords = newAction("Project Keywords…", hotkey("openKeywords", "Alt+Shift+K"));
    actOpenIn = newAction("Open In…", hotkey("openIn", "Ctrl+Shift+E"));
    setActionTip(actOpenIn,
        "Open the current project in the browser app or the Telegram bot");
    actChat = newAction("AI Assistant", hotkey("toggleChat", "Alt+G"));
    // Named to disambiguate from the dock's own toggleViewAction (same text) for the GUI e2e lookup.
    actChat->setObjectName("actChat");
    actChat->setCheckable(true);
    setActionTip(actChat, "Chat with the AI assistant — plan edits, variants, and layouts");
    // A popover gesture (armed pop.anchor) always means "show it compact here", so an unchecking trigger is re-checked.
    connect(actChat, &QAction::toggled, this, [this](bool on) {
      QWidget* anchor = pop.anchor.data();
      pop.anchor.clear();
      if (anchor && chatDock) {
        if (!on) {
          QSignalBlocker b(actChat);
          actChat->setChecked(true);
        }
        openChatCompact(anchor);
        return;
      }
      setChatShown(on, /*animate=*/true);
    });
    actNewProject = newAction("New Project", "Ctrl+Shift+N");
    actSaveProject = newAction("Save to Project", "Ctrl+Shift+S");
    // Browser's #clear-storage danger button; hidden for server projects (refreshActions).
    actClearProject = newAction("Clear Project", hotkey("clearProject", "Ctrl+Alt+R"));
    setActionTip(actClearProject, "Remove current project");
    // The same inline edit the ✎ beside the toolbar name opens.
    actRenameProject = newAction("Rename Project", hotkey("renameProject", "Ctrl+Alt+N"));
    connect(actRenameProject, &QAction::triggered, this, &MainWindow::enterNameEdit);
    actSaveSession = newAction("Save Session", "Ctrl+S");
    actInfo = newAction("Controls && Shortcuts Info", hotkey("openHelp", "F1"));
    actIncognito = newAction("Incognito", hotkey("toggleIncognito", "Alt+I"));
    actTooltip = newAction("Show Tooltips", QString());   // browser label parity (was "Hover Tooltip")
    actTooltip->setCheckable(true);
    // Two-way synced with the toolbar allowFormulas checkbox.
    actAllowFormulas = newAction("Allow Formulas", QString());
    actAllowFormulas->setCheckable(true);
    actQuit = newAction("Quit", "Ctrl+Q");

  }


}  // namespace stencil::gui

