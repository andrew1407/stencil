#pragma once
#include "formulaParser.hpp"
#include "pageMetrics.hpp"
#include "projectsStore.hpp"
#include "tooltipRows.hpp"
#include "fileStore.hpp"
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QMainWindow>
#include <QString>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QComboBox;
class QLabel;
class QScrollArea;
class QTimer;
class QDoubleSpinBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QToolButton;
class QVariantAnimation;
class QJsonObject;
class QActionGroup;
class QWidgetAction;
class QImage;
class QPixmap;
class QMenu;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QUrl;

namespace stencil::net {
  class ConnectionManager;
  class ServerClient;
  class LiveFeed;
}

// Top-level window. Mirrors the composition done by browser/js/ui/layout.js +
// toolbar.js + the DrawingApp wiring: a toolbar of actions, the canvas in the
// center, and a status bar that reports pixel and page (cm) coordinates.
namespace stencil::gui {

  class CanvasWidget;
  class SelectionPanel;
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
    // markerSize/lineStyle change handlers (~155-178).
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
    void showContextMenu(const QPoint& globalPos);
    // Re-sync the persistent context-menu actions to the live canvas state
    // (enable flags, draw-mode label, marker/thickness seeds, group checks,
    // tooltip rows). Called as the first statement of showContextMenu().
    void syncContextActions();
    void onHoverDetail(double imageX, double imageY, const QPoint& globalPos,
                       Qt::KeyboardModifiers mods);
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
    void buildMenus();
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
    void buildProjectNameGroup(class QToolBar* bar);   // header-row project name + rename/colour group
    void updateImageSizeInfo();                        // refresh the header-row "Image Size" readout
    QPixmap makeLogoPixmap(int size) const;            // paint the mini line-chart logo (browser parity)
    void buildPageFormulaToolbar();
    void buildStyleToolbar();
    QString hotkey(const QString& id, const QString& fallback) const;
    // The canonical page-format value ("A4"/"custom") behind the toolbar combo's
    // display label (the item data — the label text carries the physical size).
    QString pageSizeValue() const;
    core::PageSize currentPageDimensions() const;
    core::Point pageCoords(double imageX, double imageY) const;
    // Active display unit derived from settings_.units (cm default, else inches).
    core::UnitFormat unitFormat() const;
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
    void updatePanelReopenButton();     // show/hide + place the floating right-edge re-open chevron
    void positionPanelReopenButton();   // position it flush to the canvas' right edge, vertically centred
    void setPanelShown(bool show, bool animate);      // animated points-panel collapse/expand
    void setToolbarsShown(bool show, bool animate);   // animated top-menu collapse/expand
    void scrollTo(int x, int y);
    void setZoomAnchored(double newScale, const QPoint& cursorInViewport);
    void applyTheme();
    // Assign shared line-art icons to every action + icon toolbutton, tinted to the
    // theme text color. Re-run from applyTheme() on each light/dark/accent change.
    void styleActionIcons(bool dark, const QColor& iconColor);
    void toggleTheme();
    void applySettings(const Settings& s, bool persist);
    void openSettings();
    void scheduleAutosave();
    void saveSessionNow();
    void restoreSession();
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
    bool loadProjectIntoCanvas(const QString& id);
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
    void createLocalProject(const QString& name, bool announce = true);
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
    // Adopt a full layout envelope (crop + rotation + filter + lines + page/formulas,
    // in the ORIGINAL image's pixel space) onto `img` and show it — the shared body of
    // opening a server project and of an inline browser→desktop "Open in…" hand-off.
    // Unlike applyLayoutJson (the lines-only file-import path), this restores the crop,
    // rotation and filter too, so no dimension-mismatch prompt and nothing is dropped.
    void loadImageWithLayout(const QImage& img, const QJsonObject& layout);
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

