#pragma once
#include "chatDock.hpp"  // ChatDock::ProviderStatus (chatMirrorProviderStatus)
#include "formulaParser.hpp"
#include "llmClient.hpp"
#include "pageMetrics.hpp"
#include "projectsStore.hpp"
#include "tipContent.hpp"
#include "tooltipRows.hpp"
#include "fileStore.hpp"
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QSet>
#include <QMainWindow>
#include <QString>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QComboBox;
class QLabel;
class QScrollArea;
class QTimer;
class QFileSystemWatcher;
class QDoubleSpinBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QToolButton;
class QVariantAnimation;
class QJsonObject;
class QActionGroup;
class QWidgetAction;
class QCheckBox;
class QRadioButton;
class QButtonGroup;
class QImage;
class QPixmap;
class QMenu;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QUrl;
class QGraphicsOpacityEffect;

namespace stencil::net {
  class ConnectionManager;
  class ServerClient;
  class LiveFeed;
}

namespace stencil::llm {
  class QtLlmTransport;
  struct OpPlan;
}

class MainWindowGuiTest;  // QtTest e2e (tests/mainWindow.gui.cpp)
class QScrollBar;

// Top-level window. Mirrors the composition done by browser/js/ui/layout.js +
// toolbar.js + the DrawingApp wiring: a toolbar of actions, the canvas in the
// center, and a status bar that reports pixel and page (cm) coordinates.
namespace stencil::gui {

  class CanvasWidget;
  class OpenInDialog;
  class SelectionPanel;
  class SelectedLineBar;
  class Notifications;
  class CanvasTooltip;
  class IncognitoOverlay;
  class DropZonesOverlay;
  class ProjectDragZones;
  class MediaLoader;
  class DataExportController;
  class RemoteSyncController;
  class RemoteSession;
  class ProjectTransferController;
  class DisintegrateOverlay;
  struct LaunchOptions;

  class MainWindow : public QMainWindow {
    Q_OBJECT
   public:
    // restoreLast=false skips reloading the last autosaved session, so the editor
    // starts empty — used for a "New Incognito Editor" window (and an incognito
    // launch), which should begin blank rather than resurrecting prior content.
    explicit MainWindow(QWidget* parent = nullptr, bool restoreLast = true);
    // Out-of-line (defined in the .cpp) so unique_ptr members of forward-declared types
    // (e.g. DataExportController) are destroyed where their complete type is visible.
    ~MainWindow() override;

    // Apply command-line launch options (the desktop counterpart of the browser's
    // URL deep-links). Called from main() AFTER show() so the async image / video
    // / network resolution runs on the event loop. See gui/launchOptions.hpp.
    void applyLaunchOptions(const LaunchOptions& opts);

    // Open a file handed in by the OS shell — a Finder/Explorer "Open With", a
    // file-association double-click (via QFileOpenEvent / argv), or a drag onto
    // the window. Sniffs the suffix: *.json → layout, else image/video → --src.
    // `frame` selects the video frame (0 = first).
    void openPathFromOS(const QString& path, int frame = 0);

    // Open a stencil:// deep link handed in by the OS (macOS QFileOpenEvent url,
    // Linux argv %u): parse it (launchOptions parseStencilUrl) and apply it like
    // launch options — a server-project reference connects + opens; inline
    // src/layout load like --src/--layout. Malformed links just notify.
    void openStencilUrl(const QUrl& url);

   private slots:
    // The single Open entry (File ▸ Open / top-left toolbar): the unified Open dialog
    // (mirrors browser openImageModal.js) — a local file, a web URL, or a new blank.
    void openImage();
    // Idle-canvas + projects "new blank" shortcut: opens the same dialog in blank mode.
    void newBlankImage();
    // Crop dialog (mirrors browser cropModal.js): pick the page-shaped region of
    // the original image to show on the canvas. Confirms before discarding lines
    // when the orientation flips; the original image is never replaced.
    void openCropDialog();
    void onHovered(double imageX, double imageY);
    void refreshActions();
    void onCanvasChanged();
    void onSelectionChanged();
    void onPageSizeChanged();
    void validateAndApplyFormulas();
    // Toolbar line-style row (S8): push the current default visuals to the canvas
    // and persist them. Mirrors browser drawingApp.js lineColor/lineThickness/
    // pointSize/lineStyle change handlers (~155-178).
    void onLineStyleControlChanged();
    // Single source of truth for the filter/style controls that exist in BOTH the
    // toolbar and the context menu: apply + persist + keep the two UIs in sync.
    void applyImageFilter(const QString& mode);
    // Compare view (transient): apply to the canvas + sync the toolbar combo and the
    // View → Compare submenu radio set.
    void setCompareModeUi(const QString& mode);
    void applyTintColor(const QColor& color);
    void applyLineStyle(const QString& style);
    // Paint a small color chip onto a swatch toolbutton (S8).
    void updateColorSwatch(QToolButton* btn, const QColor& color);
    // The point colour new lines actually draw in: defaultPointColor when set, else the
    // line colour it inherits (core pointColorOr). Paints the Points swatch.
    QColor effectiveDefaultPointColor() const;
    void showContextMenu(const QPoint& globalPos);
    // Shows/hides actCopyImageSplit_/actSaveImageSplit_ ("With Compare") with
    // the canvas's split-compare state, and moves the real Ctrl+C/Ctrl+Shift+D shortcut onto whichever
    // of "Current"/"With Compare" is the primary gesture right now — see their
    // declarations below for why. Called from every refresh point (refreshActions,
    // syncContextActions) AND after a theme repaint (applyTheme), since styleActionIcons()
    // would otherwise reset the split action's icon back to the static default.
    void syncSplitCopyDownloadSlot();
    // Re-sync the persistent context-menu actions to the live canvas state
    // (enable flags, draw-mode label, point/thickness seeds, group checks,
    // tooltip rows). Called as the first statement of showContextMenu().
    void syncContextActions();
    void onHoverDetail(double imageX, double imageY, const QPoint& globalPos,
                       Qt::KeyboardModifiers mods, bool immediate = false);
    // Debounces the tooltip reveal by the target hovered (browser: tooltip.js
    // scheduleShow); `immediate` (refreshHoverForModifiers) skips the wait outright.
    void scheduleHoverShow(const QString& key, std::function<void()> revealFn, bool immediate);
    // Drops the tooltip and any pending reveal for it.
    void hideHoverTooltip();
    // Data actions (S9): the layout JSON export/import + clipboard + image save/copy methods live
    // in DataExportController (dataExport_). pasteImage() stays here — it creates a project — and
    // delegates its JSON-text fallback to dataExport_->pasteLayout().
    void pasteImage();

   private:
    // Catches Escape + focus-out on the project-name field so the user can always leave the edit.
    bool eventFilter(QObject* obj, QEvent* event) override;
    void buildActions();
    // S11: construct the persistent grouped actions + QWidgetActions used by the
    // nested right-click context menu (style/filter submenus, tooltip rows, the
    // draw-mode bridge). Mirrors the wiring done in browser/js/ui/contextMenu.js
    // wire() (~112-605). Called once, right after buildActions().
    void buildContextActions();
    // Build the two toolbar buttons' export-options popups (Copy Image / Download Image
    // variants) — double-click / right-click opens, Alt+hover on a row previews it. Called
    // once, right after buildToolbar() (needs the buttons, resolved via buttonForAction).
    void wireExportOptionsPopups();
    // Wire the Alt+hover live preview (support/exportPreview.hpp) onto one export-variant
    // menu — shared by the nested context-menu submenus and the toolbar popups above.
    void wireExportPreviewHover(QMenu* menu);
    // Fill one Copy/Download Image variant menu (split · current-row · the two fixed
    // variants) and wire its preview hover — the ONE row order every surface shows
    // (context menu, Data menu, toolbar popups).
    void populateExportVariantMenu(QMenu* menu, bool copy);
    // The export actions' enable/visibility gating + the split-shortcut swap, computed
    // from the live canvas/settings state — shared by refreshActions, syncContextActions
    // and applyImageFilter.
    void syncExportActions();
    // The rendered preview image for one export-variant QAction (maps the action pointer
    // back to its variant string). Null if `act` isn't one of ours.
    QImage exportVariantPreviewImage(QAction* act) const;
    void buildMenus();
    // Put the menu bar where settings_.nativeMenuBar says. Qt only reads the flag
    // when the bar is (re)created, so switching it at runtime rebuilds the menus.
    void applyMenuBarPlacement();
    void buildToolbar();
    // ctor decomposition (behavior-preserving): the hotkeys-config read/merge,
    // and the contiguous signal-connection block (connect ORDER is preserved
    // verbatim). loadHotkeys() must run before buildActions(); wireSignals()
    // must run after buildActions/Context/Menus/Toolbar (it references their
    // widgets/actions) and before the persisted-state load.
    void loadHotkeys();
    void wireSignals();
    // buildToolbar() split into its three rows (call order preserves the
    // addToolBar/addToolBarBreak sequencing that fixes visual row order).
    void buildMainToolbar();
    // Build a toolbar "section": a small uppercase label ABOVE a horizontal strip of the given
    // actions' buttons (+ optional extra widgets) — the desktop match for the browser's stacked
    // .ctrl-section (label on top of its button row).
    // Build the f(x,y) transform fields — called before the Formula section, which takes
    // formulaGroup_ as one of its row widgets so pill and inputs share a baseline.
    void buildFormulaFields();

