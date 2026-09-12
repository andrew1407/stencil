#pragma once
#include "chatDock.hpp"  // ChatDock::ProviderStatus (chatMirrorProviderStatus)
#include "fullscreenController.hpp"
#include "popoverHost.hpp"
#include "projectNameBar.hpp"
#include "sessionController.hpp"
#include "unitsController.hpp"
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

class MainWindowGuiTest;  // QtTest e2e (tests/mainWindow.<area>.gui.cpp)
class QScrollBar;

// Top-level window. Mirrors browser/js/ui/layout.js + toolbar.js + the DrawingApp
// wiring: a toolbar of actions, the canvas centred, a status bar of px + page (cm) coords.
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
    // restoreLast=false skips reloading the last autosaved session: a "New Incognito
    // Editor" window (and an incognito launch) begins blank.
    explicit MainWindow(QWidget* parent = nullptr, bool restoreLast = true);
    // Out-of-line so unique_ptr members of forward-declared types are destroyed where
    // their complete type is visible.
    ~MainWindow() override;

    // Desktop counterpart of the browser's URL deep-links (gui/launchOptions.hpp). Called
    // from main() AFTER show(), so async image/video/network resolution runs on the loop.
    void applyLaunchOptions(const LaunchOptions& opts);

    // OS-shell open ("Open With", file association, drag). Sniffs the suffix: *.json →
    // layout, else image/video → --src. `frame` selects the video frame (0 = first).
    void openPathFromOS(const QString& path, int frame = 0);

    // stencil:// deep link from the OS (launchOptions parseStencilUrl): a server-project
    // reference connects + opens, inline src/layout load like --src/--layout.
    void openStencilUrl(const QUrl& url);

   private slots:
    // The single Open entry (File ▸ Open / top-left toolbar), browser openImageModal.js:
    // a local file, a web URL, or a new blank. newBlankImage opens it in blank mode.
    void openImage();
    void newBlankImage();
    // Browser cropModal.js. Confirms before discarding lines when the orientation flips;
    // the original image is never replaced.
    void openCropDialog();
    void onHovered(double imageX, double imageY);
    void refreshActions();
    void onCanvasChanged();
    void onSelectionChanged();
    void onPageSizeChanged();
    void validateAndApplyFormulas();
    // Push the toolbar row's default visuals to the canvas and persist them. Browser twin:
    // drawingApp.js lineColor/lineThickness/pointSize/lineStyle handlers.
    void onLineStyleControlChanged();
    // Single source of truth for the filter controls living in BOTH the toolbar and the
    // context menu: apply + persist + keep the two in sync.
    void applyImageFilter(const QString& mode);
    // Transient: applies to the canvas, then syncs the toolbar combo and the
    // View ▸ Compare radio set.
    void setCompareModeUi(const QString& mode);
    void applyTintColor(const QColor& color);
    void applyLineStyle(const QString& style);
    void updateColorSwatch(QToolButton* btn, const QColor& color);
    // defaultPointColor when set, else the line colour it inherits (core pointColorOr).
    QColor effectiveDefaultPointColor() const;
    void showContextMenu(const QPoint& globalPos);
    // Follows the canvas's split-compare state: shows/hides the "With Compare" actions and
    // moves the real Ctrl+C / Ctrl+Shift+D onto whichever is the primary gesture. Must run
    // after applyTheme too — styleActionIcons() resets the split action's icon otherwise.
    void syncSplitCopyDownloadSlot();
    // Re-sync the persistent context-menu actions to live canvas state. Called as the
    // first statement of showContextMenu().
    void syncContextActions();
    void onHoverDetail(double imageX, double imageY, const QPoint& globalPos,
                       Qt::KeyboardModifiers mods, bool immediate = false);
    // Debounces the reveal by the target hovered (browser tooltip.js scheduleShow);
    // `immediate` skips the wait outright.
    void scheduleHoverShow(const QString& key, std::function<void()> revealFn, bool immediate);
    void hideHoverTooltip();
    // Layout/clipboard/image export live in DataExportController (dataExport_). pasteImage()
    // stays here — it creates a project — and delegates its JSON fallback to pasteLayout().
    void pasteImage();

   private:
    // Catches Escape + focus-out on the project-name field so the user can always leave the edit.
    bool eventFilter(QObject* obj, QEvent* event) override;
    void buildActions();
    // The persistent grouped actions + QWidgetActions the nested right-click menu reuses.
    // Mirrors browser/js/ui/contextMenu.js wire(). Called once, right after buildActions().
    void buildContextActions();
    // The two toolbar buttons' export-options popups. Called once right after
    // buildToolbar() — it needs the buttons, resolved via buttonForAction.
    void wireExportOptionsPopups();
    // Alt+hover live preview (support/exportPreview.hpp) on one export-variant menu.
    void wireExportPreviewHover(QMenu* menu);
    // The ONE row order every surface shows (context menu, Data menu, toolbar popups):
    // split · current-row · the two fixed variants.
    void populateExportVariantMenu(QMenu* menu, bool copy);
    // Export enable/visibility gating + the split-shortcut swap, from live canvas state.
    void syncExportActions();
    // Null when `act` is not one of the export-variant actions.
    QImage exportVariantPreviewImage(QAction* act) const;
    void buildMenus();
    // Qt reads settings_.nativeMenuBar only when the bar is (re)created, so switching it
    // at runtime rebuilds the menus.
    void applyMenuBarPlacement();
    void buildToolbar();
    // The ctor's phases, in exactly this call order: construction order is observable (tab
    // order, sibling stacking, dock placement) and tests/mainWindow.composition.gui.cpp pins it.
    // wireSignals()'s own connect ORDER is preserved verbatim too.
    void loadHotkeys();
    void setupCanvasArea();            // canvas, scroll area, central column, drop hint
    void installWindowFilters();       // the app-wide filters and the shared motion installs
    void setupDocks();                 // the points panel + the "Selected Line:" bar
    void setupChatDock();              // the chat dock and every signal it raises
    void installPanelShimmers();       // hover shimmer over the three panels' own controls
    void setupOverlaysAndStatus();     // toasts, canvas overlays, tooltip, coord readout
    void setupPageAndZoomControls();   // the page-format and zoom combos
    void setupSyncControllers();       // session, live co-edit, project transfer
    void wireSignals();
    // Projects + settings off disk, the saved dock layout, and the live OS-scheme follow.
    // `restoreLast` false skips the autosaved session and the startup auto-connect.
    void restorePersistedState(bool restoreLast);
    // buildToolbar() split into its rows; the call order preserves the
    // addToolBar/addToolBarBreak sequencing that fixes visual row order.
    void buildMainToolbar();
    // Called before the Formula section, which takes formulaGroup_ as one of its row
    // widgets so the pill and the inputs share a baseline.
    void buildFormulaFields();

    // The single wrapping tool row every cluster is appended to (support/wrapRow.hpp).
    class QToolBar* toolRow() const;
    // An uppercase label ABOVE a strip of the actions' buttons (browser .ctrl-section).
    class QWidget* makeToolSection(const QString& title, const QList<class QAction*>& actions,
                                   const QList<class QWidget*>& extras = {},
                                   const QList<class QWidget*>& leading = {});
    void buildProjectNameGroup(class QToolBar* bar);
    void updateImageSizeInfo();
    QString incognitoTagHtml() const;   // divider + themed glyph + accent text
    // Pin the info row to the taller of its two states, so the passive incognito indicator
    // can never reflow the window (canvas, points panel, rows below).
    void reserveImageInfoHeight();
    void syncImageInfoDockHeight();   // locks imageInfoDock_'s height to its content's
    void syncFullscreenGlyph();       // maximize ⇄ minimize with the state
    void markFullscreenBars(bool on);   // deeper bottom band while they hang over the canvas
    QPixmap makeLogoPixmap(int size) const;
    // A popover DIALOG opened via execMaybePopover, so the Alt-peek/glide/release machinery
    // treats it like every other popover. Right-click opens sticky; hold-Alt peeks it.
    void openAccentPicker();
    void remarkAccentPopover();   // move an open popover's ✓ to the settings' accent
    // Browser twin accentController.previewAccent: repaint in a preset INSTANTLY — no theme
    // wipe, no persist — while the pointer rests on a row. A real pick discards the snapshot
    // so the revert never fights the commit.
    void previewAccent(const QString& key);
    void endAccentPreview();
    // Shift+F10: the canvas menu under the pointer while it rests over the viewport, else at
    // the viewport's centre — where the browser puts it.
    void showContextMenuFromKeyboard();
    void buildPageFormulaToolbar();
    void buildStyleToolbar();
    void buildDrawViewToolbar();   // row 4: Draw + View (the rows above are full)
    void buildImageInfoBar();
    QString hotkey(const QString& id, const QString& fallback) const;
    // The canonical value ("A4"/"custom") behind the combo's display label, which carries
    // the physical size.
    QString pageSizeValue() const;
    core::PageSize currentPageDimensions() const;
    core::Point pageCoords(double imageX, double imageY) const;
    // From settings_.units: cm by default, else inches.
    core::UnitFormat unitFormat() const;
    // Centimetres, per-axis px→cm scale, formula- and unit-independent; 0 when nothing is
    // measurable. Browser twin: units.js layoutLineLengthCm.
    double currentLineLengthCm() const;
    // Display-only canvas-derived meta (image px dims + line length in cm) from live editor
    // state. Called wherever the active project entry is captured, so they cannot diverge.
    void stampCanvasMeta(core::ProjectMeta& meta) const;
    void applyUnitToPageInputs();
    // Labels only: values/data stay put, so the selection and handlers are unaffected.
    void applyUnitToPageCombo();
    // The single entry point: persists, syncs both UI surfaces, refreshes every readout.
    void applyUnits(const QString& code);
    // Push settings_.units into the menu actions + toolbar combo; no side effects.
    void syncUnitControls();
    void zoomIn();
    void zoomOut();
    void setZoom(double scale, bool syncCombo = true);
    void fitToWindow();
    void toggleFullscreen();
    void setToolbarsVisible(bool on);   // instantly, no animation
    void fsHoverTick();                 // fullscreen: edge-hover reveal of toolbars/panel
    // Browser parity: a chevron at the RIGHT edge toggles the points panel, one at the TOP the
    // toolbars — each where its menu is, NOT in the top toolbar.
    void buildOverlayArrows();
    void positionOverlayArrows();       // call on resize / state change
    void spinControlsPill(bool animate);  // turn the pill's chevron ↑⇄↓ with the toolbar fold
    void sizeViewToggles();               // re-floor the View checkboxes' width after a restyle
    void syncToastInset();                // keep the toast stack clear of the status bar
    void updatePanelReopenButton();
    void positionPanelReopenButton();   // flush to the canvas' right edge, vertically centred
    void positionPanelGrip();           // canvas↔panel separator grip (dockGrip.hpp)
    void positionChatEdge();            // the chat dock's resize-edge tint (dockGrip.hpp)
    void setPanelShown(bool show, bool animate);
    // Browser mainContent.js: the table pours out past the edge it is docked to, behind a veil
    // (panelVeil_) so the motes ARE the panel, not a cloud over a visible slide. Snapshot taken
    // at `full` either way; null when nothing played.
    QPointer<gui::DisintegrateOverlay> panelSurfaceFlight(bool gather, int ms, int full);
    void releasePanelVeil();
    // The TOOL ROWS fold as one block, so the flight is over their UNION rect, streaming
    // past the window's top edge. No veil — animateBarsHeight stays a pure geometry slide.
    QPointer<gui::DisintegrateOverlay> barsSurfaceFlight(const QList<class QToolBar*>& bars,
                                                         bool gather, int ms);
    // Browser selectionPanel.js surfaceIn/Out, from onSelectionChanged() only on the
    // hidden↔visible edge. In grabs the bar after the dock is shown, Out before it hides.
    void dustSelectedLineBarIn();
    void dustSelectedLineBarOut();
    // `barPicture`'s centre for x, just under the "Image Size:" row for y; falls back to
    // dockAwayPoint off `barPicture` when that row is unmeasurable.
    QPoint selectedLineBarDustPoint(const QRect& barPicture, bool closing);
    // Browser panel parity (~0.34 s in, ~0.26 s out). Slides the DOCKED extent — width for
    // left/right, height for top/bottom — with the same min==max pinning setPanelShown uses.
    // A floating dock is its own window, so it just shows/hides.
    void setChatShown(bool show, bool animate);
    // While a chat flight shares `chatArea` with the points panel, hold the panel at a FIXED
    // width so the slide eats into the CANVAS column only instead of letting Qt redistribute
    // between the two docks. No-op when not sharing; stopChatAnim always releases the pin.
    void pinPanelWhileSharing(Qt::DockWidgetArea chatArea);
    // Only when the panel is visible: splitting against a hidden dock can park it
    // off-screen. Called from wherever either dock's visibility changes. Idempotent.
    void ensurePanelChatSplit();
    // Title-bar buttons, a drag dropped on a dock zone, or toggleChatFloat's float→dock
    // leg; animated via chatSurfaceFlight below.
    void dockChatTo(Qt::DockWidgetArea area);
    // Motes stream out of (or into) the far side of `area`; `gather` true for an arrival. `pin`
    // is the caller's extent setter, and the snapshot is taken at the settled `full` extent
    // either way. Builds chatVeil_ so the real dock stays invisible for the whole flight.
    QPointer<gui::DisintegrateOverlay> chatSurfaceFlight(Qt::DockWidgetArea area, bool gather, int ms,
                                                         const std::function<void(int)>& pin, int full);
    // Width for L/R docks, height for T/B — the `pin` every chat slide hands on.
    std::function<void(int)> chatExtentPin(bool horiz);
    // Animated, unlike QDockWidget::setFloating(): docked leaves through chatSurfaceFlight's
    // dust then flies out of the icon; floating leaves through dismissWindow then re-docks at
    // the last area it held.
    void toggleChatFloat();
    // The chat icon's popover gesture (dblclick / right-click, like every dialog-opening
    // icon): float the dock compact, pinned beside the icon (chatPanel.js compactChatRect).
    // …Now is the second half, once any chat already on screen has animated out.
    void openChatCompact(QWidget* anchor);
    void openChatCompactNow(QWidget* anchor);
    // Placement + the dock's tear-off size; also what tells a re-pin from a real move.
    QRect compactChatRect(QWidget* anchor) const;
    // Browser popover.js altHover parity. Closes a compact chat the glide moved off first.
    void altPeekOpen(class QToolButton* btn, QAction* act);
    // Hover-bind a LINGERING window (an engaged peek after the Alt release).
    void startLingerPoll();
    void stopLingerPoll();
    // Releases the pinned size constraints too (re-entrancy, tear-off mid-slide, teardown).
    void stopChatAnim();
    void setToolbarsShown(bool show, bool animate);
    // Show the "?" only while the tool rows are collapsed and there is something to read.
    void refreshStatusHintVisibility();
    // 0↔natural height, pinning min==max each frame. Used by the pill collapse (tool rows
    // only) AND the fullscreen edge-hover reveal (every row, header included).
    void animateBarsHeight(const QList<class QToolBar*>& bars, bool show);
    void scrollTo(int x, int y);
    void setZoomAnchored(double newScale, const QPoint& cursorInViewport);
    void applyTheme();
    // Re-run from applyTheme() on each light/dark/accent change.
    void styleActionIcons(bool dark, const QColor& iconColor);
    // Destructive actions are FILLED red on the toolbar (browser .danger.btn-icon) and
    // red-on-menu-background in menus, so the button's glyph differs from the action's.
    void styleDangerToolButtons();
    bool sectionButtonVisible(QAction* act, QToolButton* btn) const;
    // support/faceSwap.hpp. The Start/Stop toggle also takes the accent treatment (outlined
    // idle, filled drawing). `animate=false` is the repaint path (theme/accent change),
    // which must land on the end state at once.
    void syncDrawToggleFace(bool drawing, bool animate);
    void syncDrawModeFace(bool rect, bool animate);
    void bindRevealAnchors();
    void bindRevealAnchor(QAction* a);
    QColor toolButtonIconColor(QAction* act, const QColor& normal) const;
    // macOS menu-bar icons follow the SYSTEM appearance, not our theme; reconcile the two
    // when they disagree (support/theme.hpp systemPrefersDark).
    void retintMenuIconsForSystem(bool appDark, const QColor& appIconColor);
    // The visible toolbar button presenting an action (anchors the theme wipe).
    QWidget* buttonForAction(QAction* act) const;
    QHash<QAction*, QString> actionIconNames_;   // action → shared-icon glyph name
    // To the theme TEXT colour, not the app-wide accent, so the hosted checkboxes and radios
    // read like the surrounding menu text.
    void restyleContextToggles(const QColor& textColor);
    void toggleTheme();
    void applySettings(const Settings& s, bool persist);
    void openSettings();
    // The chat gear's assistant-only dialog (browser llmSettingsModal), writing the same llm*
    // keys through the same applySettings path. A named pair, NOT an overload:
    // &MainWindow::openAssistantSettings is taken by address in several connect()s.
    void openAssistantSettings();
    // `anchor` owns the dialog's dust flight. `anchorRect` (GLOBAL) is the fallback for a
    // caller whose own button is ABOUT to be hidden — opening the dialog closes the
    // context-menu mirror first, so that caller captures the rect while it is still visible.
    void openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect = QRect());
    // A persistent QWidgetAction parented to the WINDOW, so the transcript survives the menu
    // being rebuilt on every right-click. Only called while a provider is configured.
    void ensureChatMenuPanel();
    // ONE pipeline (onChatSend/onChatReply, one history, one client, one executor) and two
    // views: these mirror what the dock is told onto the context-menu panel, and are a no-op
    // until that panel exists. No plan or execution logic lives here.
    void chatMirror(const QString& role, const QString& text, bool muted,
                    const QString& retryText = QString(), const QStringList& notes = {},
                    bool configure = false);
    // Fanned out to BOTH views. A non-empty toastError also raises the hidden-dock failure
    // toast ("Assistant failed — <toastError>").
    void chatError(const QString& text, const QString& toastError = QString());
    // Browser "unreachable" kind: an error card with a "Configure provider" CTA instead of
    // chatError's plain Retry.
    void chatUnreachable(const QString& text, const QString& toastError = QString());
    void chatMirrorPending(bool show);  // add / remove the in-flight "…" row
    void chatMirrorStopped(const QString& retryText = QString());  // → a "Stopped." card
    // A dock mid-close counts as hidden: its slide keeps isVisible() true for 260 ms.
    // (No unread mark on the chat icon — the toast opens the chat instead. Browser twin:
    // js/ui/chatPanel.js, same decision.)
    bool chatSurfaceHidden() const;
    void chatRetryTurn(const QString& text);   // the dock signal AND the panel card use it
    void chatMirrorBusy(bool on);       // send button ⇄ stop button
    void chatMirrorClear();
    // Reply text with its warnings folded in, the way the dock renders them.
    static QString withChatWarnings(const QString& text, const QStringList& warnings);
    void chatMirrorLateNote(const QString& text);   // §3.2/§3.1, INTO the last mirrored card
    // Use this, never the dock's appendLateNote alone: a dock-only note leaves the panel a
    // row short, and mirrored as its own row leaves it a row long.
    void chatLateNote(const QString& text);
    void chatNote(const QString& text);   // muted standalone "Note" card on BOTH views
    // refreshLlmStatus: the dock's gear badge and the context-menu panel's carry the same
    // tooltip and dot.
    void chatMirrorProviderStatus(const QString& richTooltip,
                                  ChatDock::ProviderStatus status);
    SessionController::Gates sessionGates() const;
    void scheduleAutosave();
    void saveSessionNow();
    void restoreSession();
    // Browser storage.js parity; touches only the active project's view fields.
    void scheduleViewSave();
    void saveActiveProjectView();
    // Invisible at rest, revealed only by a real pan or zoom — a QGraphicsOpacityEffect per
    // bar, since QSS cannot express "hidden until an unrelated action, then fade out".
    void revealCanvasScrollbars();
    void scheduleScrollbarHide();
    // The floating bar the user sees/drags (overlayScrollArea.hpp); scroll_'s own scrollbars
    // stay the value model.
    QScrollBar* canvasScrollBar(Qt::Orientation o) const;
    void openProjects();
    // Projects-dialog row icons, through the same canvas/export path the editor uses: the
    // active project from the live canvas, the rest composited offscreen. A pathless
    // (in-memory) source gets no preview.
    QHash<QString, QPixmap> buildProjectThumbs() const;
    // Browser connectModal.js. Lazily creates the ConnectionManager.
    void openConnections();
    // Wires changed() to persist the live server set (connectionStore) across relaunch.
    stencil::net::ConnectionManager* ensureConnections();
    // Best-effort, and only when the preference is on. Gated to the primary restored window
    // so spawned windows do not each reconnect.
    void autoConnectServers();
    // Plaintext http to a remote host sends the bearer token + image bytes in the clear.
    void warnInsecureConnections();
    // Browser switchToProject(): page size, image + lines + crop, marked active. animate=false
    // for a REBIND, where the same image stays put and a dust arrival would be a lie.
    bool loadProjectIntoCanvas(const QString& id, bool animate = true);
    // Browser "open in new tab". The new window owns itself (WA_DeleteOnClose) and reads
    // projects from disk.
    void openProjectInNewWindow(const QString& id);
    // Local↔server transfer lives in ProjectTransferController (projectTransfer_).
    // Blocks removing/moving a project open elsewhere — the browser's "open in another
    // tab" guard.
    bool projectOpenInOtherWindow(const QString& id) const;
    // Shows OpenImageDialog and dispatches its outcome (here/new-window/replace/blank).
    void openImageDialog(bool startBlank);
    void createBlankImageFromDialog(const QColor& color, int w, int h);
    void createBlankImage(const QColor& color, int w, int h);   // no confirm — the op-plan path
    // Here: replace this editor's image, saving the current content first unless incognito.
    // NewWindow: a fresh window via applyLaunchOptions (--src/--incognito), this one untouched.
    void openImageHere(const QString& path, bool incognito);
    void openImageInNewWindow(const QString& path, bool incognito);
    // The same two outcomes for a URL / video source, which resolves asynchronously via
    // MediaLoader rather than a synchronous local-image load.
    void openSourceHere(const QString& src, int frame, bool incognito);
    // `crop*` carry the Open-Image dialog's quick-crop into the fresh window, which
    // re-resolves the same source (identical pixels) and re-applies it.
    void openSourceInNewWindow(const QString& src, int frame, bool incognito,
                               bool hasPreview = false, bool cropToPage = false,
                               bool cropAlbum = false,
                               const QString& cropPage = QString());
    // Adopt the dialog's already-decoded preview pixels — no re-fetch or re-seek — honouring
    // its quick-crop choice. `localPath` is the originating local file (kept for saves), empty
    // for a URL/video frame; `provSource` records the URL as provenance.
    void openPreviewedImageHere(const QImage& image, const QString& localPath,
                                const QString& provSource, bool incognito,
                                bool cropToPage, bool cropAlbum,
                                const QString& cropPage);
    // Swap the CURRENT project's image in place, keeping its local id / server link. Server
    // sessions also re-upload the `original`. canReplaceActive() gates the outcome: a
    // saved/linked, non-incognito project must be open.
    bool canReplaceActive() const;
    void replaceProjectImage(const QString& path, bool rename, bool keepAnnotations);
    void replaceServerOriginal(std::function<void()> done = {});
    // Create + upload original + link the session, leave incognito, then push the layout and
    // result. Browser parity.
    void publishIncognitoToServer(const QString& serverUrl);
    bool loadLocalImageReset(const QString& path);   // resets page + provenance
    bool openProjectByName(const QString& name);     // --project; case-insensitive, first match
    // --src support, wired to MediaLoader::loaded: `localPath` non-empty for a local file, so
    // it survives session/project saves. Applies any pending --layout afterwards.
    void onLaunchImageLoaded(const QImage& image, const QString& localPath);
    // Consume pendingCrop_: Page crops to the chosen page+orientation, None takes the full
    // frame, Auto leaves the default page-aspect crop applied at load.
    void applyQuickCrop();
    void applyLayoutFromSource(const QString& src);   // --layout, from a local path or a URL
    // Lazily builds the resolver. Shared by --src, the positional/OS open path, and drag-drop.
    void openImageSource(const QString& src, int frame);
    void ensureMediaLoader();
    // Browser linksModal.js: view/edit/open/remove the active image's provenance and add an
    // image by URL. Edits persist to the active project; a URL load goes via loadImageByUrl().
    void openLinks();
    // The active SAVED project's DESCRIPTION & ATTRIBUTES cluster; both persist to the store.
    void openDescription();
    void openKeywords();
    // Tags the result with `source`/`resource` provenance, so the next project save records it.
    void loadImageByUrl(const QString& source, const QString& resource, int frame);

    // Self-owned top-level windows (WA_DeleteOnClose), so they never depend on the lifetime
    // of the window that triggered them — safe from a long-lived application Dock menu.
    static void openIncognitoWindow();
    static void openProjectsWindow();
    static void openProjectWindowById(const QString& id);
    // New Incognito Editor · Open Projects · recent projects. No-op off macOS; call after
    // project-list changes.
    void refreshDockMenu();
    void newProjectFromCanvas();
    // With ≥1 server connected it first asks for a target (this computer vs which server);
    // otherwise it saves locally.
    void createProject(const QString& name);
    // Persist locally, mark active, refresh, and (when announce) notify. A pathless canvas
    // (blank / remote / video frame) is written to the state dir first, so it keeps its pixels.
    void createLocalProject(const QString& name, bool announce = true, bool fromFile = false);
    // Browser parity: the active editor is always a saved project. No-op while incognito,
    // already bound to a project/server session, or with no image.
    void adoptCanvasAsLocalProject();
    // createProject + upload the original, then link this session so later saves write back.
    // Browser twin: remoteSync.js createRemoteProject.
    void createServerProject(const QString& serverUrl, const QString& name,
                             std::function<void()> onLinked = {});
    void saveToActiveProject();
    // Browser #clear-storage: confirm, then clear the editor back to blank, removing the
    // active LOCAL project first. Hidden for server-linked sessions, so it only ever runs for
    // a local/temporary editor.
    void clearCurrentProject();
    // Drops the image, lines, project binding and provenance — storage.newTemporary().
    void resetToBlankEditor();
    // Version-guarded PUT of name+layout, then upload the rendered result. A 409 surfaces
    // "edited elsewhere" and leaves the link untouched.
    void saveToServer();
    // Downloads original + layout and links the session to {serverUrl, id, version}.
    // `silent` suppresses the "Opened …" toast, for the live-co-edit poll's repeated reloads.
    // link=false adopts content only — browser copyServerProjectToIncognito, for deep links.
    void openServerProject(const QString& serverUrl, const QString& id, bool silent = false,
                           bool link = true);
    // Connect as a user would from the Servers dialog — live connection, else a saved token,
    // else mint one via POST /auth/token — then open project `id`, unlinked when `incognito`.
    void openServerLaunch(const QString& serverUrl, const QString& id, bool incognito);
    // Browser openInModal.js, for the current session.
    void openInAnotherApp();
    // The same hand-off aimed at a projects-list row. A server row (`serverUrl` non-empty)
    // sends only the reference; a local row sends its stored image + layout; the active
    // project falls through to openInAnotherApp() so the live state is used.
    void openInAnotherAppFor(const QString& id, const QString& serverUrl,
                             const QRect& closeRect);
    // What either hand-off gathered. dispatchOpenIn does the rest — the dialog, the Telegram
    // branch, the #stencil= payload, the size gates — so the two differ only in provenance.
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
    // A full envelope (crop + rotation + filter + lines + page/formulas) in the ORIGINAL
    // image's pixel space. Unlike applyLayoutJson, the lines-only import path, this restores
    // crop, rotation and filter too — so no dimension-mismatch prompt and nothing is dropped.
    void loadImageWithLayout(const QImage& img, const QJsonObject& layout,
                             const QByteArray& sourceBytes = {}, const QString& sourceExt = {});

    // image + layout + metadata + optional theme; also the OS-open / drag / file-arg entry
    // for *.stencil (see openPathFromOS).
    void openProjectFile(const QString& path);
    void saveProjectFileAs();
    void deleteProjectFile();   // delete the linked .stencil file from disk (confirm), then unlink

    // Opt-in: auto-save edits back to the linked file and watch it for another client's
    // changes, applying them in place or prompting on conflict. Browser twin: StencilSync.
    // `stencilLink_` empty ⇒ not file-linked.
    QByteArray buildStencilBytes();                 // serialize the current project to .stencil bytes
    void linkStencilFile(const QString& path, const QByteArray& baseline);
    void unlinkStencilFile();                       // drop the link: stop auto-save + watcher, disable the file-linked actions (mirrors StencilSync.unlink)
    void writeStencilNow(const QByteArray& prebuilt = {});  // write to the linked file; reuse prebuilt bytes if given
    void scheduleStencilAutosave();                 // debounced auto-save on edit
    void flushStencilAutosave();
    void onStencilFileChanged(const QByteArray& prebuilt = {});  // watcher: external change → apply / prompt (reuse prebuilt local bytes)
    void applyStencilExternal(const QByteArray& text, bool merge = false);
    void toggleStencilLiveSync(bool on);
    // Page format + x/y formulas from global settings, passed to buildLayoutJson on server
    // save so they round-trip to the browser and peers.
    fileStore::LayoutMeta currentLayoutMeta() const;
    // Only the fields the layout carries, so a reopened server project shows its saved page
    // and a later save re-emits it instead of clobbering with the desktop's global default.
    void adoptServerLayoutMeta(const QJsonObject& layout);
    // Live co-edit push/pull lives in RemoteSyncController (remoteSync_).

    // AI assistant (llm-contract.md). ChatPlanTarget (mainWindow.cpp) adapts the live editor
    // to the plan executor's narrow PlanTarget interface, so it needs the private appliers.
    friend class ChatPlanTarget;
    // The GUI e2e drives private completion paths (mocked chat replies → toast).
    friend class ::MainWindowGuiTest;
    // Token resolver prefers the LIVE connection's token over the saved one.
    void ensureLlmClient();
    // The contract's LlmSettings shape; an empty llmServerUrl resolves to the first
    // configured server connection.
    stencil::llm::LlmSettings currentLlmSettings() const;
    // Recomposes the gear's rich tooltip and, while the dock is open, probes reachability
    // (LlmClient::probe) for its status dot. Called on dock open and after a settings change.
    void refreshLlmStatus();
    // Working image dimensions; whether the input is a video and its frame count.
    QString chatSystemSuffix() const;
    // Append the user turn (attachments downscaled to ≤1568 px, base64), replay the bounded
    // history per the image replay rule, call the provider, then parse + execute the op plan.
    void onChatSend(const QString& text);
    void onChatReply(const stencil::llm::LlmReply& reply);
    // §7 auto-continuation: re-send once when a plan only LOADED an unseen picture.
    bool maybeContinueChat(const stencil::llm::OpPlan& plan);
    // Its plan-shape test, consulted BEFORE the reply renders, so a round-1 bubble a
    // continuation would supersede is held back.
    bool chatPlanLoadsWithoutTracing(const stencil::llm::OpPlan& plan) const;
    // Post a held round-1 bubble whose continuation never fired: no reply is ever lost.
    void flushHeldChatReply();
    // The canceled reply still lands in onChatReply, which renders "Stopped." — no history
    // push, no toast.
    void onChatStop();
    // The dock already emptied the transcript + attachments, so this drops the model-side
    // per-conversation state and (§12.2) the persisted copy when save-chats is on. Provider
    // settings and the working image itself are NOT touched.
    void onChatClear();
    // §10 clearChat is deferred: the op only flags chatClearPending_, and chatTurnSettled —
    // every turn terminal, a turn ending once its plan executed and its reply showed (§3.0) —
    // queues the confirm + clear.
    void chatTurnSettled();
    void runDeferredChatClear();
    // Chat persistence (§12), all gated on settings_.saveChatsWithProject (default off) and
    // never active in incognito. resetChatState is onChatClear minus the persisted-copy
    // deletion, reused by restores, which must not delete what they read.
    void resetChatState();
    QJsonObject buildActiveChatDoc() const;   // the §12.1 document; empty object = no chat
    // To the local project record, or the linked server project's "chat" file kind. Called
    // after each settled turn.
    void persistActiveChat();
    // Seeds chatHistory_ and replays the turns into the dock + mirror. Empty doc = a fresh
    // conversation scope. Never triggers a model call, never writes.
    void restoreChatFromDoc(const QJsonObject& doc);
    void clearPersistedChat();
    // Browser closedToast parity, for turns finishing while the dock is hidden: ~90 chars,
    // click opens the chat. Callers gate on the dock being hidden.
    void showChatToast(const QString& text, bool success);
    // Appends and drops the oldest beyond the 32-message bound.
    void pushChatHistory(const stencil::llm::ChatMessage& m);
    // §7: last 32 messages, and only the current turn's images plus the single most recent
    // prior image ride along.
    QVector<stencil::llm::ChatMessage> wireChatMessages() const;
    // Remembers it as the current video input and extracts a preview frame as an image
    // attachment; a server-linked session is also offered the §8 "video" upload.
    void onChatVideoAttached(const QString& path);
    void offerChatVideoUpload(const QString& path);
    // `frame` op: sequential MediaLoader seeks, blocking on a local event loop, each extracted
    // frame becoming a new project entry.
    bool chatExtractFrames(const QVector<int>& indices, QString* err);
    // §2.1 `save`, through createLocalProject like the New Project flow. A FRESH project per
    // save, so a multi-image plan leaves one project per image; publishing to a server stays a
    // user action. Browser twin: chatSession.js saveProject.
    bool chatSaveProject(const QString& name, const QString& dest, QString* err);
    // The local twin of publishIncognitoToServer: incognito stops the app writing by itself,
    // it does not trap what is on screen. Returns the project name, "" when there is nothing.
    QString promoteIncognitoToLocal(const QString& name = QString());
    // §10 openFile: a .stencil project, a .json layout, or a picture/video, through the paths
    // the Open dialog uses. The echo guard already ran in the plan executor.
    bool chatOpenFile(const QString& path, QString* err);
    // Blocks until MediaLoader resolves, so a plan's next action edits the loaded picture
    // instead of racing it. Shared by the openUrl and openFile ops; *why gets its reason.
    bool chatLoadSource(const QString& src, bool incognito, QString* why);
    // The attachment being worked on (file name minus extension), else the editor's own
    // project/image name.
    QString chatSaveBaseName(const QString& requested) const;
    // `wanted`, else "wanted 2", "wanted 3"… — a batch of saves routinely wants the same
    // base, and project names are unique.
    QString uniqueLocalProjectName(const QString& wanted) const;
    // Used for LLM variants + extracted frames. Loop callers pass deferRegistrySave=true and
    // do one saveProjects + refreshDockMenu themselves after the batch.
    QString addImageProjectEntry(const QImage& img, const QString& baseName,
                                 bool deferRegistrySave = false);

    Project* findProject(const std::string& id);

    // Resets the editor when `id` is the open one; the caller persists + refreshes afterwards.
    void eraseLocalProject(const QString& id);
    void persistSettings();   // no-op in an incognito window, which never writes

    // Project name surface (window title + toolbar field). Browser twin: updateProjectTitle
    // plus the validated inline rename (validateName/nameExists).
    void updateProjectTitle();
    QString activeProjectName() const;
    // The active project name, else the image's base name, so a download stays in lockstep.
    QString projectBaseName() const;
    core::ProjectsStore::NameCheck checkProjectName(const QString& name,
                                                    const QString& exceptId) const;
    bool renameProjectById(const QString& id, const QString& name);   // notifies on rejection
    QString activeProjectColor() const;   // "#rrggbb", empty for none / the theme default
    // The server record for a server session, else the active local project. Ignores
    // incognito; painting callers gate that themselves.
    QString currentProjectColor() const;
    void chooseProjectColor();
    // Browser-like 🎨 popup: with a custom colour set, "Choose colour…" plus "Use theme
    // default colour"; with none set the picker opens directly — there is nothing to clear.
    void showProjectColorMenu();
    // Local id or server-linked session. Validates, persists, repaints the name, and pushes
    // UpdateProject{color} for a server project.
    void setActiveProjectColor(const QString& color);
    // The active BLANK project's solid background, keeping the drawn lines. No-op unless this
    // session is a blank image; persists blank/blankColor.
    void setActiveBlankColor();
    void applyBlankColor(const QColor& c);   // shared with the assistant's §10 blankColor op
    // The Projects dialog "Set colour" path. A server project (serverUrl non-empty) PUTs
    // UpdateProject{color} asynchronously, a local one updates the meta synchronously, so
    // `done(ok)` is what lets callers repaint the title once the change lands.
    void setProjectColorById(const QString& id, const QString& serverUrl, const QString& color,
                             std::function<void(bool ok)> done = {});
    // Version-guarded server writes live on RemoteSession; the CRUD methods here call through it.
    // "" stays "" (clear), a QColor-valid string becomes "#rrggbb" lower-case, anything else is
    // nullopt and rejects the set.
    std::optional<QString> normalizeProjectColor(const QString& color) const;
    void refreshProjectNameButtons();   // ✓/✗ visibility + ✓ enabled state, as the field is edited
    void setPaintedOut(QWidget* w, bool out);   // browser `visibility: hidden` — keeps the slot
    void updateNameHover();   // is the cursor over the name group (hover-reveal ✎/🎨)
    // Editing shows an accent-outlined input; read-only shows a plain title with NO
    // border/focus ring (browser parity). Keeps the project colour either way.
    void applyProjectNameStyle(bool editing);
    // The description plus the shortcut it carries, in the platform's own notation — tipContent
    // then draws that trailing "(⌘Z)" as a keycap. buildActions' local `tip` helper is this,
    // and refreshActions re-states the ones that flip direction.
    void setActionTip(QAction* a, const QString& desc);
    void enterNameEdit();
    void commitProjectName();
    void cancelProjectName();
    void openInfo();
    void openShortcuts();
    void applyHotkeyOverrides(const QHash<QString, QString>& overrides);
    void updateStatusIdle();
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;  // clears the Alt+R rotate-chord flag
    // A file dropped on the window (image / video / layout JSON) routes through
    // openPathFromOS. dragMove tracks the cursor's half (LEFT save / RIGHT incognito) and
    // highlights the split drop overlay.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // Every picture that appears assembles the same way, drop or not — browser drawingApp.js,
    // any non-in-place load (.canvas-container.drop-landing).
    void playImageArrival();
    // First-show window-opacity ramp, the browser container's appReveal. Runs once; later
    // shows are instant.
    void showEvent(QShowEvent* event) override;
    // Persists the window/dock state. Closing is immediate on every path — no confirmation
    // modal, a deliberate decision.
    void closeEvent(QCloseEvent* event) override;

    bool rKeyHeld_ = false;  // R held? gates the Alt+R+←/→ line-rotate chord
    // Browser controlsBinder.js wireArrowPan: two keys held deliver as two independent native
    // auto-repeat streams, so they combine on our own tick.
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
    // A dock area, not a QToolBar row: it stretches its one content widget to the full window
    // width, which selectedLineBar_'s FlowLayout needs to wrap against. QToolBarLayout instead
    // sizes an added widget to its own content.
    class QDockWidget* selectedLineDock_ = nullptr;
    class QVBoxLayout* centralLayout_ = nullptr;  // [image-info bar, scroll_]
    ChatDock* chatDock_ = nullptr;   // dockable on all four sides + free-floating
    // QPointer: notify_ dies with the scroll viewport during teardown while dock signals
    // (syncToastInset) can still fire, so the null check must see a real null.
    QPointer<Notifications> notify_;
    CanvasTooltip* tooltip_ = nullptr;
    // The same wait the toolbar/menu tooltip gets via SH_ToolTip_WakeUpDelay (main.cpp).
    QTimer* hoverTooltipTimer_ = nullptr;
    QString hoverPendingKey_;   // target the timer is armed for ("" = none)
    QString hoverShownKey_;     // target CURRENTLY on screen, or about to be mid-timer
    std::function<void()> hoverPendingReveal_;
    IncognitoOverlay* incognitoOverlay_ = nullptr;
    DropZonesOverlay* dropZones_ = nullptr;   // split image-drop overlay (save | incognito)
    ProjectDragZones* projectZones_ = nullptr;  // 3-zone overlay for dragging a project out of the dialog
    QLabel* status_ = nullptr;
    QComboBox* zoom_ = nullptr;
    // app/sessionController.hpp. Its restoring() guard keeps loadProjectIntoCanvas /
    // restoreSession from re-saving the view they are still applying.
    SessionController session_;
    // QPointer, not raw: QWidget::setGraphicsEffect DELETES the effect the widget had, so
    // re-installing one leaves a raw pointer dangling — and the reveal path reads opacity()
    // off it every pan tick (a SIGSEGV in the GUI suite).
    QPointer<QGraphicsOpacityEffect> vScrollOpacity_;
    QPointer<QGraphicsOpacityEffect> hScrollOpacity_;
    QTimer* scrollbarHideTimer_ = nullptr;
    bool scrollbarHovered_ = false;   // pointer is on a bar right now — never auto-hide then
    // Reentrancy flags, true while an async push/reload is in flight. Read by the
    // RemoteSyncController as const bool*, which owns the timers and LiveFeed too.
    bool remotePushing_ = false;
    bool remoteReloading_ = false;
    // THIS user changed the filter since the last sync, so a save imposes ours; otherwise a
    // line-only save preserves the server's, rather than clobbering a peer's change.
    bool filterDirty_ = false;

    ProjectNameBar nameBar_;   // app/projectNameBar.hpp
    // Its tooltip carries the two facts that would otherwise be unreadable with the tool rows
    // collapsed — the image size and, while incognito, "Incognito — not saved". Nothing else.
    class QLabel* statusHint_ = nullptr;
    QAction* statusHintAction_ = nullptr;   // its slot in the header row (hides with it)
    bool toolbarsShown_ = true;             // the "?" is the collapsed state's readout

    bool tearingDown_ = false;   // set in ~MainWindow: ignore late child signals

    UnitsController units_;   // app/unitsController.hpp
    // The QWidgetAction handle (…Act_) is toggled, not the widget, so the toolbar
    // re-lays-out and makes room for the inputs.
    QCheckBox* allowFormulas_ = nullptr;
    QWidget* formulaGroup_ = nullptr;
    QLineEdit* formulaX_ = nullptr;
    QLineEdit* formulaY_ = nullptr;
    QLabel* formulaError_ = nullptr;
    // The pair commits when typing SETTLES (or on Enter / focus-out), never per keystroke:
    // "(x" and a field cleared to be retyped are states passed through, not values to apply.
    // Browser twin: settingsController.wireFormulaInputs, same delay.
    QTimer* formulaCommitTimer_ = nullptr;
    // Transformation-submenu twins of the toolbar formula controls (browser contextMenu.js):
    // edits here drive the canonical toolbar widgets above, so the validate/apply/persist
    // pipeline runs unchanged. Seeded in syncContextActions.
    QWidgetAction* ctxAllowFormulasAct_ = nullptr;
    QCheckBox* ctxAllowFormulas_ = nullptr;
    QWidgetAction* ctxFormulaXAct_ = nullptr;
    QWidgetAction* ctxFormulaYAct_ = nullptr;
    QLineEdit* ctxFormulaX_ = nullptr;
    QLineEdit* ctxFormulaY_ = nullptr;

    // Browser toolbar.js Image + Line Style + Draw sections. These set canvas DEFAULTS only —
    // selected-line editing belongs to SelectionPanel.
    QToolButton* drawModeBtn_ = nullptr;
    QToolButton* zoomFitBtn_ = nullptr;   // fit-to-window, beside the zoom combo
    // The browser's ☑ Points / ☑ Lines checkboxes, mirroring the menu actions.
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

    // actions (shared by menu bar, toolbar, context menu)
    QAction* actOpen_ = nullptr;
    // actOpen_'s handler on its own row button: browser #open-image-btn, the compact icon
    // shown once an image is loaded, vs #load-image-btn's full button in the empty state.
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
    QWidget* settingsSection_ = nullptr;       // toolbar SETTINGS cluster (browser's last group)
    QWidget* connectionsSection_ = nullptr;   // built with row one, added to row two
    class QToolButton* panelReopenBtn_ = nullptr;   // re-opens a hidden panel
    // support/dockGrip.hpp, plus whether a separator drag started on it — the grip stays hot
    // for the whole drag.
    class DockGripOverlay* panelGrip_ = nullptr;
    bool panelGripDrag_ = false;
    // The same pair for the chat dock's resize edge (browser .chat-resizer). chatEdgeHit_ is
    // the separator's real rect: the band is painted thicker than a hairline separator, but it
    // may only light where Qt actually starts a resize.
    class DockEdgeOverlay* chatEdge_ = nullptr;
    QRect chatEdgeHit_;
    bool chatEdgeDrag_ = false;
    class QLabel* imageSizeInfo_ = nullptr;       // "Image Size: W × H px" — hides with the tool rows
    // selectedLineBarDustPoint reads THIS rect for the dust point's y (height/bottom only).
    QWidget* imageInfoBar_ = nullptr;
    // The bar's host (adaptive top gap + fixed bottom gap) and the Qt::TopDockWidgetArea dock
    // it lives in, stacked below selectedLineDock_ so the row spans the full window width above
    // canvas AND panel — browser #image-info is a sibling of .main-content, not nested in it.
    QWidget* imageInfoHost_ = nullptr;
    class QDockWidget* imageInfoDock_ = nullptr;
    QString imageInfoHeightKey_;   // font/theme key the reserved height was measured for
    // Browser .drop-hint. Icon and text are separate labels so applyTheme() can re-tint the
    // rasterised icon alone.
    QWidget* dropHint_ = nullptr;
    class QLabel* dropHintIcon_ = nullptr;
    class QLabel* dropHintText_ = nullptr;
    class QToolButton* logoBtn_ = nullptr;   // header-row app logo — click cycles the accent
    QTimer* logoClickTimer_ = nullptr;      // defers the cycle so a double-click can pre-empt it
    QAction* actAccent_ = nullptr;          // opens the accent-preset popover
    bool altHeldForTest_ = false;           // GUI-test stand-in for a held Alt (glide poll only)
    QWidget* logoFx_ = nullptr;             // hover pulse/glow/rays (browser animations.css logoPulse)

    PopoverHost pop_;   // app/popoverHost.hpp
    // `opener` is the window action this dialog belongs to, so its own shortcut can close it
    // and another window's shortcut can swap to that window (see WindowShortcutSwitch).
    int execMaybePopover(QDialog& dlg, QAction* opener = nullptr);
    // Rejects it and lets the overlay's own collapse animation carry it out.
    void dismissPopover();
    // The popover is a child widget of this window, so its own frameGeometry() is not a screen
    // rect; the hosting overlay's is.
    QRect popoverRectGlobal() const;
    // ONE rule for "a press landed while a popover is open", shared by the app-wide event filter
    // (the presses Qt delivers) and the popover's own poll (the presses a modal exec() makes the
    // platform drop). `target` is nullptr when the poll saw it. True only when the press was
    // CONSUMED as a gesture; a dismissal returns false, so the press travels on.
    bool handlePopoverPress(class QWidget* target, const QPoint& globalPos,
                            Qt::MouseButton button);
    FullscreenController fs_;   // app/fullscreenController.hpp
    // What applyTheme() last painted, so it can tell a real theme/accent CHANGE (animate) from
    // the boot pass and the re-applies resolving to the same palette.
    bool themePainted_ = false;
    bool paintedDark_ = false;
    QString paintedAccent_;
    // openAccentPicker: the committed accent to restore on leave. The preview floods the
    // palette like a real change, so no suppression flag — applyTheme's own themeSwapping
    // guard keeps rapid row-hovers from stacking wipes.
    QString accentPreviewSaved_;
    bool accentPreviewActive_ = false;
    // The not-allowed override cursor, and the toolbar row that asked for it — its ancestors
    // see the same move bubble past (see eventFilter).
    bool blockedCursorOn_ = false;
    QPointer<QWidget> blockedRow_;
    void setBlockedCursor(bool on);
    // The wipe in flight: a second toggle is ignored while it plays, since the overlay is a
    // snapshot taken BEFORE the restyle and re-theming under it tears the window. QPointer —
    // the overlay deleteLater()s itself when the animation ends.
    QPointer<QWidget> themeWipe_;
    bool themeSwapping() const { return !themeWipe_.isNull(); }

    // Entering, the canvas STRETCHES out of the viewport box it had; leaving it MINIMISES back
    // into the smaller one (js/ui/motion.js's FLIP). The chosen zoom is preserved: the ramp
    // only ever ENDS on it.
    void beginFullscreenZoom();         // capture + schedule
    void startFullscreenZoom();         // run once the new viewport size is in
    // The browser's --coord-panel-default (css/layout.css).
    static constexpr int kPanelDefaultWidth = 405;
    // The coordinate columns turn into "…" below this; browser .coordinates-panel holds the
    // same floor (css min-width: 240px).
    static constexpr int kPanelMinWidth = 240;
    int panelRestoreWidth_ = kPanelDefaultWidth;   // remembered width for the expand animation
    QVariantAnimation* panelAnim_ = nullptr;
    // The extent to reopen at (remembered just before each hide) and the dock's OWN minimum
    // size — captured at construction, before any clamping, so the finish step restores it
    // instead of releasing to 0.
    QVariantAnimation* chatAnim_ = nullptr;
    int chatRestoreExtent_ = 0;
    QSize chatNaturalMin_;
    // The real dock stays invisible behind its own dust flight: the motes ARE the panel forming
    // or leaving. Tracked so stopChatAnim() can hand the dock back if the flight is interrupted;
    // QPointer because setGraphicsEffect(nullptr) deletes it. panelVeil_ is panelSurfaceFlight's.
    QPointer<QGraphicsOpacityEffect> chatVeil_;
    QPointer<QGraphicsOpacityEffect> panelVeil_;
    // Settings key of a model that rejected images: while it still matches, the working image
    // is NOT auto-attached — every turn would 400. Session-only.
    QString chatTextOnlyKey_;
    // §10 clearChat requested by this turn's plan; consumed at chatTurnSettled.
    bool chatClearPending_ = false;
    // Torn off as the icon's POPOVER (browser chatPanel compactPopover): a transient shape, so
    // the next setChatShown(true) re-docks to the area it displaced. Cleared once the user
    // adopts a layout deliberately.
    bool chatCompactPopover_ = false;
    bool chatClosing_ = false;          // the dock is mid-slide/flight OUT
    // What the DOCK displayed, in order: the panel is built lazily and replays THIS, never
    // chatHistory_, which is what the MODEL sees — the §7 continuation note and every interim
    // round's reply, none of it a message to a user.
    struct MirrorRow {
      QString role;
      QString text;
      QString retryText;
      QStringList notes;   // warnings / executor notes shown inside this card
      bool muted = false;
    };
    QVector<MirrorRow> chatMirrorLog_;
    Qt::DockWidgetArea chatCompactPrevArea_ = Qt::LeftDockWidgetArea;
    // The title-bar Float button's rect for the session, NOT openChatCompact's icon-anchored
    // popover. Invalid until the dock has floated once, and captured just before it leaves that
    // shape, so a round-trip comes back where it was left.
    QRect chatFloatRect_;
    // Browser FLOAT_DEFAULT, ported to this window's top-left rather than the viewport's —
    // desktop has no single shared viewport origin. A fixed inset, clamped to its own screen.
    QRect defaultChatFloatRect() const;
    // Floating AND unadopted: the shape the mini-window rules apply to. A docked panel or a
    // user-adopted float is never popover-dismissed.
    bool chatCompactShowing() const;
    QVariantAnimation* barsAnim_ = nullptr;   // in-flight toolbars collapse/expand
    QAction* actSettings_ = nullptr;
    QAction* actProjects_ = nullptr;
    QAction* actConnect_ = nullptr;
    QAction* actLinks_ = nullptr;
    QAction* actDescription_ = nullptr;   // the saved project's description (dialogs/descriptionDialog)
    QAction* actKeywords_ = nullptr;      // …and its search keywords (dialogs/keywordsDialog)
    QAction* actNewProject_ = nullptr;
    QAction* actSaveProject_ = nullptr;
    QAction* actClearProject_ = nullptr;   // hidden for server projects
    QAction* actRenameProject_ = nullptr;  // the ✎ beside the toolbar project name
    QAction* actProjectColor_ = nullptr;
    QAction* actProjectColorClear_ = nullptr;
    QAction* actSaveSession_ = nullptr;
    QAction* actInfo_ = nullptr;
    QAction* actIncognito_ = nullptr;
    QAction* actShortcuts_ = nullptr;
    QAction* actContextMenu_ = nullptr;   // Shift+F10: the canvas context menu from the keyboard (browser parity)
    QAction* actOpenIn_ = nullptr;   // "Open In…" (browser / Telegram) — see openInAnotherApp
    QAction* actChat_ = nullptr;     // AI Assistant chat dock toggle (checkable)
    QAction* actAssistantSettings_ = nullptr;   // the chat's … ▸ Settings dialog, on its own chord
    QAction* actQuit_ = nullptr;

    // Browser toolbar.js Image/Layout buttons and the paste listener.
    QAction* actDownloadJson_ = nullptr;
    QAction* actUploadJson_ = nullptr;
    QAction* actSaveProjectFile_ = nullptr;
    QAction* actOpenProjectFile_ = nullptr;
    QAction* actDeleteProjectFile_ = nullptr;   // delete the linked .stencil file (enabled only when linked)
    QAction* actCopyLayout_ = nullptr;
    QAction* actPasteLayout_ = nullptr;
    // Browser exportService.js variants. actSaveImage_/actCopyImage_ are the PRIMARY gesture
    // actions and ALWAYS mean "current"; the …Split_ pair is the separate "With Compare" action.
    // Only ONE of a pair holds the real shortcut at a time — Qt disallows ambiguous shortcuts.
    QAction* actSaveImage_ = nullptr;            // "current" — Ctrl+Shift+D outside compare
    QAction* actSaveImageSplit_ = nullptr;       // "split" — "With Compare", shown only while comparing
    QAction* actSaveImageOriginal_ = nullptr;    // "original" (no tint, no lines/points)
    QAction* actSaveImageTint_ = nullptr;        // "tint"     (tint only, no lines/points)
    QAction* actCopyImage_ = nullptr;            // "current" — Ctrl+C outside compare
    QAction* actCopyImageSplit_ = nullptr;       // "split" — "With Compare", shown only while comparing
    QAction* actCopyImageOriginal_ = nullptr;    // "original"
    QAction* actCopyImageTint_ = nullptr;        // "tint"
    // "Current"'s own menu row, SEPARATE from the actions above, which cannot be hidden without
    // hiding the toolbar icon; this row has its own hasLines gate. The live combo is mirrored
    // into its TEXT, since a real QAction::shortcut() would conflict with the toolbar's.
    QAction* actSaveImageCurrentRow_ = nullptr;
    QAction* actCopyImageCurrentRow_ = nullptr;
    QAction* actShareImage_ = nullptr;   // native OS share sheet, hidden without one (Linux)
    QAction* actPasteImage_ = nullptr;
    // Double-click / right-click opens; Alt+hover on a row previews it
    // (support/exportPreview.hpp). Built once, reused.
    QMenu* copyImageOptionsMenu_ = nullptr;
    QMenu* saveImageOptionsMenu_ = nullptr;

    // Context-menu submenu actions (browser/js/ui/contextMenu.js), owned by `this` and reused
    // on every right-click so their checked/enabled/visible state stays live. The toolbar and
    // menu bar keep their own shared QActions; these are only what the context menu adds.

    // ctx-draw-line / ctx-draw-rect: set the mode and begin drawing immediately.
    QAction* actDrawLineNow_ = nullptr;
    QAction* actDrawRectNow_ = nullptr;

    // Style submenu: point/thickness spinboxes hosted in QWidgetActions plus an exclusive
    // line-style radio group. They drive canvas defaults.
    QActionGroup* lineStyleGroup_ = nullptr;
    QAction* actStyleSolid_ = nullptr;
    QAction* actStyleDashed_ = nullptr;
    QAction* actStyleDotted_ = nullptr;
    QWidgetAction* pointSizeAction_ = nullptr;
    QWidgetAction* thicknessAction_ = nullptr;
    QSpinBox* pointSpin_ = nullptr;
    QSpinBox* thickSpin_ = nullptr;

    // Browser .ctx-sub-label captions. Plain muted text, NOT QMenu::addSection(): a section is
    // a separator with a label, and the theme's QSS separator rule paints a line the browser's
    // caption does not have (addSection()'s QAction is still isSeparator()==true).
    QWidgetAction* secImageAct_ = nullptr;
    QWidgetAction* secLayoutJsonAct_ = nullptr;
    QWidgetAction* secLineStyleAct_ = nullptr;
    QWidgetAction* secFilterAct_ = nullptr;
    QWidgetAction* secCoordFormulasAct_ = nullptr;
    QWidgetAction* secShowInTooltipAct_ = nullptr;

    // Hosted QRadioButtons in an exclusive QButtonGroup, so picking one keeps the menu open
    // like the browser's inline radios, instead of a plain QAction that dismisses it.
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

    // Real QCheckBoxes in QWidgetActions, so a click flips them WITHOUT closing the menu and
    // they render as checkboxes rather than the action's icon. actTooltip_ stays a plain
    // QAction for the View menu; tooltipEnableCheck_ mirrors it here.
    QWidgetAction* actTooltipEnable_ = nullptr;
    QCheckBox* tooltipEnableCheck_ = nullptr;
    QWidgetAction* actTtPage_ = nullptr;
    QWidgetAction* actTtScreen_ = nullptr;
    QWidgetAction* actTtCoords_ = nullptr;
    QCheckBox* ttPageCheck_ = nullptr;
    QCheckBox* ttScreenCheck_ = nullptr;
    QCheckBox* ttCoordsCheck_ = nullptr;


    // hotkeys (defaults + user overrides, live re-apply)
    QHash<QString, QString> hotkeys_;
    QHash<QString, QString> hotkeyDefaults_;
    QHash<QString, QString> hotkeyLabels_;
    QStringList hotkeyOrder_;   // ids in hotkeysConfig.json order (the shortcuts list order)
    QHash<QString, QAction*> hotkeyActions_;

    Settings settings_;
    core::ProjectsStore projectsStore_;
    std::vector<Project> projectList_;
    QString activeProjectId_;
    // Lazily created. Owns the REST clients; shared projects are listed through it.
    stencil::net::ConnectionManager* connections_ = nullptr;
    // remoteSession.hpp: the session's remote-link state (empty address = purely local), the
    // ConnectionManager handle, and the version-guarded write helpers. Reached through
    // remoteSession_->link() here; RemoteSyncController composes it directly.
    RemoteSession* remoteSession_ = nullptr;
    // The canvas image's own URL and the page it came from. Set by loadImageByUrl(), cleared on
    // a plain local open / blank image, folded into the project meta on create/save.
    QString currentSource_;
    QString currentResource_;

    // Retained so a .stencil bundle embeds the untouched source rather than a PNG re-encode.
    // Cleared on a synthetic original (blank / clipboard / video frame); empty ⇒ none.
    QByteArray sourceBytes_;
    QString sourceExt_;
    void setSourceBytes(const QByteArray& bytes, const QString& ext);
    void retainSourceFromFile(const QString& path);   // read + retain a local image file's bytes

    // .stencil live-sync state (see the openProjectFile/live-sync methods above)
    QString stencilLink_;                            // linked .stencil path ("" = not linked)
    QByteArray stencilBaseline_;                     // bytes we last wrote/read (the sync ancestor)
    bool stencilLiveSync_ = false;                   // the opt-in toggle (per session)
    bool stencilApplying_ = false;                   // guards auto-save while applying an external change
    QFileSystemWatcher* stencilWatcher_ = nullptr;   // watches stencilLink_ for external edits
    QTimer* stencilAutosaveTimer_ = nullptr;         // debounces auto-save on edit
    QAction* actStencilLiveSync_ = nullptr;          // Data-menu toggle
    // The session's blank-fill colour, "" for an ordinary image project; non-empty ⇔ blank.
    // Folded into the project meta as blank/blankColor.
    QString blankColor_;
    // Promoted to current* in onLaunchImageLoaded() once the async load succeeds.
    QString pendingProvSource_;
    QString pendingProvResource_;
    // The Open dialog's "Save to" server, consumed once by adoptCanvasAsLocalProject;
    // empty = this computer.
    QString pendingServerTarget_;
    // Set by openLinks, consumed once in onLaunchImageLoaded. Browser linksModal load opts:
    // Auto = the default page-aspect auto-crop, Page = crop to `page`, None = the full frame.
    struct QuickCropOpts {
      enum class Mode { Auto, Page, None };
      Mode mode = Mode::Auto;
      bool album = false;
      QString page;  // canonical format name ("A3"/"B5"…; empty keeps the current page)
    };
    QuickCropOpts pendingCrop_;
    bool incognito_ = false;
    // NaN until the cursor really hovers the canvas, and NaN again when it leaves: the
    // unit/page refreshers replay onHovered(lastHoverX_, lastHoverY_), so anything else would
    // resurrect stale numbers into an empty readout.
    double lastHoverX_ = std::numeric_limits<double>::quiet_NaN();
    double lastHoverY_ = std::numeric_limits<double>::quiet_NaN();
    // What styleActionIcons last rasterized in, so live handlers can re-icon a widget in the
    // current theme colour without recomputing the palette.
    QColor iconColor_{Qt::black};
    // Guards the one-shot first-show fade (see showEvent).
    bool firstShow_ = true;

    stencil::llm::QtLlmTransport* llmTransport_ = nullptr;  // QObject child of this window
    std::unique_ptr<stencil::llm::LlmClient> llmClient_;
    // Reused within a short TTL, so reopening the context menu does not re-hit the endpoint
    // (browser chatSession cacheProbe). Keyed by the effective settings, so a change re-probes.
    QString llmProbeKey_;
    qint64 llmProbeAt_ = 0;
    stencil::llm::LlmProbeResult llmProbeCache_;
    QString chatLastPrompt_;   // the turn in flight, for a stopped card's Retry
    // Bounded to the most recent 32 messages and replayed in full on every call (§7).
    QVector<stencil::llm::ChatMessage> chatHistory_;
    // Kept past the send, so an EDITING plan can adopt one as the working image when the
    // canvas is empty (onChatReply, browser parity).
    QList<QImage> chatTurnAttachments_;
    QStringList chatTurnAttachmentNames_;
    // §2.1: which attachment the plan is working on (1-based, 0 = none), so an unnamed `save`
    // is named after it. Set to the sole attachment of a one-attachment turn, then moved by
    // every `image` op.
    int chatActiveAttachment_ = 0;
    // The chat's current video input ("" = none); gates the `frame` op.
    QString chatVideoPath_;
    int chatVideoFrames_ = 0;            // estimated frame count (0 = unknown)
    MediaLoader* chatMedia_ = nullptr;   // dedicated loader for chat frame extraction
    // Turns whose rendered pixels are unchanged reuse the previous downscale + PNG + base64.
    QByteArray chatImageDigest_;
    stencil::llm::ChatImage chatImageEncoded_;
    // §7 edge map (contour render of the snapshot): wire-only for the current turn, never
    // pushed to chatHistory_, so never replayed or persisted.
    stencil::llm::ChatImage chatEdgeMapEncoded_;
    QWidget* chatToast_ = nullptr;   // ChatToast in the .cpp, objectName "chatToast"
    // ChatMenuPanel in the .cpp. The ACTION is parented to the window, not the menu, so it
    // outlives the per-right-click rebuild and keeps its transcript; the widget pointers are the
    // StayOpenMenu interactive area and its key target.
    QWidgetAction* chatMenuAction_ = nullptr;
    QWidget* chatMenuPanel_ = nullptr;
    QWidget* chatMenuInput_ = nullptr;
    // DockZonesOverlay in the .cpp, shown while the floating chat dock is title-dragged.
    QWidget* dockZones_ = nullptr;
    bool chatStopRequested_ = false;   // makes the next (canceled) reply render as "Stopped."
    bool chatContinued_ = false;       // §7: at most ONE auto-continuation per user turn
    // Held back while its §7 continuation may still fire: ONE final bubble folds the stash into
    // the last round's reply, and flushHeldChatReply posts it as-is if none launches.
    bool chatReplyHeld_ = false;
    QString chatHeldReply_;
    QStringList chatHeldWarnings_;
    QStringList chatHeldNotes_;

    MediaLoader* mediaLoader_ = nullptr;   // --src resolver, created on first use
    // dataExportController.hpp. Constructed in the ctor, once canvas_/notify_ exist.
    std::unique_ptr<DataExportController> dataExport_;
    // remoteSyncController.hpp: owns the sync timers + LiveFeed, and reads MainWindow's
    // remote-link state + flags through hooks.
    std::unique_ptr<RemoteSyncController> remoteSync_;
    std::unique_ptr<ProjectTransferController> projectTransfer_;   // projectTransferController.hpp
    QString pendingLaunchLayout_;       // --layout, held until the --src image has loaded
    QString pendingLaunchLayoutJson_;   // the same, inline from a stencil:// deep link

    // Shared across all windows: last setAsDockMenu wins, so one app-lifetime menu avoids
    // dangling when a window closes. Owned by the app, not any window. Unused off macOS.
    static QMenu* sDockMenu_;
  };

}