    // Find a loaded project by id, or nullptr when none matches.
    Project* findProject(const std::string& id);
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
    // Browser-like 🎨 popup: a menu offering "Choose colour…" (opens the picker) and
    // "Use theme default colour" (enabled only when a custom colour is set) — instead of
    // opening the picker directly.
    void showProjectColorMenu();
    // Set the ACTIVE editor's project colour (local id or server-linked session):
    // validates ("" = clear, else QColor(str).isValid() → "#rrggbb" lower-case),
    // persists, repaints the name, and pushes UpdateProject{color} for a server project.
    void setActiveProjectColor(const QString& color);
    // Recolour the active BLANK project's solid background (keeps the drawn lines). No-op unless
    // this session is a blank image. Opens a colour picker; persists blank/blankColor.
    void setActiveBlankColor();
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
    void updateNameHover();   // recompute whether the cursor is over the name group (hover-reveal ✎/🎨)
    // Style the name field for its mode: editing shows an accent-outlined input; read-only shows
    // a plain title with NO border/focus ring (browser parity). Keeps the project colour.
    void applyProjectNameStyle(bool editing);
    void enterNameEdit();   // browser-like: switch the read-only name field into edit mode
    void commitProjectName();
    void cancelProjectName();
    void openInfo();
    void openShortcuts();
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
    // First-show fade-in (a gentle window-opacity ramp), mirroring the browser
    // container's appReveal animation. Runs once; later shows are instant.
    void showEvent(QShowEvent* event) override;
    // Intercept window close (Quit action, Ctrl+Q, title-bar X) to ask "are you
    // sure you want to quit?" — mirrors the browser's beforeunload guard. Bypassed
    // when forceClose_ is set (programmatic load-failure auto-closes).
    void closeEvent(QCloseEvent* event) override;

    // ── core widgets ──
    bool rKeyHeld_ = false;  // R held? gates the Alt+R+←/→ line-rotate chord
    CanvasWidget* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    SelectionPanel* selPanel_ = nullptr;
    Notifications* notify_ = nullptr;
    CanvasTooltip* tooltip_ = nullptr;
    IncognitoOverlay* incognitoOverlay_ = nullptr;
    DropZonesOverlay* dropZones_ = nullptr;   // split image-drop overlay (save | incognito)
    ProjectDragZones* projectZones_ = nullptr;  // 3-zone overlay for dragging a project out of the dialog
    QLabel* status_ = nullptr;
    QComboBox* pageSize_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    // Set before a programmatic close() (e.g. a new window that failed to load its
    // project) so closeEvent skips the quit-confirmation prompt for that path.
    bool forceClose_ = false;
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
    QToolButton* projectNameEdit_ = nullptr;    // ✎ rename affordance (enters edit mode)
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
    QAction* projectNameEditAction_ = nullptr;
    QAction* projectColorBtnAction_ = nullptr;
    QAction* blankColorBtnAction_ = nullptr;
    QAction* projectNameAcceptAction_ = nullptr;
    QAction* projectNameCancelAction_ = nullptr;

    // ── inline toolbar widget groups (S10 custom page, S11 formulas) ──
    // The QWidgetAction handle (…Act_) is toggled, not the widget, so the
    // toolbar re-lays-out and actually makes room for the inputs.
    QWidget* customGroup_ = nullptr;
    QAction* customGroupAct_ = nullptr;
    QDoubleSpinBox* customW_ = nullptr;
    QDoubleSpinBox* customH_ = nullptr;
    QLabel* customUnitLabel_ = nullptr;  // "cm"/"in" suffix by the spinboxes
    QComboBox* unitCombo_ = nullptr;     // toolbar cm/in switch (mirrors the menu)
    QCheckBox* allowFormulas_ = nullptr;
    QWidget* formulaGroup_ = nullptr;
    QAction* formulaGroupAct_ = nullptr;
    QLineEdit* formulaX_ = nullptr;
    QLineEdit* formulaY_ = nullptr;
    QLabel* formulaError_ = nullptr;