    // The single wrapping tool row every cluster is appended to (support/wrapRow.hpp).
    class QToolBar* toolRow() const;
    class QWidget* makeToolSection(const QString& title, const QList<class QAction*>& actions,
                                   const QList<class QWidget*>& extras = {},
                                   const QList<class QWidget*>& leading = {});
    void buildProjectNameGroup(class QToolBar* bar);   // header-row project name + rename/colour group
    void updateImageSizeInfo();                        // refresh the header-row "Image Size" readout
    // The incognito tag's markup (divider + themed glyph + accent text) for that line.
    QString incognitoTagHtml() const;
    // Pin the info row to the taller of its two states so the passive incognito
    // indicator can never reflow the window (canvas, points panel, rows below).
    void reserveImageInfoHeight();
    // Locks imageInfoDock_'s own height to its content's — see the .cpp for why.
    void syncImageInfoDockHeight();
    // The Fullscreen glyph turns over with the state (maximize ⇄ minimize).
    void syncFullscreenGlyph();
    // …and the tool rows take a deeper bottom band while they hang over the canvas.
    void markFullscreenBars(bool on);
    QPixmap makeLogoPixmap(int size) const;            // paint the mini S-mark logo (browser parity)
    // Logo accent-preset picker: a popover DIALOG opened via execMaybePopover, so the
    // Alt-peek/glide/release machinery treats it exactly like every other popover.
    // Right-click on the logo opens it sticky; hold-Alt peeks it (altPeekOpen).
    void openAccentPicker();
    // Move an open accent popover's ✓ to the accent the settings now hold (applyTheme).
    void remarkAccentPopover();
    // Hover preview for the accent popover (browser twin: accentController.previewAccent):
    // repaint the app in a preset INSTANTLY — no theme wipe, no persist — while the pointer
    // rests on its row; endAccentPreview() puts the committed accent back. A real pick
    // (applySettings) discards the snapshot so the revert never fights the commit.
    void previewAccent(const QString& key);
    void endAccentPreview();
    // Shift+F10 (hotkeysConfig contextMenu): the canvas menu under the pointer while it
    // rests over the viewport, else at the viewport's centre — where the browser puts it.
    void showContextMenuFromKeyboard();
    void buildPageFormulaToolbar();
    void buildStyleToolbar();
    void buildDrawViewToolbar();   // row 4: Draw + View (the rows above are full)
    void buildImageInfoBar();
    QString hotkey(const QString& id, const QString& fallback) const;
    // The canonical page-format value ("A4"/"custom") behind the toolbar combo's
    // display label (the item data — the label text carries the physical size).
    QString pageSizeValue() const;
    core::PageSize currentPageDimensions() const;
    core::Point pageCoords(double imageX, double imageY) const;
    // Active display unit derived from settings_.units (cm default, else inches).
    core::UnitFormat unitFormat() const;
    // Total real-world length of every drawn line segment, in centimetres, using the
    // per-axis px→cm scale (formula- and unit-independent). Mirrors browser units.js
    // layoutLineLengthCm; cached on the project meta at save time. 0 when nothing measurable.
    double currentLineLengthCm() const;
    // Stamp the display-only, canvas-derived tooltip fields (image px dims + total line
    // length in cm) onto a project's meta from live editor state. imageW/H stay 0 when
    // there's no image. Called wherever the active project entry is captured, to keep the
    // capture points from diverging.
    void stampCanvasMeta(core::ProjectMeta& meta) const;
    // Apply the current unit to the custom page spinboxes + their suffix label.
    void applyUnitToPageInputs();
    // Re-render the page-format combo's option labels in the current unit
    // (values/data untouched, so the selection and handlers are unaffected).
    void applyUnitToPageCombo();
    // Single entry point for changing units: persists, syncs both UI surfaces
    // (View ▸ Units menu + toolbar combo), and refreshes every length readout.
    void applyUnits(const QString& code);
    // Push settings_.units into the menu actions + toolbar combo (no side effects).
    void syncUnitControls();
    void zoomStep(int dir);
    void zoomIn();
    void zoomOut();
    void setZoom(double scale, bool syncCombo = true);
    void fitToWindow();
    void toggleFullscreen();
    void setToolbarsVisible(bool on);   // show/hide every top toolbar (top menu), instantly
    void fsHoverTick();                 // fullscreen: edge-hover reveal of toolbars/panel
    // Floating arrow overlays (browser parity): a chevron at the RIGHT edge toggles the points
    // panel, a chevron at the TOP toggles the toolbars — positioned where each menu is, NOT in the
    // top toolbar. Show/hide is animated (slide). buildOverlayArrows() creates them once.
    void buildOverlayArrows();
    void positionOverlayArrows();       // place + re-icon the arrows (call on resize / state change)
    void spinControlsPill(bool animate);  // turn the pill's chevron ↑⇄↓ with the toolbar fold
    void sizeViewToggles();               // re-floor the View checkboxes' width after a restyle
    void syncToastInset();                // keep the toast stack clear of the status bar
    void updatePanelReopenButton();     // show/hide + place the floating right-edge re-open chevron
    void positionPanelReopenButton();   // position it flush to the canvas' right edge, vertically centred
    void positionPanelGrip();           // place the animated canvas↔panel separator grip (dockGrip.hpp)
    void positionChatEdge();            // place the chat dock's resize-edge tint (dockGrip.hpp)
    void setPanelShown(bool show, bool animate);      // animated points-panel collapse/expand
    // The points panel is a surface too (browser mainContent.js parity): its table pours
    // out past the edge it is docked to and gathers back out of it, behind a veil
    // (panelVeil_) so the motes ARE the panel rather than a cloud over a visible slide.
    // The snapshot is taken at `full`, whichever way the slide is about to run. Null when
    // nothing played (reduced motion, headless, an unmeasurable box).
    QPointer<gui::DisintegrateOverlay> panelSurfaceFlight(bool gather, int ms, int full);
    void releasePanelVeil();            // drop that veil (finish step, or a superseding toggle)
    // …and the same for the collapsing TOOL ROWS (browser toolbar.js): they fold as one
    // block, so the flight is over their UNION rect, streaming past the window's top
    // edge. No veil — animateBarsHeight stays a pure geometry slide on purpose.
    QPointer<gui::DisintegrateOverlay> barsSurfaceFlight(const QList<class QToolBar*>& bars,
                                                         bool gather, int ms);
    // The "Selected Line:" bar's own flight (browser: selectionPanel.js surfaceIn/Out),
    // called from onSelectionChanged() only on the hidden<->visible edge. In grabs the bar
    // after the dock is shown; Out grabs it before the dock hides.
    void dustSelectedLineBarIn();
    void dustSelectedLineBarOut();
    // Where those motes land: `barPicture`'s own centre for x, just under the "Image Size:
    // …" row for y — see the .cpp for how `closing` predicts vs. reads that row's rect.
    // Falls back to dockAwayPoint off `barPicture` if the row is unmeasurable.
    QPoint selectedLineBarDustPoint(const QRect& barPicture, bool closing);
    // Animated chat-dock reveal/dismiss (browser panel parity: ~0.34 s in,
    // ~0.26 s out, ease-out). Slides the DOCKED extent — width for the
    // left/right areas, height for top/bottom — between 0 and its natural size
    // with the same min==max pinning setPanelShown uses, then hides at the end.
    // A floating dock is its own window, so it just shows/hides.
    void setChatShown(bool show, bool animate);
    // Hold the points panel at a FIXED width for the length of a chat-dock flight that
    // shares `chatArea` with it, so the slide eats into the CANVAS column only (browser
    // parity) instead of letting Qt redistribute between the two docks. No-op when not
    // sharing; stopChatAnim always releases the pin.
    void pinPanelWhileSharing(Qt::DockWidgetArea chatArea);
    // Re-split the panel and chat side-by-side when they share an L/R area AND the panel
    // is visible (splitting against a hidden dock can park it off-screen). Called from
    // wherever either dock's visibility changes. Idempotent.
    void ensurePanelChatSplit();
    // Pin the chat dock to a side (title bar buttons, a drag dropped on a dock zone,
    // or toggleChatFloat's float→dock leg), animated via chatSurfaceFlight below.
    void dockChatTo(Qt::DockWidgetArea area);
    // A docked chat panel is ALSO a surface (setChatShown's own open/close play the
    // same flight): motes stream out of (or into) the far side of the edge named by
    // `area`, `gather` true for an arrival. `pin` is the caller's own width/height
    // setter — the snapshot is taken at the panel's settled `full` extent regardless
    // of which way `pin` is about to animate, then handed back to it. Builds the veil
    // (chatVeil_) too, so the real dock stays invisible for the whole flight instead
    // of visibly sliding under a full-brightness picture of itself. Shared by
    // dockChatTo (side switches) and the title bar's Float toggle. Null when nothing
    // played (reduced motion, an unmeasurable box) — the caller falls back to `pin` alone.
    QPointer<gui::DisintegrateOverlay> chatSurfaceFlight(Qt::DockWidgetArea area, bool gather, int ms,
                                                         const std::function<void(int)>& pin, int full);
    // The min==max pin on the chat dock's slide axis (width for L/R docks, height for
    // T/B) — the `pin` every chat slide hands to chatSurfaceFlight/startExtentSlide.
    std::function<void(int)> chatExtentPin(bool horiz);
    // The title bar's Float button (browser chat-float-btn parity): animated, unlike
    // QDockWidget::setFloating() alone — docked leaves through chatSurfaceFlight's dust
    // the same way a side switch does, then the floating shape flies out of the icon
    // (support::revealWindow); floating leaves through dismissWindow, then re-docks at
    // the last area it held (dockChatTo), which plays its own arrival dust.
    void toggleChatFloat();
    // The chat icon's popover gesture (dblclick / right-click, like every
    // dialog-opening icon): float the dock at its compact size pinned next to
    // the icon — the browser's compact chat (chatPanel.js compactChatRect).
    void openChatCompact(QWidget* anchor);
    // …and the second half of that swap: the compact float itself, opened once
    // any chat already on screen has finished animating out.
    void openChatCompactNow(QWidget* anchor);
    // The global rect the compact popover takes for `anchor` (placement + the
    // dock's tear-off size) — also what tells a re-pin from a real move.
    QRect compactChatRect(QWidget* anchor) const;
    // Alt+hover peek: open `act`'s popover pinned to `btn` (browser popover.js
    // altHover parity). Closes a compact chat the glide moved off first.
    void altPeekOpen(class QToolButton* btn, QAction* act);
    // Hover-bind a LINGERING window (engaged peek after the Alt release).
    void startLingerPoll();
    void stopLingerPoll();
    // Stop an in-flight chat slide and release the pinned size constraints
    // (re-entrancy, tear-off mid-slide, teardown). `settle` applies the
    // animation's target extent first.
    void stopChatAnim();
    void setToolbarsShown(bool show, bool animate);
    // Show the "?" only while the tool rows are collapsed and there is something to read.
    void refreshStatusHintVisibility();   // animated top-menu collapse/expand
    // Shared height slide (0↔natural) for a set of toolbars, pinning min==max each frame. Used by the
    // pill collapse (tool rows only) AND the fullscreen edge-hover reveal (all rows incl. header).
    void animateBarsHeight(const QList<class QToolBar*>& bars, bool show);
    void scrollTo(int x, int y);
    void setZoomAnchored(double newScale, const QPoint& cursorInViewport);
    void applyTheme();
    // Assign shared line-art icons to every action + icon toolbutton, tinted to the
    // theme text color. Re-run from applyTheme() on each light/dark/accent change.
    void styleActionIcons(bool dark, const QColor& iconColor);
    // Destructive actions are FILLED red on the toolbar (browser .danger.btn-icon parity)
    // and red-on-menu-background in menus, so the button's glyph differs from the action's.
    void styleDangerToolButtons();
    bool sectionButtonVisible(QAction* act, QToolButton* btn) const;
    // The two Draw toggles' faces (support/faceSwap.hpp). Each puts the button in the
    // state named — the Start/Stop toggle also takes the matching accent treatment,
    // outlined while idle, filled while drawing — and, with `animate`, gets there through
    // the shared swap instead of blinking. `animate=false` is the repaint path (theme /
    // accent change), which must land on the end state at once.
    void syncDrawToggleFace(bool drawing, bool animate);
    void syncDrawModeFace(bool rect, bool animate);
    void bindRevealAnchors();
    void bindRevealAnchor(QAction* a);
    QColor toolButtonIconColor(QAction* act, const QColor& normal) const;
    // macOS menu-bar icons follow the SYSTEM appearance, not our theme; reconcile the
    // two when they disagree (support/theme.hpp systemPrefersDark).
    void retintMenuIconsForSystem(bool appDark, const QColor& appIconColor);
    // The visible toolbar button presenting an action (anchors the theme wipe).
    QWidget* buttonForAction(QAction* act) const;
    QHash<QAction*, QString> actionIconNames_;   // action → shared-icon glyph name
    // Recolour the context-menu hosted checkboxes/radios' indicators to the theme TEXT colour
    // (not the app-wide accent), so they read like the surrounding menu text.
    void restyleContextToggles(const QColor& textColor);
    void toggleTheme();
    void applySettings(const Settings& s, bool persist);
    void openSettings();
    // The chat dock's gear: the dedicated assistant-only dialog (browser
    // llmSettingsModal parity), writing the same llm* keys through the same
    // applySettings path as the full Settings dialog. Kept as a distinctly named
    // pair rather than an overload — &MainWindow::openAssistantSettings is taken
    // by address in several connect()s, which needs it unambiguous.
    void openAssistantSettings();  // the gear's own "…"-trigger anchor
    // `anchor` is the button the dialog's dust flight belongs to — the
    // unreachable card's "Configure provider" CTA, which (unlike the gear)
    // stays on screen through the click. `anchorRect` (GLOBAL) is the fallback
    // for a caller whose own button is ABOUT to be hidden (the context-menu
    // mirror's gear/CTA: opening the dialog closes that popup first) — captured
    // by the caller while the button was still visible, support::revealDialog
    // parity with pickColorAnimated's own anchorRect fallback.
    void openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect = QRect());
    // Build (once) the ChatMenuPanel hosted by the context menu's "Assistant ▸"
    // submenu — a persistent QWidgetAction, parented to the WINDOW, so the
    // transcript survives the menu being rebuilt on every right-click. Only ever
    // called while a provider is configured; showContextMenu omits the whole
    // Assistant entry when it isn't.
    void ensureChatMenuPanel();
    // ── chat transcript fan-out ──
    // The conversation has ONE pipeline (onChatSend/onChatReply, one history,
    // one client, one plan executor) and two views. These mirror what the dock
    // is told onto the context-menu panel; each is a no-op until that panel
    // exists. No plan or execution logic lives here.
    void chatMirror(const QString& role, const QString& text, bool muted,
                    const QString& retryText = QString(), const QStringList& notes = {},
                    bool configure = false);
    // Transcript error/notice lines fanned out to BOTH views. A non-empty
    // toastError additionally raises the hidden-dock failure toast
    // ("Assistant failed — <toastError>").
    void chatError(const QString& text, const QString& toastError = QString());
    // A transport/config failure the user can fix by choosing a different
    // provider (browser "unreachable" kind) — an error card with a "Configure
    // provider" CTA instead of chatError's plain Retry.
    void chatUnreachable(const QString& text, const QString& toastError = QString());
    void chatMirrorPending(bool show);  // add / remove the in-flight "…" row
    void chatMirrorStopped(const QString& retryText = QString());  // → a "Stopped." card
    // Can the user SEE a chat result right now? False while both surfaces are
    // away — and a dock mid-close counts as away (its slide keeps isVisible()
    // true for 260ms, which used to swallow the toast for a turn landing then).
    bool chatSurfaceHidden() const;
    // (No unread mark on the chat icon: a result that lands with no surface to show it
    // toasts, and the toast opens the chat — a badge left behind after it faded was one
    // more thing to dismiss. Browser twin: js/ui/chatPanel.js, same decision.)
    // Resend the failed/stopped turn (the dock signal AND the panel card use it).
    void chatRetryTurn(const QString& text);
    void chatMirrorBusy(bool on);       // send button ⇄ stop button
    void chatMirrorClear();
    // Reply text with its warnings folded in, the way the dock renders them.
    static QString withChatWarnings(const QString& text, const QStringList& warnings);
    // A late note (§3.2/§3.1) reported INTO the last mirrored card, as the dock does.
    void chatMirrorLateNote(const QString& text);             // the dock's trash button clears both
    // Late note fanned out to BOTH views in one call — every dock-only
    // appendLateNote left the panel a row short (or, mirrored as its own row, a
    // row long). Use this, never the dock's method alone.
    void chatLateNote(const QString& text);
    // Muted standalone "Note" card on BOTH views (e.g. "this model is text-only").
    void chatNote(const QString& text);
    // Provider reachability (refreshLlmStatus): the dock's gear badge + the
    // context-menu panel's carry the same tooltip and dot.
    void chatMirrorProviderStatus(const QString& richTooltip,
                                  ChatDock::ProviderStatus status);
    void scheduleAutosave();
    void saveSessionNow();
    void restoreSession();
    // Pan/zoom persistence (browser parity: storage.js's debounced scrollLeft/scrollTop/zoom
    // save + "Saved" toast). Lighter than saveToActiveProject(): touches only the active
    // project's view fields, not lines/crop/chat. scheduleViewSave() debounces every route
    // (setZoom, the two scrollbars); saveActiveProjectView() runs once the burst settles.
    void scheduleViewSave();
    void saveActiveProjectView();
    // Canvas scrollbars are invisible at rest, revealed only by an actual pan or zoom —
    // driven from code via a QGraphicsOpacityEffect per bar (QSS can't express "hidden until
    // an unrelated action, then fade out"). revealCanvasScrollbars() shows both and restarts
    // the idle timer; scheduleScrollbarHide() is the shared restart path eventFilter's
    // hover-suppress uses too.
    void revealCanvasScrollbars();
    void scheduleScrollbarHide();
    // The floating bar the user sees/drags for an axis (overlayScrollArea.hpp);
    // scroll_->horizontalScrollBar()/verticalScrollBar() stay the value model.
    QScrollBar* canvasScrollBar(Qt::Orientation o) const;
    void openProjects();
    // Build id -> edited-result preview pixmaps for the local project list, shown as
    // the Projects dialog's row icons. Each is rendered through the same canvas/export
    // path the editor uses (filtered image + drawn lines): the active project from the
    // live canvas (current unsaved edits), the rest composited offscreen from their
    // stored image+crop+rotation+lines. Pathless (in-memory) sources get no preview.
    QHash<QString, QPixmap> buildProjectThumbs() const;
    // Server connections dialog (mirrors browser connectModal.js): connect to /
    // disconnect from collaboration servers. Lazily creates the ConnectionManager.
    void openConnections();
    // Lazily build the window's ConnectionManager, wiring its changed() signal to
    // persist the live server set (connectionStore) so it survives relaunch.
    stencil::net::ConnectionManager* ensureConnections();
    // Startup auto-connect (mirrors the browser's "auto-connect on open"): when the
    // preference is on, re-establish the saved server set best-effort. Gated to the
    // primary restored window so spawned windows don't each reconnect.
    void autoConnectServers();
    // Surface a security notice for any live connection that talks plaintext http to a
    // remote host (bearer token + image bytes sent in the clear). Called after connect.
    void warnInsecureConnections();
    // Load a saved project (by id) into THIS window's canvas, mirroring the
    // browser switchToProject(): set page size, restore image + lines + crop,
    // mark it active. Returns false if no project with that id exists.
    // `animate` plays the dust arrival (a picture is landing on the canvas); pass false
    // for a REBIND, where the same image stays put and a flourish would be a lie.
    bool loadProjectIntoCanvas(const QString& id, bool animate = true);
    // Open a saved project in a NEW top-level window, leaving this one untouched
    // (the desktop counterpart of the browser's "open in new tab"). The new
    // window owns itself (WA_DeleteOnClose) and reads projects from disk.
    void openProjectInNewWindow(const QString& id);
    // Local↔server project transfer (move/copy to/from a server + the shared import) lives in
    // ProjectTransferController (projectTransferController.hpp), constructed as projectTransfer_;
    // the Projects dialog callbacks call projectTransfer_->move/copy*().
    // True if `id` is the active project in some OTHER open window — used to block
    // removing/moving a project that's open elsewhere (the desktop analogue of the
    // browser's "open in another tab" guard).
    bool projectOpenInOtherWindow(const QString& id) const;
    // Unified Open dialog driver (shared by File ▸ Open and the blank shortcuts):
    // shows OpenImageDialog and dispatches its outcome (here/new-window/replace/blank).
    void openImageDialog(bool startBlank);
    // Generate + adopt a solid-color blank image (the dialog's blank-mode outcome).
    void createBlankImageFromDialog(const QColor& color, int w, int h);
    void createBlankImage(const QColor& color, int w, int h);   // no confirm — the op-plan path
    // Open dialog outcomes. Open here: replace this editor's image
    // (saving the current content first unless incognito), adopting the chosen
    // incognito mode. New window: launch a fresh window loading the image (via
    // applyLaunchOptions, the --src/--incognito path), leaving this one untouched.
    void openImageHere(const QString& path, bool incognito);
    void openImageInNewWindow(const QString& path, bool incognito);
    // Same two outcomes for a URL / local video source, which resolves asynchronously
    // via MediaLoader (openImageSource) rather than a synchronous local-image load.
    void openSourceHere(const QString& src, int frame, bool incognito);
    // `crop*` carry the Open-Image dialog's quick-crop into the fresh window: it
    // re-resolves the same source (identical pixels) and applies the same page-aspect
    // crop (`cropToPage`) in `cropAlbum`/portrait at `cropPage`, or opens the whole
    // frame when a preview was taken with cropping off (`hasPreview && !cropToPage`).
    void openSourceInNewWindow(const QString& src, int frame, bool incognito,
                               bool hasPreview = false, bool cropToPage = false,
                               bool cropAlbum = false,
                               const QString& cropPage = QString());
    // Adopt already-decoded preview pixels from the Open-Image dialog directly (no
    // re-fetch/seek), honoring its quick-crop choice. Mirrors openLinks' reuse of the
    // previewed image. `localPath` is the originating file for a local image (kept for
    // saves), empty for a URL/video frame; `provSource` records the URL as provenance.
    // `cropToPage` crops centered to `cropPage` in `cropAlbum`/portrait; off ⇒ whole
    // frame. Resets the editor like openSourceHere before adopting.
    void openPreviewedImageHere(const QImage& image, const QString& localPath,
                                const QString& provSource, bool incognito,
                                bool cropToPage, bool cropAlbum,
                                const QString& cropPage);
    // Replace outcome: swap the CURRENT project's image in place (same local id / server
    // link), optionally renaming the project + keeping the existing annotations. Server
    // sessions also re-upload the `original` (replaceServerOriginal). canReplaceActive()
    // gates the outcome (a saved/linked, non-incognito project must be open).
    bool canReplaceActive() const;
    void replaceProjectImage(const QString& path, bool rename, bool keepAnnotations);
    void replaceServerOriginal(std::function<void()> done = {});
    // Publish the current incognito session to a server: create + upload original + link the
    // session, leave incognito, then push the layout + result. Mirrors the browser.
    void publishIncognitoToServer(const QString& serverUrl);
    // Load a local file as a fresh image (resets page + provenance); returns success.
    bool loadLocalImageReset(const QString& path);
    // Launch support: open a saved project by NAME (case-insensitive; first
    // match), used by --project. Returns false when no such project exists.
    bool openProjectByName(const QString& name);
    // Launch support: adopt a freshly resolved --src image onto the canvas (path
    // non-empty for a local file, so it survives session/project saves), then
    // apply any pending --layout. Wired to MediaLoader::loaded.
    void onLaunchImageLoaded(const QImage& image, const QString& localPath);
    // Apply (and consume) pendingCrop_ to the just-loaded image: a Page crop to the
    // chosen page+orientation, a None full-frame crop, or nothing for Auto (the
    // default page-aspect crop applied at load). Mirrors the browser's load opts.
    void applyQuickCrop();
    // Launch support: load a layout JSON from a local path or URL and adopt it
    // (shared applyLayoutJson guards apply). Used for --layout after --src loads.
    void applyLayoutFromSource(const QString& src);
    // Lazily build + wire the async --src resolver, then begin resolving `src`
    // (image / URL / video frame). Shared by --src, the positional/OS open path,
    // and drag-and-drop.
    void openImageSource(const QString& src, int frame);
    void ensureMediaLoader();
    // Source/resource links dialog (mirrors browser linksModal.js): view/edit/open/
    // remove the active image's provenance and add a new image by URL. Edits persist
    // to the active project; a URL load routes through loadImageByUrl().
    void openLinks();
    // Description / keywords editors for the active SAVED project (browser parity: the
    // DESCRIPTION & ATTRIBUTES cluster); both persist through the projects store.
    void openDescription();
    void openKeywords();
    // Load an image/video BY URL (extracting frame `frame` for video), tagging the
    // result with `source`/`resource` provenance so the next project save records it.
    void loadImageByUrl(const QString& source, const QString& resource, int frame);

