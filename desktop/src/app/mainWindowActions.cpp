#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "dataExportController.hpp"
#include "incognitoOverlay.hpp"
#include "modalReveal.hpp"
#include "notifications.hpp"
#include "numericInput.hpp"
#include "exportPreview.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"
#include "../support/modalChrome.hpp"   // confirmModal — the browser-styled question
#include "../support/shareImage.hpp"    // shareSheetAvailable — no Share button on Linux

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

// MainWindow's action set: newAction(), buildActions()'s phase chain, and the core group of
// QActions. The wiring, data, tooltip and context-menu phases are the mainWindowActions*.cpp
// and mainWindowContext*.cpp siblings; same class, definitions only.

namespace stencil::gui {

  // One action: its shortcut (portable form stored, native form shown), the tooltip that
  // carries that shortcut, the window registration that makes it fire, and the anchor its
  // dialog flies from. WindowShortcut scope, so Backspace/Esc stay usable inside a dialog.
  QAction* MainWindow::newAction(const QString& text, const QString& seq) {
    auto* a = new QAction(text, this);
    if (!seq.isEmpty()) a->setShortcut(QKeySequence(seq));
    setActionTip(a, text);
    addAction(a);
    bindRevealAnchor(a);
    return a;
  }

  // The phases, in call order. The menus and toolbar iterate the actions in creation order,
  // so re-cut these freely but reorder nothing.
  void MainWindow::buildActions() {
    createCoreActions();
    createDataActions();
    wireActionHandlers();
    mapHotkeyActions();
    setActionTooltips();
  }