    // ── Style toolbar row (S8; browser toolbar.js Image + Line Style + Draw
    // sections ~24-63). Filter combo + tint swatch, default line color/thickness/
    // marker/style controls, and the line/rect draw-mode toggle. The toolbar sets
    // canvas DEFAULTS only — selected-line inline editing is owned by the
    // SelectionPanel (Step 10), per the plan's setSelectedLineStyle resolution.
    QToolButton* drawModeBtn_ = nullptr;
    QToolButton* lineColorBtn_ = nullptr;
    QSpinBox* lineThickness_ = nullptr;
    QSpinBox* markerSize_ = nullptr;
    QComboBox* lineStyle_ = nullptr;
    QComboBox* imageFilter_ = nullptr;
    QComboBox* compareCombo_ = nullptr;   // compare-with-original view selector
    QToolButton* filterColorBtn_ = nullptr;
    QAction* filterColorAct_ = nullptr;  // tint swatch action (hidden unless custom)
    QColor lineColorValue_{"#FFFF00"};
    QColor filterColorValue_{"#7c3aed"};

    // ── actions (shared by menu bar, toolbar, context menu) ──
    QAction* actOpen_ = nullptr;
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
    class QToolBar* headerToolbar_ = nullptr;     // always-visible header row (pill + project name)
    class QToolButton* panelReopenBtn_ = nullptr; // floating right-edge chevron: re-opens a hidden panel
    class QLabel* imageSizeInfo_ = nullptr;       // header-row "Image Size: W × H px" (always visible)
    class QToolButton* logoBtn_ = nullptr;        // header-row app logo — click cycles the accent (browser parity)
    QTimer* logoClickTimer_ = nullptr;            // defers the single-click cycle so a double-click can pre-empt it
    // Fullscreen restore state: whether the toolbars were shown, and the panel's dock area/visibility
    // before entering fullscreen (fullscreen hides the toolbars + moves the panel to the LEFT).
    bool fsActive_ = false;   // our own fullscreen flag (isFullScreen() is unreliable on macOS)
    bool fsWasToolbars_ = true;
    bool fsWasPanel_ = true;
    QTimer* fsHoverTimer_ = nullptr;   // polls the cursor to edge-reveal toolbars/panel in fullscreen
    int panelRestoreWidth_ = 300;           // remembered panel width for the expand animation
    QVariantAnimation* panelAnim_ = nullptr;  // in-flight panel collapse/expand (min==max pinning)
    QVariantAnimation* barsAnim_ = nullptr;   // in-flight toolbars collapse/expand
    QAction* actSettings_ = nullptr;
    QAction* actProjects_ = nullptr;
    QAction* actConnect_ = nullptr;
    QAction* actLinks_ = nullptr;
    QAction* actNewProject_ = nullptr;
    QAction* actSaveProject_ = nullptr;
    QAction* actClearProject_ = nullptr;  // trash: clear (remove) the current project/editor; hidden for server projects
    QAction* actProjectColor_ = nullptr;       // Project menu: pick the active project's name colour
    QAction* actProjectColorClear_ = nullptr;  // Project menu: revert it to the theme default
    QAction* actSaveSession_ = nullptr;
    QAction* actInfo_ = nullptr;
    QAction* actIncognito_ = nullptr;
    QAction* actShortcuts_ = nullptr;
    QAction* actOpenIn_ = nullptr;   // "Open In…" (browser / Telegram) — see openInAnotherApp
    QAction* actQuit_ = nullptr;

    // ── Data actions (S9; browser toolbar.js Image/Layout buttons + the paste
    // listener). Layout JSON export/import + clipboard, image save/copy/paste.
    QAction* actDownloadJson_ = nullptr;
    QAction* actUploadJson_ = nullptr;
    QAction* actCopyLayout_ = nullptr;
    QAction* actPasteLayout_ = nullptr;
    QAction* actSaveImage_ = nullptr;
    QAction* actCopyImage_ = nullptr;
    QAction* actPasteImage_ = nullptr;