    // OS-shell window spawners for the Dock menu / Jump-list-style actions. They
    // create self-owned top-level windows (WA_DeleteOnClose) so they never depend
    // on the lifetime of the window that triggered them — safe to invoke from a
    // long-lived application Dock menu.
    static void openIncognitoWindow();
    static void openProjectsWindow();
    static void openProjectWindowById(const QString& id);
    // Rebuild the macOS Dock menu (New Incognito Editor · Open Projects · recent
    // projects). No-op off macOS. Called after project-list changes.
    void refreshDockMenu();
    void newProjectFromCanvas();
    // Project-creation entry point: when ≥1 server is connected it first asks for a
    // target (this computer vs which server); otherwise it saves locally. Used by
    // openProjects' New action + newProjectFromCanvas.
    void createProject(const QString& name);
    // Build a Project from the current canvas, persist locally, mark it active,
    // refresh, and (when announce) notify. pr.meta.name == the passed name. A
    // pathless canvas (blank / remote / video frame) is written to the state dir
    // first so the project keeps its pixels.
    void createLocalProject(const QString& name, bool announce = true, bool fromFile = false);
    // Auto-persist the freshly-loaded canvas as a local project so it appears in
    // Projects immediately (browser parity: the active editor is always a saved
    // project). No-op while incognito, already bound to a project/server session,
    // or with no image. Called from the fresh-load entry points.
    void adoptCanvasAsLocalProject();
    // Create the project on `serverUrl` (createProject + upload the original image)
    // and link this session to it so later saves write back. Mirrors the browser's
    // createRemoteProject (remoteSync.js).
    void createServerProject(const QString& serverUrl, const QString& name,
                             std::function<void()> onLinked = {});
    void saveToActiveProject();
    // Trash button (mirrors the browser #clear-storage handler): confirm, then clear
    // the current editor back to blank — removing the active LOCAL project from the
    // store first when one is open. Hidden for server-linked sessions (refreshActions),
    // so it only ever runs for a local/temporary editor.
    void clearCurrentProject();
    // Reset the editor to the empty "Open an image" canvas (drops the image, lines,
    // project binding + provenance). The desktop equivalent of storage.newTemporary().
    void resetToBlankEditor();
    // Save a server-linked session back: version-guarded PUT of name+layout, then
    // upload the rendered result. Surfaces a 409 "edited elsewhere" message and
    // leaves the link untouched. Mirrors the browser's saveToServer/saveRemoteProject.
    void saveToServer();
    // Open a server-stored project: download its original image + layout, load them
    // into this canvas, and link the session to {serverUrl, id, version}. `silent`
    // suppresses the "Opened …" toast (used by the live-co-edit poll, which reloads
    // repeatedly when peers change the project). link=false adopts the content only —
    // no session link, no live co-edit, nothing pushed back — the desktop analogue of
    // the browser's copyServerProjectToIncognito (used by incognito deep links).
    void openServerProject(const QString& serverUrl, const QString& id, bool silent = false,
                           bool link = true);
    // Deep-link support: connect to `serverUrl` the way a user would from the
    // Servers dialog (reuse the live connection, else a saved token, else mint one
    // via POST /auth/token), then open project `id` — unlinked when `incognito`.
    // On connect failure notifies and opens the Servers dialog (the normal path).
    void openServerLaunch(const QString& serverUrl, const QString& id, bool incognito);
    // "Open in…" (browser app / Telegram bot) dialog for the current session —
    // the desktop counterpart of the browser's open-in modal (openInModal.js).
    void openInAnotherApp();
    // …the same hand-off aimed at a projects-list row rather than the open session.
    // `serverUrl` non-empty = a server row, which sends only the reference; a local row
    // sends its stored image + layout. The active project falls through to
    // openInAnotherApp() so the live state is used.
    void openInAnotherAppFor(const QString& id, const QString& serverUrl,
                             const QRect& closeRect);
    // What either hand-off above gathered: a server reference (url + id), else the inline
    // image + layout. dispatchOpenIn does the rest — the dialog, the Telegram branch, the
    // #stencil= payload and the size gates — so the two differ only in where this came from.
    struct OpenInSource {
      QString serverUrl, serverId;
      qint64 version = 0;
      QImage image;
      QString name, source, resource;
      QJsonObject layout;
      bool startIncognito = false;
    };
    void dispatchOpenIn(const OpenInSource& src, bool browserAvailable, bool telegramAvailable,
                        const std::function<int(OpenInDialog&)>& run);
    // Adopt a full layout envelope (crop + rotation + filter + lines + page/formulas,
    // in the ORIGINAL image's pixel space) onto `img` and show it — the shared body of
    // opening a server project and of an inline browser→desktop "Open in…" hand-off.
    // Unlike applyLayoutJson (the lines-only file-import path), this restores the crop,
    // rotation and filter too, so no dimension-mismatch prompt and nothing is dropped.
    void loadImageWithLayout(const QImage& img, const QJsonObject& layout,
                             const QByteArray& sourceBytes = {}, const QString& sourceExt = {});