  void MainWindow::createCoreActions() {
    // The unified Open dialog (local file, URL, or a new blank canvas), replacing the
    // former three-way split of Open Image / Open Another Image / New Blank Image —
    // mirroring the browser's one dialog behind both #load-image-btn and
    // #open-image-btn. actOpenAnother_ below opens the very same dialog, just from
    // the icon-only row button shown once an image is loaded.
    actOpen_ = newAction("Open Image…", hotkey("loadImage", "Ctrl+O"));
    setActionTip(actOpen_,
        "Open an image — a local file, a web URL, or a new blank canvas");
    // Same dialog as actOpen_, its own action so the toolbar can give it a distinct
    // icon/shortcut (browser parity: #open-image-btn vs #load-image-btn) — it's the
    // compact icon shown alongside Save/Copy/Share/Open-in once an image is loaded,
    // where actOpen_'s labelled button steps aside for the empty-state one.
    actOpenAnother_ = newAction("Open Another Image…", hotkey("openAnotherImage", "Ctrl+Shift+O"));
    setActionTip(actOpenAnother_,
        "Open another image — local file, URL, or new blank");
    // Emoji prefixes were removed from these labels now that every action carries
    // a themed line-art icon (styleActionIcons): the menu shows icon + clean text,
    // the icon-only toolbar shows the glyph with the label on its tooltip.
    // New Line has no entry in the shared hotkeysConfig.json registry, so its
    // (browser-coordinated) default is set literally rather than through hotkey().
    actCrop_ = newAction("Crop Image…", hotkey("cropImage", "Ctrl+Shift+X"));
    setActionTip(actCrop_,
        "Crop the image — pick the page-shaped region to show on the canvas");
    // Non-destructive 90° rotation (browser hotkeys rotateImageLeft=Alt+R,
    // rotateImageRight=Alt+Shift+R). The crop window and lines follow the picture.
    actRotateLeft_ = newAction("Rotate Left", hotkey("rotateImageLeft", "Alt+R"));
    setActionTip(actRotateLeft_, "Rotate the image left (counter-clockwise)");
    actRotateRight_ = newAction("Rotate Right", hotkey("rotateImageRight", "Alt+Shift+R"));
    setActionTip(actRotateRight_, "Rotate the image right (clockwise)");
    actCycleFilter_ = newAction("Cycle Image Filter", hotkey("cycleFilter", "Alt+B"));
    setActionTip(actCycleFilter_,
        "Cycle the image filter (none → B&W → sepia → invert → contour → tint)");
    // Compare view: cycle none → original → vertical split → horizontal split. The
    // toolbar combo + View submenu offer direct picks; hold Alt+Shift+O to peek.
    actCycleCompare_ = newAction("Cycle Compare View", hotkey("cycleCompare", "Alt+O"));
    setActionTip(actCycleCompare_,
        "Cycle the compare view (none → original → vertical split → horizontal split); "
        "hold Alt+Shift+O to peek at the original");
    // Start/Stop drawing: mirrors hotkeysConfig startDraw=Alt+A,
    // stopDraw=Alt+S. actNewLine_ keeps "commit + begin a fresh line" but loses
    // its shortcut to avoid colliding with Stop (Alt+S now drives stopDraw).
    actStartDraw_ = newAction("Start Drawing", hotkey("startDraw", "Alt+A"));
    actStopDraw_ = newAction("Stop Drawing", hotkey("stopDraw", "Alt+S"));
    // Short labels for the shared toolbar button (QToolButton renders iconText());
    // the menu entries keep the full "Start Drawing" / "Stop Drawing".
    actStartDraw_->setIconText("Start");
    actStopDraw_->setIconText("Stop");
    actNewLine_ = newAction("New Line", "Alt+N");
    actUndo_ = newAction("Undo", hotkey("undo", "Ctrl+Z"));
    actRedo_ = newAction("Redo", hotkey("redo", "Ctrl+Shift+Z"));
    actDeleteLast_ = newAction("Delete Last Point", "Backspace");
    // Selection deletes (shared hotkeysConfig deleteLine=Alt+Delete,
    // deletePoint=Alt+Shift+Delete). On macOS Delete→Backspace so ⌥⌫ / ⌥⇧⌫ work.
    actDeleteLine_ = newAction("Delete Selected Line (Point if focused)",
                        platformizeSeq(hotkey("deleteLine", "Alt+Delete")));
    actDeletePoint_ = newAction("Delete Selected Point",
                         platformizeSeq(hotkey("deletePoint", "Alt+Shift+Delete")));
    actClearAll_ = newAction("Clear All Lines", hotkey("clearAllLines", "Alt+W"));
    actDeselect_ = newAction("Deselect", "Esc");
    actZoomIn_ = newAction("Zoom In", hotkey("zoomIn", "Alt+Up"));
    actZoomOut_ = newAction("Zoom Out", hotkey("zoomOut", "Alt+Down"));
    actFit_ = newAction("Fit to Window", hotkey("resetZoom", "Alt+0"));
    actShowPoints_ = newAction("Show Points", hotkey("togglePoints", "Alt+P"));
    actShowLines_ = newAction("Show Lines", hotkey("toggleLines", "Alt+L"));
    actTheme_ = newAction("Dark Theme", hotkey("toggleTheme", "Ctrl+D"));
    actPanel_ = newAction("Selection Panel", hotkey("togglePointsList", "Alt+X"));
    actToolbars_ = newAction("Toolbars", hotkey("toggleControls", "Alt+C"));  // show/hide the top toolbars
    actFullscreen_ = newAction("Enter Fullscreen", hotkey("fullscreen", "Alt+F"));
    actSettings_ = newAction("Visuals && Settings…", hotkey("openVisuals", "Alt+V"));
    setActionTip(actSettings_, "Default visuals & highlight styles");   // the browser #visuals-btn title
    actProjects_ = newAction("Projects…", hotkey("openProjects", "Ctrl+Shift+P"));
    actConnect_ = newAction("Servers…", hotkey("openServers", "Ctrl+Shift+K"));
    setActionTip(actConnect_,
        "Connect to collaboration servers — shared projects appear with a golden outline");
    actLinks_ = newAction("Image Links…", hotkey("openLinks", "Ctrl+Shift+L"));
    actDescription_ = newAction("Project Description…", hotkey("openDescription", "Alt+Shift+D"));
    actKeywords_ = newAction("Project Keywords…", hotkey("openKeywords", "Alt+Shift+K"));
    actOpenIn_ = newAction("Open In…", hotkey("openIn", "Ctrl+Shift+E"));
    setActionTip(actOpenIn_,
        "Open the current project in the browser app or the Telegram bot");
    // AI Assistant chat dock toggle (llm-contract.md; shared hotkeysConfig toggleChat).
    actChat_ = newAction("AI Assistant", hotkey("toggleChat", "Alt+G"));
    // Named to disambiguate from the dock's internal toggleViewAction (same
    // visible text) for the GUI e2e lookup.
    actChat_->setObjectName("actChat");
    actChat_->setCheckable(true);
    setActionTip(actChat_, "Chat with the AI assistant — plan edits, variants, and layouts");
    // Animated reveal/dismiss (browser parity). A popover gesture (armed
    // pop_.anchor) opens the COMPACT floating chat instead — that gesture
    // always means "show it compact here", so an unchecking trigger is re-checked.
    connect(actChat_, &QAction::toggled, this, [this](bool on) {
      QWidget* anchor = pop_.anchor.data();
      pop_.anchor.clear();
      if (anchor && chatDock_) {
        if (!on) {
          QSignalBlocker b(actChat_);
          actChat_->setChecked(true);
        }
        openChatCompact(anchor);
        return;
      }
      setChatShown(on, /*animate=*/true);
    });
    // New Project has no shared hotkeysConfig.json entry; literal default here.
    actNewProject_ = newAction("New Project", "Ctrl+Shift+N");
    actSaveProject_ = newAction("Save to Project", "Ctrl+Shift+S");
    // Trash: clear (remove) the current project/editor (mirrors the browser's
    // #clear-storage danger button). Hidden for server projects (refreshActions).
    actClearProject_ = newAction("Clear Project", hotkey("clearProject", "Ctrl+Alt+R"));
    setActionTip(actClearProject_, "Remove current project");
    // Rename the project from the keyboard — the same inline edit the ✎ beside the
    // toolbar name opens (enterNameEdit declines when there is nothing to rename).
    actRenameProject_ = newAction("Rename Project", hotkey("renameProject", "Ctrl+Alt+N"));
    connect(actRenameProject_, &QAction::triggered, this, &MainWindow::enterNameEdit);
    actSaveSession_ = newAction("Save Session", "Ctrl+S");
    actInfo_ = newAction("Controls && Shortcuts Info", hotkey("openHelp", "F1"));
    actIncognito_ = newAction("Incognito", hotkey("toggleIncognito", "Alt+I"));
    actTooltip_ = newAction("Show Tooltips", QString());   // browser label parity (was "Hover Tooltip")
    actTooltip_->setCheckable(true);
    // Allow-formulas toggle, also reachable from the View menu so the
    // f(x,y) inputs aren't lost when the toolbar overflows. Two-way synced with
    // the toolbar allowFormulas_ checkbox below.
    actAllowFormulas_ = newAction("Allow Formulas", QString());
    actAllowFormulas_->setCheckable(true);
    actQuit_ = newAction("Quit", "Ctrl+Q");

  }


}  // namespace stencil::gui