    // ── Context-menu submenu actions (S11; browser/js/ui/contextMenu.js). These
    // persistent actions/QWidgetActions are owned by `this` and reused on every
    // right-click so their checked/enabled/visible state stays live. The toolbar
    // and menu bar keep their own shared QActions; these cover the bits the
    // context menu adds on top (draw-mode bridge, instant rect, the Style/Filter/
    // Tooltip submenus).

    // Draw-mode bridge (contextMenu.js:416-421 ctx-drawmode-toggle) — flips the
    // canvas line<->rect mode and notifies. Label re-synced before each exec.
    QAction* actDrawModeToggle_ = nullptr;
    // Instant rectangle (contextMenu.js:425-431 ctx-draw-rect): rect mode + begin
    // drawing immediately.
    QAction* actDrawRectNow_ = nullptr;

    // Style submenu (contextMenu.js:39-57): marker/thickness spinboxes hosted in
    // QWidgetActions + an exclusive line-style radio group. Drive canvas defaults.
    QActionGroup* lineStyleGroup_ = nullptr;
    QAction* actStyleSolid_ = nullptr;
    QAction* actStyleDashed_ = nullptr;
    QAction* actStyleDotted_ = nullptr;
    QWidgetAction* markerSizeAction_ = nullptr;
    QWidgetAction* thicknessAction_ = nullptr;
    QSpinBox* markerSpin_ = nullptr;
    QSpinBox* thickSpin_ = nullptr;

    // Image Filter submenu (contextMenu.js:59-74): exclusive filter radio group +
    // a custom-tint picker action shown only for "custom".
    QActionGroup* filterGroup_ = nullptr;
    // Compare-with-original radio set (View → Compare), synced with compareCombo_.
    QActionGroup* compareGroup_ = nullptr;
    QAction* actFilterNone_ = nullptr;
    QAction* actFilterBW_ = nullptr;
    QAction* actFilterSepia_ = nullptr;
    QAction* actFilterInvert_ = nullptr;
    QAction* actFilterContour_ = nullptr;
    QAction* actFilterCustom_ = nullptr;
    QAction* tintColorAction_ = nullptr;

    // Tooltip submenu rows (contextMenu.js:96-107): per-row visibility toggles.
    // Backed by the booleans below (no Settings persistence yet — see notes).
    QAction* actTtPage_ = nullptr;
    QAction* actTtScreen_ = nullptr;
    QAction* actTtCoords_ = nullptr;
    bool tooltipShowPage_ = true;
    bool tooltipShowScreen_ = true;
    bool tooltipShowCoords_ = true;

    // Units submenu (View ▸ Units): cm | inches, persisted via settings_.units.
    QAction* actUnitCm_ = nullptr;
    QAction* actUnitIn_ = nullptr;

    // ── hotkeys (S13: defaults + user overrides, live re-apply) ──
    QHash<QString, QString> hotkeys_;
    QHash<QString, QString> hotkeyDefaults_;
    QHash<QString, QString> hotkeyLabels_;
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
    // Active session's blank-fill colour ("#rrggbb"), or "" for an ordinary image project. Set by
    // createBlankImageFromDialog, restored on open, folded into the project meta (blank/blankColor),
    // and recoloured in place by setActiveBlankColor. Non-empty ⇔ a blank project.
    QString blankColor_;
    // Pending provenance for an in-flight loadImageByUrl(), promoted to current* in
    // onLaunchImageLoaded() once the async load succeeds.
    QString pendingProvSource_;
    QString pendingProvResource_;
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
    double lastHoverX_ = 0.0;
    double lastHoverY_ = 0.0;
    // The text color the toolbar/menu icons were last rasterized in (set by
    // styleActionIcons). Lets live handlers re-icon a widget in the current theme
    // color without recomputing the palette (e.g. the draw-mode toggle).
    QColor iconColor_{Qt::black};
    // Guards the one-shot first-show fade (see showEvent).
    bool firstShow_ = true;

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