    // ── .stencil portable project files ──
    // Open one (image + layout + metadata + optional theme) — also the OS-open / drag /
    // file-arg entry for *.stencil (see openPathFromOS) — and save the current project as one.
    void openProjectFile(const QString& path);
    void saveProjectFileAs();
    void deleteProjectFile();   // delete the linked .stencil file from disk (confirm), then unlink

    // ── .stencil live sync (opt-in): auto-save edits back to the linked file + watch it for
    // external changes (another client), applying them in place or prompting on conflict.
    // Mirrors the browser StencilSync. `stencilLink_` empty ⇒ not file-linked.
    QByteArray buildStencilBytes();                 // serialize the current project to .stencil bytes
    void linkStencilFile(const QString& path, const QByteArray& baseline);
    void unlinkStencilFile();                       // drop the link: stop auto-save + watcher, disable the file-linked actions (mirrors StencilSync.unlink)
    void writeStencilNow(const QByteArray& prebuilt = {});  // write to the linked file; reuse prebuilt bytes if given
    void scheduleStencilAutosave();                 // debounced auto-save on edit
    void flushStencilAutosave();
    void onStencilFileChanged(const QByteArray& prebuilt = {});  // watcher: external change → apply / prompt (reuse prebuilt local bytes)
    void applyStencilExternal(const QByteArray& text, bool merge = false);
    void toggleStencilLiveSync(bool on);
    // The current page format + x/y formulas (from global settings) as a layout-envelope meta,
    // passed to buildLayoutJson on server save so they round-trip to the browser/peers.
    fileStore::LayoutMeta currentLayoutMeta() const;
    // Adopt a fetched layout's page format + formulas into the toolbar + settings (only the
    // fields it carries), so a reopened server project shows its saved page and a later save
    // re-emits them instead of clobbering with the desktop's global default.
    void adoptServerLayoutMeta(const QJsonObject& layout);
    // Live co-edit push/pull (debounce/poll/reload timers + LiveFeed) lives in
    // RemoteSyncController (remoteSyncController.hpp), constructed as remoteSync_. MainWindow
    // calls remoteSync_->scheduleRemotePush()/startRemotePoll()/stopRemotePoll().

    // ── AI assistant (llm-contract.md; chat dock + LLM client glue) ──
    // ChatPlanTarget (mainWindow.cpp) adapts the live editor to the plan
    // executor's narrow PlanTarget interface; it needs the private appliers.
    friend class ChatPlanTarget;
    // The GUI e2e drives private completion paths (mocked chat replies → toast).
    friend class ::MainWindowGuiTest;
    // Lazily build the LLM client (QtLlmTransport-backed) with a token resolver
    // that prefers the LIVE connection's token over the saved one.
    void ensureLlmClient();
    // The persisted provider config as the contract's LlmSettings shape; an
    // empty llmServerUrl resolves to the first configured server connection.
    stencil::llm::LlmSettings currentLlmSettings() const;
    // Recompose the chat gear's rich provider tooltip and — while the dock is
    // open — probe reachability (LlmClient::probe) to drive its status dot.
    // Called on dock open and after a settings change.
    void refreshLlmStatus();
    // Short dynamic suffix appended to the canonical system prompt (working
    // image dimensions; whether the input is a video + its frame count).
    QString chatSystemSuffix() const;
    // Chat send pipeline: append the user turn (attachments downscaled to
    // ≤1568 px and base64-encoded), replay the bounded history per the image
    // replay rule, call the provider, then parse + execute the op-plan.
    void onChatSend(const QString& text);
    void onChatReply(const stencil::llm::LlmReply& reply);
    // §7 auto-continuation: re-send once when a plan only LOADED an unseen picture.
    bool maybeContinueChat(const stencil::llm::OpPlan& plan);
    // Its plan-shape test (loads a picture, drew no layout), consulted BEFORE the
    // reply renders so a round-1 bubble a continuation would supersede is held back.
    bool chatPlanLoadsWithoutTracing(const stencil::llm::OpPlan& plan) const;
    // Post a held round-1 bubble whose continuation never fired (or failed) —
    // no reply is ever lost. No-op with nothing held.
    void flushHeldChatReply();
    // The dock's STOP button: abort the in-flight request; the canceled reply
    // then lands in onChatReply, which renders "Stopped." (no history push,
    // no toast).
    void onChatStop();
    // The dock's "Clear the conversation" button: the dock already emptied the
    // transcript + attachments, so drop the model-side per-conversation state —
    // the replayed history and the video/working-image caches that describe it —
    // and (§12.2) the persisted copy when save-chats is on. Provider settings
    // and the working image itself are NOT touched.
    void onChatClear();
    // §10 clearChat (deferred): a plan's clearChat op only flags the request
    // (chatClearPending_); chatTurnSettled runs at every turn terminal — a turn
    // ends once its plan executed and its reply showed (§3.0) — and queues the
    // confirm + clear.
    void chatTurnSettled();
    void runDeferredChatClear();
    // ── Chat persistence (llm-contract.md §12) — all gated on
    // settings_.saveChatsWithProject (default off) and never active in
    // incognito. resetChatState is onChatClear minus the persisted-copy
    // deletion, reused by restores (which must not delete what they read).
    void resetChatState();
    // chatHistory_ → the §12.1 document (text-only; empty object = no chat).
    QJsonObject buildActiveChatDoc() const;
    // Write the conversation to its home: the local project record, or the
    // linked server project's "chat" file kind. Called after each settled turn.
    void persistActiveChat();
    // Replace the conversation with a stored document: seed chatHistory_ and
    // replay the turns into the dock + context-menu mirror. Empty doc = a
    // fresh conversation scope. Never triggers a model call, never writes.
    void restoreChatFromDoc(const QJsonObject& doc);
    // Drop the persisted copy (project record / server "chat" file) on Clear.
    void clearPersistedChat();
    // Bottom-left completion toast for turns finishing while the dock is
    // hidden (browser closedToast parity): short status, ~90 chars, click =
    // open the chat. No-op when the dock is visible (callers gate).
    void showChatToast(const QString& text, bool success);
    // Append to chatHistory_ and drop the oldest beyond the 32-message bound.
    void pushChatHistory(const stencil::llm::ChatMessage& m);
    // The wire view of chatHistory_: last 32 messages; only the current turn's
    // images + the single most recent prior image ride along (contract §7).
    QVector<stencil::llm::ChatMessage> wireChatMessages() const;
    // A video was attached in the chat: remember it as the current video input,
    // extract a preview frame as an image attachment, and (when the session is
    // server-linked) offer uploading it with kind "video" (contract §8).
    void onChatVideoAttached(const QString& path);
    // The server-storage tail of onChatVideoAttached: when the session is
    // linked and the user confirms, upload the video bytes with kind "video".
    void offerChatVideoUpload(const QString& path);
    // `frame` op support: sequential MediaLoader seeks (blocking on a local
    // event loop), each extracted frame becoming a new project entry.
    bool chatExtractFrames(const QVector<int>& indices, QString* err);
    // §2.1 `save`: persist the working image + layout as a LOCAL project through
    // createLocalProject — the same path the New Project flow uses. A FRESH
    // project per save, so a multi-image plan leaves one project per image;
    // publishing to a server stays a user action. Browser twin: chatSession.js
    // saveProject.
    bool chatSaveProject(const QString& name, const QString& dest, QString* err);
    // Leave incognito and keep the current picture + lines as a LOCAL project. The user's own
    // way out of an incognito session, the local twin of publishIncognitoToServer: S6 keeps
    // incognito from writing anything BY ITSELF, it was never meant to trap what is on screen.
    // Returns the project name, or "" when there is nothing to promote.
    QString promoteIncognitoToLocal(const QString& name = QString());
    // §10 openFile: load a user-named LOCAL file — a .stencil project, a .json layout, or a
    // picture/video — through the paths the Open dialog itself uses. The echo guard already
    // ran in the plan executor.
    bool chatOpenFile(const QString& path, QString* err);
    // Open a source (URL or local path) HERE and block until MediaLoader resolves, so a
    // plan's next action edits the loaded picture instead of racing it. Shared by the
    // assistant's openUrl and openFile ops; *why gets MediaLoader's own reason.
    bool chatLoadSource(const QString& src, bool incognito, QString* why);
    // The name an unnamed §2.1 save gets: the attachment being worked on (file
    // name minus extension), else the editor's own project/image name.
    QString chatSaveBaseName(const QString& requested) const;
    // A free project name: `wanted`, else "wanted 2", "wanted 3"… — a batch of
    // saves routinely wants the same base, and project names are unique.
    QString uniqueLocalProjectName(const QString& wanted) const;
    // Create a project entry from an in-memory image (the create-project-from-
    // image path used for LLM variants + extracted frames). Returns the new id.
    // Loop callers pass deferRegistrySave=true and do one saveProjects +
    // refreshDockMenu themselves after the batch.
    QString addImageProjectEntry(const QImage& img, const QString& baseName,
                                 bool deferRegistrySave = false);

    // Find a loaded project by id, or nullptr when none matches.
    Project* findProject(const std::string& id);

    // Remove ONE local project row, resetting the editor when it is the open
    // one; the caller persists + refreshes after its batch. Shared by the
    // projects dialog's removeRequested handler and the chat removeProject op.
    void eraseLocalProject(const QString& id);
    // Persist settings to disk unless this is an incognito window (which never
    // writes). Centralizes the incognito-gated save used across the toolbar.
    void persistSettings();

    // ── Project name surface (window title + toolbar field). Mirrors the browser's
    // updateProjectTitle + validated inline rename (validateName/nameExists). ──
    // Reflect the active project's name in the window title and the toolbar field.
    void updateProjectTitle();
    // The active project's name, or empty when there is no active saved project.
    QString activeProjectName() const;
    // The name used for downloads/exports: the active project name when there is one,
    // else the image's base name. Keeps the download name in lockstep with the project.
    QString projectBaseName() const;
    // Validate a proposed name against the current project set (uses core::validateName).
    core::ProjectsStore::NameCheck checkProjectName(const QString& name,
                                                    const QString& exceptId) const;
    // Validate + rename a project by id; notifies on rejection. Returns true on success.
    bool renameProjectById(const QString& id, const QString& name);
    // The active project's name colour ("#rrggbb"), or empty when none / theme default.
    QString activeProjectColor() const;
    // The colour of the bound project: the server record for a server session, else the
    // active local project. Ignores incognito (painting callers gate that themselves).
    QString currentProjectColor() const;
    // Pop a colour picker seeded with the active project's colour, then apply it.
    void chooseProjectColor();
    // Browser-like 🎨 popup: with a custom colour set, a menu offering "Choose colour…"
    // (opens the picker) and "Use theme default colour"; with none set the picker opens
    // directly — there is nothing to clear.
    void showProjectColorMenu();
    // Set the ACTIVE editor's project colour (local id or server-linked session):
    // validates ("" = clear, else QColor(str).isValid() → "#rrggbb" lower-case),
    // persists, repaints the name, and pushes UpdateProject{color} for a server project.
    void setActiveProjectColor(const QString& color);
    // Recolour the active BLANK project's solid background (keeps the drawn lines). No-op unless
    // this session is a blank image. Opens a colour picker; persists blank/blankColor.
    void setActiveBlankColor();
    // The dialog-free recolour itself — shared with the assistant's §10
    // blankColor op (ChatPlanTarget).
    void applyBlankColor(const QColor& c);
    // Set a colour on a project BY id (the Projects dialog "Set colour" path). For a
    // server project (serverUrl non-empty) it PUTs UpdateProject{color}; else it
    // updates the local meta + persists. Returns true on success.
    // Server projects PUT asynchronously; local projects resolve synchronously. `done(ok)` (when
    // supplied) fires on completion so callers can repaint the title once the change lands.
    void setProjectColorById(const QString& id, const QString& serverUrl, const QString& color,
                             std::function<void(bool ok)> done = {});
    // Version-guarded server writes (requireClient/putVersionGuarded) now live on RemoteSession
    // (remoteSession_); the server CRUD methods here call through it.
    // Normalise a colour for storage: "" stays "" (clear); a QColor-valid string
    // returns "#rrggbb" lower-case; anything else returns nullopt (reject the set).
    std::optional<QString> normalizeProjectColor(const QString& color) const;
    // Live-update the ✓/✗ visibility + ✓ enabled-state/tooltip as the field is edited.
    void refreshProjectNameButtons();
    void setPaintedOut(QWidget* w, bool out);   // browser `visibility: hidden` — keeps the slot
    void updateNameHover();   // recompute whether the cursor is over the name group (hover-reveal ✎/🎨)
    // Style the name field for its mode: editing shows an accent-outlined input; read-only shows
    // a plain title with NO border/focus ring (browser parity). Keeps the project colour.
    void applyProjectNameStyle(bool editing);
    // A control's tooltip: the description plus the shortcut it carries, in the platform's
    // own notation (tipContent then draws that trailing "(⌘Z)" as a keycap). buildActions'
    // local `tip` helper is this, and refreshActions re-states the ones that flip direction.
    void setActionTip(QAction* a, const QString& desc);
    void enterNameEdit();   // browser-like: switch the read-only name field into edit mode
    void commitProjectName();
    void cancelProjectName();
    void openInfo();
    void openShortcuts();
    void applyHotkeyOverrides(const QHash<QString, QString>& overrides);
    void updateStatusIdle();
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;  // clears the Alt+R rotate-chord flag
    // Drag-and-drop of a file onto the window (image / video / layout JSON),
    // routed through openPathFromOS — the Photoshop-style drop-to-open.
    void dragEnterEvent(QDragEnterEvent* event) override;
    // Track the cursor's half (LEFT save / RIGHT incognito) while dragging + highlight the
    // split drop overlay; hide it when the drag leaves.
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // A drop opening INTO this window arms a canvas reveal; the load funnels
    // (openImageHere / onLaunchImageLoaded) consume it once the pixels are up, so
    // the image visibly arrives where the drop zones just were (browser parity:
    // .canvas-container.drop-landing).
    // Play the arrival for a freshly installed image (see the definition). No longer
    // gated on "was it a drop?" — every picture that appears assembles the same way,
    // which is the browser's rule too (drawingApp.js: any non-in-place load).
    void playImageArrival();
    // First-show fade-in (a gentle window-opacity ramp), mirroring the browser
    // container's appReveal animation. Runs once; later shows are instant.
    void showEvent(QShowEvent* event) override;
    // Persist the window/dock state on close; closing is immediate on every
    // path (no confirmation modal — deliberate user decision).
    void closeEvent(QCloseEvent* event) override;

    // ── core widgets ──
    bool rKeyHeld_ = false;  // R held? gates the Alt+R+←/→ line-rotate chord
    // Which arrow keys (+ Shift) are down, for diagonal keyboard panning (browser parity:
    // controlsBinder.js wireArrowPan's #arrowsHeld). Two keys held deliver as two independent
    // native auto-repeat streams, so combining them happens on our own tick (arrowPanTimer_).
    bool panLeftHeld_ = false;
    bool panRightHeld_ = false;
    bool panUpHeld_ = false;
    bool panDownHeld_ = false;
    bool panShiftHeld_ = false;
    QTimer* arrowPanTimer_ = nullptr;
    CanvasWidget* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    SelectionPanel* selPanel_ = nullptr;
    SelectedLineBar* selectedLineBar_ = nullptr;  // "Selected Line:" bar above the canvas
    // Docked Qt::TopDockWidgetArea, not a QToolBar row: a top/bottom dock area spans the
    // FULL window width the same way a toolbar row does (over the right Points/Lines dock
    // too), but — unlike QToolBarLayout, which sizes an added widget to its own content
    // regardless of size policy — a QDockWidget stretches its ONE content widget to fill
    // its whole allocated area, which selectedLineBar_'s own FlowLayout needs to wrap
    // against the bar's REAL width rather than a narrow, content-sized one. Its ordinary,
    // already-proven show()/hide() (selPanel_ uses the same) also sidesteps the toolbar
    // widget-visibility sync bug the previous attempt hit.
    class QDockWidget* selectedLineDock_ = nullptr;
    class QVBoxLayout* centralLayout_ = nullptr;  // [image-info bar, scroll_]
    // AI-assistant chat dock (dockable on all four sides + free-floating).
    ChatDock* chatDock_ = nullptr;
    // QPointer: notify_ is parented to the scroll viewport and dies with it during
    // teardown, while dock signals (syncToastInset) can still fire — the null check
    // must see a real null, not a dangling raw pointer.
    QPointer<Notifications> notify_;
    CanvasTooltip* tooltip_ = nullptr;
    // Delays the canvas line/point/coords tooltip's reveal (S12), the same wait the
    // toolbar/menu tooltip already has via SH_ToolTip_WakeUpDelay (main.cpp) — see
    // scheduleHoverShow()/hideHoverTooltip().
    QTimer* hoverTooltipTimer_ = nullptr;
    QString hoverPendingKey_;   // target the timer is armed for ("" = none)
    QString hoverShownKey_;     // target CURRENTLY on screen, or about to be mid-timer
    std::function<void()> hoverPendingReveal_;
    IncognitoOverlay* incognitoOverlay_ = nullptr;
    DropZonesOverlay* dropZones_ = nullptr;   // split image-drop overlay (save | incognito)
    ProjectDragZones* projectZones_ = nullptr;  // 3-zone overlay for dragging a project out of the dialog
    QLabel* status_ = nullptr;
    QComboBox* pageSize_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    // Debounced pan/zoom persistence (scheduleViewSave/saveActiveProjectView) — browser
    // parity, storage.js's scroll/zoom save debounce. restoringView_ below guards against
    // re-saving a view that loadProjectIntoCanvas/restoreSession are still applying.
    QTimer* viewSaveTimer_ = nullptr;
    // Canvas scrollbar auto-hide (revealCanvasScrollbars) — see its own declaration above.
    // QPointer, not raw: QWidget::setGraphicsEffect DELETES whatever effect the widget had,
    // so anything that re-installs one on a scrollbar leaves a raw pointer dangling — and
    // the reveal path reads opacity() off it every pan tick (a SIGSEGV in the GUI suite).
    QPointer<QGraphicsOpacityEffect> vScrollOpacity_;
    QPointer<QGraphicsOpacityEffect> hScrollOpacity_;
    QTimer* scrollbarHideTimer_ = nullptr;
    bool scrollbarHovered_ = false;   // pointer is on a bar right now — never auto-hide then
    bool restoringView_ = false;
    // Live co-edit reentrancy flags: true while an async push / reload is in flight (set at the
    // start of saveToServer / openServerProject, cleared by a shared clearer when the whole async
    // chain ends). READ by the RemoteSyncController (passed as const bool*) plus the filter/reload
    // paths here. The timers, LiveFeed, and push-burst/reload-pending bookkeeping live inside
    // RemoteSyncController.
    bool remotePushing_ = false;
    bool remoteReloading_ = false;
    // True when THIS user changed the filter since the last sync — a save then imposes
    // our filter; otherwise a line-only save preserves the shared server filter (so it
    // doesn't clobber a peer's filter change). Cleared on save / reload.
    bool filterDirty_ = false;

    // ── Project-name field (toolbar) + its inline-rename ✓/✗ buttons. Mirrors the
    // browser topbar name field: shows the active project name, validated inline. ──
    QLineEdit* projectName_ = nullptr;
    // Compact "?" beside the name (browser parity): its tooltip carries the two
    // facts that would otherwise be unreadable with the tool rows collapsed —
    // the image size and, while incognito, "Incognito — not saved". Nothing else.
    class QLabel* statusHint_ = nullptr;
    QAction* statusHintAction_ = nullptr;   // its slot in the header row (hides with it)
    bool toolbarsShown_ = true;             // the "?" is the collapsed state's readout

    QToolButton* projectNameEdit_ = nullptr;    // ✎ rename affordance (enters edit mode)
    bool tearingDown_ = false;                  // set in ~MainWindow: ignore late child signals
    bool nameEditing_ = false;                  // true while the name field is in edit mode
    bool nameHover_ = false;                     // cursor is over the name field / ✎ / 🎨 group
    QToolButton* projectNameAccept_ = nullptr;
    QToolButton* projectNameCancel_ = nullptr;
    // Per-project accent swatch next to the name field (browser's color control):
    // its popup chooses a custom name colour or reverts to the theme accent.
    QToolButton* projectColorBtn_ = nullptr;
    QToolButton* blankColorBtn_ = nullptr;   // recolour a blank project's background (blanks only)
    // QToolBar::addWidget wraps each button in a QWidgetAction; show/hide must toggle THESE
    // actions (not just the widgets) or the toolbar ignores it. Used by refreshProjectNameButtons.
    QWidget* nameGroup_ = nullptr;   // field + ✎/🎨/✓/✗ in one hover region
    QAction* blankColorBtnAction_ = nullptr;

    // ── inline toolbar widget groups (S10 custom page, S11 formulas) ──
    // The QWidgetAction handle (…Act_) is toggled, not the widget, so the
    // toolbar re-lays-out and actually makes room for the inputs.
    QWidget* customGroup_ = nullptr;
    QDoubleSpinBox* customW_ = nullptr;
    QDoubleSpinBox* customH_ = nullptr;
    QComboBox* unitCombo_ = nullptr;     // toolbar cm/in switch (mirrors the menu)
    QCheckBox* allowFormulas_ = nullptr;
    QWidget* formulaGroup_ = nullptr;
    QLineEdit* formulaX_ = nullptr;
    QLineEdit* formulaY_ = nullptr;
    QLabel* formulaError_ = nullptr;
    // A formula is typed one character at a time, so the pair commits when typing SETTLES
    // (or on Enter / focus-out), never per keystroke — "(x" and a field cleared to be
    // retyped are states passed through, not values to apply. Mirrors the browser's
    // settingsController.wireFormulaInputs, same delay.
    QTimer* formulaCommitTimer_ = nullptr;
    // Context-menu twins of the toolbar formula controls (the Transformation submenu, mirroring
    // browser contextMenu.js): edits here drive the canonical toolbar widgets above, so the
    // existing validate/apply/persist pipeline runs unchanged. Seeded in syncContextActions.
    QWidgetAction* ctxAllowFormulasAct_ = nullptr;
    QCheckBox* ctxAllowFormulas_ = nullptr;
    QWidgetAction* ctxFormulaXAct_ = nullptr;
    QWidgetAction* ctxFormulaYAct_ = nullptr;
    QLineEdit* ctxFormulaX_ = nullptr;
    QLineEdit* ctxFormulaY_ = nullptr;

    // ── Style toolbar row (S8; browser toolbar.js Image + Line Style + Draw
    // sections ~24-63). Filter combo + tint swatch, default line color/thickness/
    // point/style controls, and the line/rect draw-mode toggle. The toolbar sets
    // canvas DEFAULTS only — selected-line inline editing is owned by the
    // SelectionPanel (Step 10), per the plan's setSelectedLineStyle resolution.
    QToolButton* drawModeBtn_ = nullptr;
    QToolButton* zoomFitBtn_ = nullptr;   // fit-to-window, beside the zoom combo
    // View row: the browser's ☑ Points / ☑ Lines checkboxes, mirroring the menu actions.
    QCheckBox* showPointsCheck_ = nullptr;
    QCheckBox* showLinesCheck_ = nullptr;
    QToolButton* startDrawBtn_ = nullptr;   // Draw toolbar Start button; goes accent while drawing
    QToolButton* lineColorBtn_ = nullptr;
    QToolButton* pointColorBtn_ = nullptr;   // default point colour (toolbar.js #point-color)
    QSpinBox* lineThickness_ = nullptr;
    QSpinBox* pointSize_ = nullptr;
    QComboBox* lineStyle_ = nullptr;
    QComboBox* imageFilter_ = nullptr;
    QComboBox* compareCombo_ = nullptr;   // compare-with-original view selector
    QToolButton* filterColorBtn_ = nullptr;
    class QToolBar* styleToolbar_ = nullptr;  // row two — Draw/View are appended to it
    // IMAGE cluster empty state: the labelled Open button vs the per-image icons.
    class QWidget* imageSection_ = nullptr;
    QSet<class QAction*> dangerIcons_;   // actions whose glyph draws in --danger
    class QToolButton* openImageBtn_ = nullptr;
    QColor lineColorValue_{"#FFFF00"};
    QColor filterColorValue_{"#7c3aed"};

    // ── actions (shared by menu bar, toolbar, context menu) ──
    QAction* actOpen_ = nullptr;
    // Same handler as actOpen_, but its own row button (browser parity: #open-image-btn,
    // the compact icon shown alongside Save/Copy/Share/Open-in once an image is loaded,
    // vs. #load-image-btn's full "Open Image" button in the empty state).
    QAction* actOpenAnother_ = nullptr;
    QAction* actCrop_ = nullptr;
    QAction* actRotateLeft_ = nullptr;
    QAction* actRotateRight_ = nullptr;
    QAction* actCycleFilter_ = nullptr;
    QAction* actCycleCompare_ = nullptr;   // Alt+O: cycle the compare view
    QAction* actStartDraw_ = nullptr;
    QAction* actStopDraw_ = nullptr;
    QAction* actNewLine_ = nullptr;
    QAction* actUndo_ = nullptr;
    QAction* actRedo_ = nullptr;
    QAction* actDeleteLast_ = nullptr;
    QAction* actDeleteLine_ = nullptr;   // Alt+Delete (⌥⌫ on macOS)
    QAction* actDeletePoint_ = nullptr;  // Alt+Shift+Delete (⌥⇧⌫ on macOS)
    QAction* actClearAll_ = nullptr;
    QAction* actDeselect_ = nullptr;
    QAction* actZoomIn_ = nullptr;
    QAction* actZoomOut_ = nullptr;
    QAction* actFit_ = nullptr;
    QAction* actShowPoints_ = nullptr;
    QAction* actShowLines_ = nullptr;
    QAction* actAllowFormulas_ = nullptr;  // View toggle mirroring allowFormulas_
    QAction* actTooltip_ = nullptr;        // View toggle for the hover tooltip
    QAction* actTheme_ = nullptr;
    QAction* actPanel_ = nullptr;
    QAction* actToolbars_ = nullptr;   // show/hide the top toolbars (browser "Controls" collapse)
    QAction* actFullscreen_ = nullptr;
    class QToolButton* controlsPill_ = nullptr;   // "Controls" chevron pill (kept in the header row)
    qreal pillChevronDeg_ = 0;                    // 0 = ↑ (rows shown), 180 = ↓; animated by spinControlsPill
    QVariantAnimation* pillSpinAnim_ = nullptr;   // in-flight pill-chevron turn
    class QToolBar* headerToolbar_ = nullptr;     // always-visible header row (pill + project name)
    QWidget* settingsSection_ = nullptr;
    QWidget* connectionsSection_ = nullptr;   // built with row one, added to row two (see the .cpp)          // toolbar SETTINGS cluster (browser's last group)
    class QToolButton* panelReopenBtn_ = nullptr; // floating right-edge chevron: re-opens a hidden panel
    // Animated grip over the canvas↔panel separator (support/dockGrip.hpp) + whether a
    // separator drag started on it, so the grip stays hot for the whole drag.
    class DockGripOverlay* panelGrip_ = nullptr;
    bool panelGripDrag_ = false;
    // …and the same pair for the chat dock's resize edge (browser .chat-resizer).
    // chatEdgeHit_ is the separator's real rect: the band is painted thicker than a
    // hairline separator, but it may only light where Qt actually starts a resize.
    class DockEdgeOverlay* chatEdge_ = nullptr;
    QRect chatEdgeHit_;
    bool chatEdgeDrag_ = false;
    class QLabel* imageSizeInfo_ = nullptr;       // "Image Size: W × H px" — hides with the tool rows
    // The styled bar imageSizeInfo_ sits in (buildImageInfoBar), inside imageInfoDock_ below
    // — selectedLineBarDustPoint still reads THIS rect for the dust point's y (height/bottom
    // only, never its x).
    QWidget* imageInfoBar_ = nullptr;
    // imageInfoBar_'s host (adaptive top gap + fixed bottom gap) and the real
    // Qt::TopDockWidgetArea dock it lives in, stacked below selectedLineDock_ so the row
    // spans the full window width above both the canvas and the panel (browser parity:
    // #image-info is a sibling of .main-content, not nested in .canvas-section).
    QWidget* imageInfoHost_ = nullptr;
    class QDockWidget* imageInfoDock_ = nullptr;
    // Font/theme key the reserved info-row height was measured for ("" = not yet).
    QString imageInfoHeightKey_;
    // Drag & drop hint below the canvas (browser .drop-hint parity, mainContent.js) — icon +
    // text kept as separate labels so applyTheme() can re-tint just the rasterised icon.
    QWidget* dropHint_ = nullptr;
    class QLabel* dropHintIcon_ = nullptr;
    class QLabel* dropHintText_ = nullptr;
    class QToolButton* logoBtn_ = nullptr;        // header-row app logo — click cycles the accent (browser parity)
    QTimer* logoClickTimer_ = nullptr;            // defers the single-click cycle so a double-click can pre-empt it
    QAction* actAccent_ = nullptr;                // opens the accent-preset popover (logo's popoverButtons_ entry)
    bool altHeldForTest_ = false;                 // GUI-test stand-in for a held Alt (glide poll only)
    // A press dismissed a popover: the same click must not go on to RE-OPEN it through
    // the icon it landed on (the logo's accent cycle, a popover icon's deferred click).
    bool popoverDismissClick_ = false;
    QWidget* logoFx_ = nullptr;                   // hover pulse/glow/rays overlay (file-local LogoHoverFx —
                                                  // browser animations.css logoPulse parity); runs only while hovered

    // ── Modal popovers (support/popover.hpp) ──
    // Dialog-opening toolbar icons answer a second gesture set: double-click / right-click
    // opens the SAME dialog as a compact frameless popover pinned next to the icon. A plain
    // click still opens the full dialog — deferred one double-click interval (logo pattern)
    // so the blocking exec() can never swallow the second click of a double-click.
    QSet<QAction*> popoverDialogActions_;         // the actions whose buttons get the gestures
    QHash<QObject*, QAction*> popoverButtons_;    // button → its action, for the event filter
    QTimer* popoverClickTimer_ = nullptr;         // the deferred single click (one at a time)
    QPointer<QWidget> popoverAnchor_;             // set right before trigger → popover shape
    // Icon that last opened a dialog (click, menu or shortcut) — the rect
    // support::revealDialog animates the window out of and back into.
    // dialogAnchorRect_ is the MENU row's global rect, used when that icon is
    // hidden (toolbars collapsed) so the window still opens from what was clicked.
    QPointer<QWidget> dialogAnchor_;
    QRect dialogAnchorRect_;
    // The menu row under the cursor/selection right now, recorded on QMenu::hovered —
    // Qt hides the menu BEFORE emitting triggered(), so the row has to be captured while
    // the popup is still up. Cleared one cycle after the menu hides (buildMenus).
    QPointer<QAction> menuRowAction_;
    QRect menuRowRect_;
    QPointer<QAction> popoverPendingAction_;      // the action a deferred click will trigger
    QPointer<QDialog> activePopover_;             // the popover being exec'd (outside-click close)
    QPointer<QWidget> popoverOverlay_;            // the in-window box hosting it
    // A double-click already acted for this press cycle: swallow its trailing
    // RELEASE without re-arming the deferred click (which would toggle a
    // NON-modal target — the chat dock — straight back off). A modal dialog's
    // exec() eats that release itself; this covers the non-blocking targets.
    // Reset on the next press, so a stale flag can never eat a fresh click.
    bool popoverSwallowRelease_ = false;
    // HOLD-to-peek: the action whose popover an Alt+hover opened. Releasing Alt
    // closes exactly that (reject the modal popover / hide the compact chat) and
    // nothing else — a dblclick / right-click open clears this and stays sticky.
    QPointer<QAction> altPeekAction_;
    // HOLD-to-peek for the copy/download-image toolbar buttons' export-options
    // popups — the SAME gesture as popoverButtons_ above, but for a plain QMenu
    // (opened via QMenu::popup(), not act->trigger()'ing a QDialog), so it is kept
    // deliberately independent of the popover machinery rather than shoehorned
    // into it. Set right after popup(); the KeyRelease(Alt) handler closes it
    // unless the cursor has since moved INSIDE it (engaged, same rule as a peeked
    // popover) — the menu's own Alt-hover row preview (exportPreview.hpp,
    // wireExportPreviewHover) then behaves exactly as it does for any other open.
    QPointer<QMenu> altPeekExportMenu_;
    // Alt-GLIDE continuation: the popover icon the cursor landed on while another
    // popover was showing; execMaybePopover rejects the current dialog and opens
    // this one's peek next (the modal loop blocks ordinary hover events).
    QPointer<QAction> altPeekNextAction_;
    QPointer<QToolButton> altPeekNextButton_;
    // Poll that hover-binds a LINGERING window (engaged peek after Alt release):
    // closes it once the cursor leaves, unless it holds typed content.
    QTimer* lingerPoll_ = nullptr;
    // `opener` is the window action this dialog belongs to, so its own shortcut can close it
    // and another window's shortcut can swap to that window (see WindowShortcutSwitch).
    int execMaybePopover(QDialog& dlg, QAction* opener = nullptr);           // exec() — anchored+compact when armed
    // Close the open popover (every dismissal path): reject it, and let the overlay's
    // own collapse animation in execMaybePopover carry it out.
    void dismissPopover();
    // Where the popover is ON SCREEN. It is a child widget of this window, so its own
    // frameGeometry() is not a screen rect; the hosting overlay's is.
    QRect popoverRectGlobal() const;
    // One rule for "a press landed while a popover is open", shared by the app-wide event
    // filter (the presses Qt delivers) and the popover's own poll (the presses it does
    // NOT — a modal exec() makes the platform drop them; see execMaybePopover). `target`
    // is the widget Qt handed the press to, or nullptr when the poll saw it. Returns true
    // only when the press was CONSUMED as a gesture (the logo's peek promote / no-op); a
    // dismissal returns false, so the press travels on exactly as it used to.
    bool handlePopoverPress(class QWidget* target, const QPoint& globalPos,
                            Qt::MouseButton button);
    // Fullscreen restore state: whether the toolbars were shown, and the panel's dock area/visibility
    // before entering fullscreen (fullscreen hides the toolbars + moves the panel to the LEFT).
    bool fsActive_ = false;   // our own fullscreen flag (isFullScreen() is unreliable on macOS)
    // Fullscreen enter/exit motion: the canvas STRETCHES out of the viewport box it
    // had (entering) and MINIMISES back into the smaller one (leaving). Browser
    // parity — the FLIP in js/ui/motion.js played by fullscreenLayer.js. The zoom the
    // user chose is preserved: the ramp only ever ENDS on it.
    // Palette-swap wipe (support/themeSwapOverlay.hpp): what applyTheme() last actually
    // painted, so it can tell a real theme/accent CHANGE (animate) from the boot pass and
    // the many re-applies that resolve to the same palette (don't).
    bool themePainted_ = false;
    bool paintedDark_ = false;
    QString paintedAccent_;
    // Accent hover preview (openAccentPicker): the committed accent to restore on leave,
    // and whether a preview is live. The preview floods the palette like a real change
    // (applyTheme's wipe), so no suppression flag — applyTheme's own in-flight guard
    // (themeSwapping) keeps rapid row-hovers from stacking wipes.
    QString accentPreviewSaved_;
    bool accentPreviewActive_ = false;
    // The wipe currently in flight, if any. Held so a second toggle can be ignored while
    // it plays: the overlay is a snapshot of the window BEFORE the restyle, so re-theming
    // underneath one leaves the new snapshot half-drawn over the old palette — hammering
    // the button was visibly tearing the window. QPointer because the overlay
    // deleteLater()s itself when the animation ends.
    // Whether the not-allowed override cursor is currently pushed, and the toolbar row
    // that asked for it — its ancestors see the same move bubble past (see eventFilter).
    bool blockedCursorOn_ = false;
    QPointer<QWidget> blockedRow_;
    void setBlockedCursor(bool on);
    QPointer<QWidget> themeWipe_;
    bool themeSwapping() const { return !themeWipe_.isNull(); }

    QSize fsZoomFromViewport_;          // viewport size before the show/showNormal
    QVariantAnimation* fsZoomAnim_ = nullptr;
    int fsZoomWaits_ = 0;               // frames spent waiting for the resize to land
    void beginFullscreenZoom();         // capture + schedule
    void startFullscreenZoom();         // run once the new viewport size is in
    bool fsWasToolbars_ = true;
    bool fsWasPanel_ = true;
    QTimer* fsHoverTimer_ = nullptr;   // polls the cursor to edge-reveal toolbars/panel in fullscreen
    // How wide the points panel is before the user has ever dragged the splitter — the
    // browser's --coord-panel-default (css/layout.css).
    static constexpr int kPanelDefaultWidth = 405;
    // Never narrower than this: the coordinate columns turn into "…" below it, and the
    // browser's .coordinates-panel holds the same floor (css min-width: 240px).
    static constexpr int kPanelMinWidth = 240;
    // Remembered panel width for the expand animation; seeded at that default.
    int panelRestoreWidth_ = kPanelDefaultWidth;
    QVariantAnimation* panelAnim_ = nullptr;  // in-flight panel collapse/expand (min==max pinning)
    // Chat-dock slide (setChatShown): the in-flight animation, the extent to
    // reopen at (remembered just before each hide, like panelRestoreWidth_),
    // and the dock's OWN minimum size — captured at construction, before any
    // clamping, so the finish step restores it instead of releasing to 0.
    QVariantAnimation* chatAnim_ = nullptr;
    int chatRestoreExtent_ = 0;
    QSize chatNaturalMin_;
    // The real dock stays invisible behind its own dust flight (setChatShown) — the
    // motes ARE the panel forming or leaving, not a decoration over an already-visible
    // slide. Tracked so stopChatAnim() can hand the dock straight back if the flight
    // is interrupted; QPointer because setGraphicsEffect(nullptr) deletes it.
    QPointer<QGraphicsOpacityEffect> chatVeil_;
    // The same veil for the points panel's own flight (panelSurfaceFlight); released by
    // setPanelShown's finish step and by any superseding toggle.
    QPointer<QGraphicsOpacityEffect> panelVeil_;
    // Settings key of a model that rejected images ("multimodal not
    // supported"): while the current provider/model still matches, the working
    // image is NOT auto-attached — every turn would 400 otherwise. Reset by
    // switching model/provider (the key stops matching). Session-only.
    QString chatTextOnlyKey_;
    // §10 clearChat requested by this turn's plan; consumed at chatTurnSettled.
    bool chatClearPending_ = false;
    // openChatCompact tore the dock off as the icon's POPOVER (browser
    // chatPanel compactPopover parity): that shape is transient — the next
    // setChatShown(true) re-docks to the area it displaced instead of reopening
    // the tiny float. Cleared when the user adopts a layout deliberately
    // (title-bar drag, dock buttons, dropping into an area).
    bool chatCompactPopover_ = false;
    bool chatClosing_ = false;          // the dock is mid-slide/flight OUT
    // What the DOCK actually displayed, in order — the panel is built lazily and
    // replays THIS, never chatHistory_. The two are deliberately different: the
    // history is what the MODEL sees (it carries the §7 continuation note and
    // every interim round's reply), and none of that is a message to a user.
    struct MirrorRow {
      QString role;
      QString text;
      QString retryText;
      QStringList notes;   // warnings / executor notes shown inside this card
      bool muted = false;
    };
    QVector<MirrorRow> chatMirrorLog_;
    Qt::DockWidgetArea chatCompactPrevArea_ = Qt::LeftDockWidgetArea;
    // The deliberate (title-bar Float button) float's own rect, remembered for the
    // session — browser chatPanel.js floatRect/FLOAT_DEFAULT parity: a fixed corner
    // the FIRST float lands at, not the toggle icon (openChatCompact's icon-anchored
    // popoverRect is a DIFFERENT gesture, kept separate). Invalid until the dock has
    // floated at least once; captured just before it leaves that shape (toggleChatFloat,
    // dockChatTo's wasFloating branch), so dragging it and toggling dock/float again
    // comes back where it was left, same as the browser's session-persisted floatRect.
    QRect chatFloatRect_;
    // Where a FRESH float lands (browser FLOAT_DEFAULT, ported to this window's own
    // top-left instead of the viewport's — desktop has no single shared viewport
    // origin): a fixed inset, clamped to the window's own screen. Never the toggle
    // icon — see chatFloatRect_.
    QRect defaultChatFloatRect() const;
    // True while the chat shows AS that popover (floating + unadopted): the
    // shape the mini-window rules apply to — a docked panel or a user-adopted
    // float is never popover-dismissed.
    bool chatCompactShowing() const;
    QVariantAnimation* barsAnim_ = nullptr;   // in-flight toolbars collapse/expand
    // Fullscreen edge-hover target states: track the INTENDED reveal, not live isVisible(), so an
    // in-flight hide slide (widget stays visible until it finishes) isn't restarted every poll tick.
    bool fsBarsShown_ = false;
    bool fsPanelShown_ = false;
    QAction* actSettings_ = nullptr;
    QAction* actProjects_ = nullptr;
    QAction* actConnect_ = nullptr;
    QAction* actLinks_ = nullptr;
    QAction* actDescription_ = nullptr;   // the saved project's description (dialogs/descriptionDialog)
    QAction* actKeywords_ = nullptr;      // …and its search keywords (dialogs/keywordsDialog)
    QAction* actNewProject_ = nullptr;
    QAction* actSaveProject_ = nullptr;
    QAction* actClearProject_ = nullptr;  // trash: clear (remove) the current project/editor; hidden for server projects
    QAction* actRenameProject_ = nullptr;  // inline rename of the toolbar project name (browser parity: the ✎)
    QAction* actProjectColor_ = nullptr;       // Project menu: pick the active project's name colour
    QAction* actProjectColorClear_ = nullptr;  // Project menu: revert it to the theme default
    QAction* actSaveSession_ = nullptr;
    QAction* actInfo_ = nullptr;
    QAction* actIncognito_ = nullptr;
    QAction* actShortcuts_ = nullptr;
    QAction* actContextMenu_ = nullptr;   // Shift+F10: the canvas context menu from the keyboard (browser parity)
    QAction* actOpenIn_ = nullptr;   // "Open In…" (browser / Telegram) — see openInAnotherApp
    QAction* actChat_ = nullptr;     // AI Assistant chat dock toggle (checkable)
    QAction* actAssistantSettings_ = nullptr;   // the chat's … ▸ Settings dialog, on its own chord
    QAction* actQuit_ = nullptr;

    // ── Data actions (S9; browser toolbar.js Image/Layout buttons + the paste
    // listener). Layout JSON export/import + clipboard, image save/copy/paste.
    QAction* actDownloadJson_ = nullptr;
    QAction* actUploadJson_ = nullptr;
    QAction* actSaveProjectFile_ = nullptr;
    QAction* actOpenProjectFile_ = nullptr;
    QAction* actDeleteProjectFile_ = nullptr;   // delete the linked .stencil file (enabled only when linked)
    QAction* actCopyLayout_ = nullptr;
    QAction* actPasteLayout_ = nullptr;
    // Image copy/download variants (browser parity: exportService.js renderExportCanvas
    // variants). actSaveImage_/actCopyImage_ are the PRIMARY gesture actions (toolbar
    // buttons, Ctrl+Shift+D/Ctrl+C), reused verbatim in the context-menu submenus and the
    // toolbar buttons' own options popups. They ALWAYS mean "current" (tint + lines/points);
    // actSaveImageSplit_/actCopyImageSplit_ are the separate "With Compare" action, visible
    // only while a split compare view is active. Only one of a pair ever holds the real
    // shortcut at a time (Qt disallows ambiguous shortcuts) — syncSplitCopyDownloadSlot()
    // moves it between them as compare toggles.
    QAction* actSaveImage_ = nullptr;            // "current" — Ctrl+Shift+D outside compare
    QAction* actSaveImageSplit_ = nullptr;       // "split" — "With Compare", shown only while comparing
    QAction* actSaveImageOriginal_ = nullptr;    // "original" (no tint, no lines/points)
    QAction* actSaveImageTint_ = nullptr;        // "tint"     (tint only, no lines/points)
    QAction* actCopyImage_ = nullptr;            // "current" — Ctrl+C outside compare
    QAction* actCopyImageSplit_ = nullptr;       // "split" — "With Compare", shown only while comparing
    QAction* actCopyImageOriginal_ = nullptr;    // "original"
    QAction* actCopyImageTint_ = nullptr;        // "tint"
    // "Current"'s OWN row inside the three menus — a SEPARATE action from
    // actSaveImage_/actCopyImage_ above, which stay the toolbar buttons' real actions and
    // so can't be hidden without hiding the toolbar icon itself. "Current" is meaningless
    // with nothing drawn, so its row needs its own hasLines gate (browser parity:
    // exportOptionsMenu.js / contextMenu.js). No shortcut of its own: syncSplitCopyDownloadSlot()
    // mirrors whichever combo is live on actCopyImage_/actSaveImage_ into this row's TEXT
    // (manual "\t"+combo hint) rather than a real QAction::shortcut(), which would conflict
    // with the toolbar's own action.
    QAction* actSaveImageCurrentRow_ = nullptr;
    QAction* actCopyImageCurrentRow_ = nullptr;
    QAction* actShareImage_ = nullptr;   // native OS share sheet (support/shareImage.hpp)
    QAction* actPasteImage_ = nullptr;
    // The two toolbar buttons' own dust-animated options popups (double-click / right-click
    // opens; Alt+hover on a row previews it — support/exportPreview.hpp). Built once, reused.
    QMenu* copyImageOptionsMenu_ = nullptr;
    QMenu* saveImageOptionsMenu_ = nullptr;

    // ── Context-menu submenu actions (S11; browser/js/ui/contextMenu.js). These
    // persistent actions/QWidgetActions are owned by `this` and reused on every
    // right-click so their checked/enabled/visible state stays live. The toolbar
    // and menu bar keep their own shared QActions; these cover the bits the
    // context menu adds on top (instant line/rect, the Style/Filter/Tooltip
    // submenus).

    // Instant line (contextMenu.js:ctx-draw-line): line mode + begin drawing
    // immediately. Siblings with actDrawRectNow_ below — same action, other mode.
    QAction* actDrawLineNow_ = nullptr;
    // Instant rectangle (contextMenu.js:ctx-draw-rect): rect mode + begin
    // drawing immediately.
    QAction* actDrawRectNow_ = nullptr;

    // Style submenu (contextMenu.js:39-57): point/thickness spinboxes hosted in
    // QWidgetActions + an exclusive line-style radio group. Drive canvas defaults.
    QActionGroup* lineStyleGroup_ = nullptr;
    QAction* actStyleSolid_ = nullptr;
    QAction* actStyleDashed_ = nullptr;
    QAction* actStyleDotted_ = nullptr;
    QWidgetAction* pointSizeAction_ = nullptr;
    QWidgetAction* thicknessAction_ = nullptr;
    QSpinBox* pointSpin_ = nullptr;
    QSpinBox* thickSpin_ = nullptr;

    // Context-menu sub-labels (browser parity: contextMenu.js's .ctx-sub-label —
    // "Image", "Layout (JSON)", "Line Style", "Filter", "Coordinate Formulas", "Show
    // in Tooltip"). Plain muted caption text, NOT QMenu::addSection() — a section is
    // a separator with a label, and the browser's caption has no line of its own; the
    // theme's QSS separator rule painted one anyway since addSection()'s QAction is
    // still isSeparator()==true. Built once in buildContextActions(), re-added to the
    // fresh menu on every right-click like pointSizeAction_ above.
    QWidgetAction* secImageAct_ = nullptr;
    QWidgetAction* secLayoutJsonAct_ = nullptr;
    QWidgetAction* secLineStyleAct_ = nullptr;
    QWidgetAction* secFilterAct_ = nullptr;
    QWidgetAction* secCoordFormulasAct_ = nullptr;
    QWidgetAction* secShowInTooltipAct_ = nullptr;

    // Image Filter submenu (contextMenu.js:59-74): the filter options are hosted QRadioButtons
    // (exclusive QButtonGroup) so picking one keeps the menu open — like the browser's inline
    // radios — instead of a plain QAction that dismisses it. actFilterX_ are their QWidgetActions.
    QButtonGroup* filterButtons_ = nullptr;
    // Compare-with-original radio set (View → Compare), synced with compareCombo_.
    QActionGroup* compareGroup_ = nullptr;
    QWidgetAction* actFilterNone_ = nullptr;
    QWidgetAction* actFilterBW_ = nullptr;
    QWidgetAction* actFilterSepia_ = nullptr;
    QWidgetAction* actFilterInvert_ = nullptr;
    QWidgetAction* actFilterContour_ = nullptr;
    QWidgetAction* actFilterCustom_ = nullptr;
    QAction* tintColorAction_ = nullptr;

    // Tooltip submenu (contextMenu.js:96-107): the enable toggle + per-row visibility
    // toggles, hosted as real QCheckBoxes in QWidgetActions so a click flips them WITHOUT
    // closing the menu (like the point/thickness spinbox rows) and they render as proper
    // checkboxes rather than the action's icon. actTooltip_ stays a plain QAction for the
    // View menu; tooltipEnableCheck_ mirrors it inside the context menu.
    QWidgetAction* actTooltipEnable_ = nullptr;
    QCheckBox* tooltipEnableCheck_ = nullptr;
    QWidgetAction* actTtPage_ = nullptr;
    QWidgetAction* actTtScreen_ = nullptr;
    QWidgetAction* actTtCoords_ = nullptr;
    QCheckBox* ttPageCheck_ = nullptr;
    QCheckBox* ttScreenCheck_ = nullptr;
    QCheckBox* ttCoordsCheck_ = nullptr;
    // Per-row visibility is the single source of truth in settings_ (persisted like the
    // enable toggle): settings_.tooltipShowPage / tooltipShowScreen / tooltipShowCoords.

    // Units submenu (View ▸ Units): cm | inches, persisted via settings_.units.
    QAction* actUnitCm_ = nullptr;
    QAction* actUnitIn_ = nullptr;

    // ── hotkeys (S13: defaults + user overrides, live re-apply) ──
    QHash<QString, QString> hotkeys_;
    QHash<QString, QString> hotkeyDefaults_;
    QHash<QString, QString> hotkeyLabels_;
    QStringList hotkeyOrder_;   // ids in hotkeysConfig.json order (the shortcuts list order)
    QHash<QString, QAction*> hotkeyActions_;

    // ── state ──
    Settings settings_;
    // core::FormulaParser's validate/apply are static (stateless), called inline where needed;
    // no per-window instance is kept.
    core::ProjectsStore projectsStore_;
    std::vector<Project> projectList_;
    QString activeProjectId_;
    // Collaboration-server connections for this window (lazily created). Owns the
    // REST clients; shared projects are listed through it.
    stencil::net::ConnectionManager* connections_ = nullptr;
    // Server-project session domain (remoteSession.hpp): the current session's remote-link state
    // (empty address = a purely-local project) + the ConnectionManager handle + the version-guarded
    // write helpers. Set when a server project is opened/created; drives saveToActiveProject() to
    // write back via saveToServer(). MainWindow reaches the link fields through remoteSession_->link()
    // and the RemoteSyncController composes remoteSession_ directly. QObject child of this window.
    RemoteSession* remoteSession_ = nullptr;
    // Provenance of the image currently on the canvas (the image/video's own URL
    // and the page it came from). Set by loadImageByUrl(); cleared on a plain local
    // open / blank image. Folded into the project meta on create/save.
    QString currentSource_;
    QString currentResource_;

    // Raw encoded bytes of the ORIGINAL image (empty ⇒ none), retained so a .stencil bundle
    // embeds the untouched source (lossless) rather than a PNG re-encode. Set on a file/.stencil
    // load, cleared on a synthetic original (blank / clipboard / video frame).
    QByteArray sourceBytes_;
    QString sourceExt_;
    void setSourceBytes(const QByteArray& bytes, const QString& ext);
    void retainSourceFromFile(const QString& path);   // read + retain a local image file's bytes

    // ── .stencil live-sync state (see the openProjectFile/live-sync methods above) ──
    QString stencilLink_;                            // linked .stencil path ("" = not linked)
    QByteArray stencilBaseline_;                     // bytes we last wrote/read (the sync ancestor)
    bool stencilLiveSync_ = false;                   // the opt-in toggle (per session)
    bool stencilApplying_ = false;                   // guards auto-save while applying an external change
    QFileSystemWatcher* stencilWatcher_ = nullptr;   // watches stencilLink_ for external edits
    QTimer* stencilAutosaveTimer_ = nullptr;         // debounces auto-save on edit
    QAction* actStencilLiveSync_ = nullptr;          // Data-menu toggle
    // Active session's blank-fill colour ("#rrggbb"), or "" for an ordinary image project. Set by
    // createBlankImageFromDialog, restored on open, folded into the project meta (blank/blankColor),
    // and recoloured in place by setActiveBlankColor. Non-empty ⇔ a blank project.
    QString blankColor_;
    // Pending provenance for an in-flight loadImageByUrl(), promoted to current* in
    // onLaunchImageLoaded() once the async load succeeds.
    QString pendingProvSource_;
    QString pendingProvResource_;
    // The Open dialog's "Save to" server for the image now loading (consumed once by
    // adoptCanvasAsLocalProject; empty = this computer).
    QString pendingServerTarget_;
    // Pending quick pre-load crop for an in-flight load (set by openLinks, consumed
    // once in onLaunchImageLoaded). Mirrors the browser linksModal load opts: Auto =
    // the default page-aspect auto-crop; Page = crop to `page` in `album`/portrait
    // orientation; None = load the full frame uncropped.
    struct QuickCropOpts {
      enum class Mode { Auto, Page, None };
      Mode mode = Mode::Auto;
      bool album = false;
      QString page;  // canonical format name ("A3"/"B5"…; empty keeps the current page)
    };
    QuickCropOpts pendingCrop_;
    bool incognito_ = false;
    // NaN until the cursor actually hovers the canvas — the coord readout shows
    // nothing before a real hover, and clears again when the cursor leaves
    // (CanvasWidget::canvasLeft re-arms it); the unit/page refreshers that replay
    // onHovered(lastHoverX_, lastHoverY_) then keep the bar empty instead of
    // resurrecting stale numbers.
    double lastHoverX_ = std::numeric_limits<double>::quiet_NaN();
    double lastHoverY_ = std::numeric_limits<double>::quiet_NaN();
    // The text color the toolbar/menu icons were last rasterized in (set by
    // styleActionIcons). Lets live handlers re-icon a widget in the current theme
    // color without recomputing the palette (e.g. the draw-mode toggle).
    QColor iconColor_{Qt::black};
    // Guards the one-shot first-show fade (see showEvent).
    bool firstShow_ = true;

    // ── AI-assistant state ──
    stencil::llm::QtLlmTransport* llmTransport_ = nullptr;  // QObject child of this window
    std::unique_ptr<stencil::llm::LlmClient> llmClient_;
    // Last reachability probe (refreshLlmStatus), reused within a short TTL so
    // reopening the context menu doesn't re-hit the endpoint on every
    // right-click (browser chatSession cacheProbe parity). Keyed by the
    // effective settings, so a config change re-probes immediately.
    QString llmProbeKey_;
    qint64 llmProbeAt_ = 0;
    stencil::llm::LlmProbeResult llmProbeCache_;
    // Client-side chat history, bounded to the most recent 32 messages and
    // replayed in full on every call (contract §7).
    QString chatLastPrompt_;   // the turn in flight, for a stopped card's Retry
    QVector<stencil::llm::ChatMessage> chatHistory_;
    // This turn's user attachments, kept past the send so an EDITING plan can adopt one
    // as the working image when the canvas is empty (onChatReply, browser parity).
    QList<QImage> chatTurnAttachments_;
    QStringList chatTurnAttachmentNames_;
    // §2.1: which attachment the plan is working on (1-based; 0 = none), so an
    // unnamed `save` is named after it. Set to the sole attachment when the turn
    // has exactly one, then moved by every `image` op.
    int chatActiveAttachment_ = 0;
    // The chat's current video input ("" = none); gates the `frame` op.
    QString chatVideoPath_;
    int chatVideoFrames_ = 0;            // estimated frame count (0 = unknown)
    MediaLoader* chatMedia_ = nullptr;   // dedicated loader for chat frame extraction
    // Encoded working-image cache: turns whose rendered pixels are unchanged
    // (digest match) reuse the previous downscale + PNG + base64.
    QByteArray chatImageDigest_;
    stencil::llm::ChatImage chatImageEncoded_;
    // §7 edge map (contour render of the snapshot): wire-only for the current
    // turn — never pushed to chatHistory_, so never replayed or persisted.
    stencil::llm::ChatImage chatEdgeMapEncoded_;
    // Lazily created bottom-left toast (ChatToast in the .cpp; objectName
    // "chatToast") for completions landing while the dock is hidden.
    QWidget* chatToast_ = nullptr;
    // Canvas context-menu "Assistant ▸" submenu chat (ChatMenuPanel in the .cpp;
    // objectName "chatMenuPanel"). The ACTION is parented to the window, not to
    // the menu, so it outlives the per-right-click menu rebuild and keeps its
    // transcript; the two widget pointers are the StayOpenMenu interactive area
    // and its key target.
    QWidgetAction* chatMenuAction_ = nullptr;
    QWidget* chatMenuPanel_ = nullptr;
    QWidget* chatMenuInput_ = nullptr;
    // Drag dock zones overlay (DockZonesOverlay in the .cpp; objectName
    // "chatDockZones"), shown while the floating chat dock is title-dragged.
    QWidget* dockZones_ = nullptr;
    // Set by onChatStop; makes the next (canceled) reply render as "Stopped.".
    bool chatStopRequested_ = false;
    bool chatContinued_ = false;   // §7: at most ONE auto-continuation per user turn
    // Round-1 reply held back while its §7 continuation may still fire: ONE final
    // bubble folds the stash into the last round's reply (browser parity);
    // flushHeldChatReply posts it as-is if the continuation never launches.
    bool chatReplyHeld_ = false;
    QString chatHeldReply_;
    QStringList chatHeldWarnings_;
    QStringList chatHeldNotes_;

    // ── launch options (CLI) ──
    // Async resolver for --src (image / URL / video frame); created on first use.
    MediaLoader* mediaLoader_ = nullptr;
    // Layout/image export + clipboard IO (dataExportController.hpp). Non-QObject helper owned by
    // value-semantics; constructed in the ctor once canvas_/notify_ exist.
    std::unique_ptr<DataExportController> dataExport_;
    // Live co-edit push/pull engine (remoteSyncController.hpp) — QObject owning the sync timers +
    // LiveFeed; reads MainWindow's remote-link state + flags via hooks.
    std::unique_ptr<RemoteSyncController> remoteSync_;
    // Local↔server project transfer service (projectTransferController.hpp).
    std::unique_ptr<ProjectTransferController> projectTransfer_;
    // A --layout source held until the --src image has loaded, then applied once.
    QString pendingLaunchLayout_;
    // Inline layout JSON from a stencil:// deep link, applied once the src image
    // has loaded (the in-URL sibling of pendingLaunchLayout_).
    QString pendingLaunchLayoutJson_;

    // macOS Dock menu, shared across all windows (last setAsDockMenu wins, so a
    // single app-lifetime menu avoids dangling when a window closes). Owned by the
    // app, not any window. Unused off macOS.
    static QMenu* sDockMenu_;
  };

}
