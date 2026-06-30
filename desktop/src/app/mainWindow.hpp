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
class QJsonObject;
class QActionGroup;
class QWidgetAction;
class QImage;
class QPixmap;
class QMenu;
class QDragEnterEvent;
class QDropEvent;

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
  class MediaLoader;
  struct LaunchOptions;

  class MainWindow : public QMainWindow {
    Q_OBJECT
   public:
    // restoreLast=false skips reloading the last autosaved session, so the editor
    // starts empty — used for a "New Incognito Editor" window (and an incognito
    // launch), which should begin blank rather than resurrecting prior content.
    explicit MainWindow(QWidget* parent = nullptr, bool restoreLast = true);

    // Apply command-line launch options (the desktop counterpart of the browser's
    // URL deep-links). Called from main() AFTER show() so the async image / video
    // / network resolution runs on the event loop. See gui/launchOptions.hpp.
    void applyLaunchOptions(const LaunchOptions& opts);

    // Open a file handed in by the OS shell — a Finder/Explorer "Open With", a
    // file-association double-click (via QFileOpenEvent / argv), or a drag onto
    // the window. Sniffs the suffix: *.json → layout, else image/video → --src.
    // `frame` selects the video frame (0 = first).
    void openPathFromOS(const QString& path, int frame = 0);

   private slots:
    void openImage();
    // "Open another image" dialog (mirrors browser openImageModal.js): pick a file
    // + incognito, then replace the current editor or launch it in a new window.
    void openAnotherImage();
    // Blank-image creator (mirrors browser blankImageModal.js): pick a fill
    // color + px size (defaulting to the page at 96 dpi), then adopt the
    // generated image through the same path as a clipboard paste.
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
    // Data actions (S9): layout JSON export/import + clipboard, and image
    // save/copy/paste. Mirror browser drawingApp.js downloadJSON/uploadJSON/
    // copyLayoutToClipboard/applyPastedLayout (~2071-2222) and saveImage/
    // copyImageToClipboard + the paste listener (~2035-2152, ~557-592).
    void downloadLayout();
    void uploadLayout();
    void copyLayout();
    void pasteLayout();
    void saveImageFile();
    void copyImageToClipboard();
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
    void buildPageFormulaToolbar();
    void buildStyleToolbar();
    QString hotkey(const QString& id, const QString& fallback) const;
    core::PageSize currentPageDimensions() const;
    core::Point pageCoords(double imageX, double imageY) const;
    // Active display unit derived from settings_.units (cm default, else inches).
    core::UnitFormat unitFormat() const;
    // Apply the current unit to the custom page spinboxes + their suffix label.
    void applyUnitToPageInputs();
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
    // Move a LOCAL project onto `serverUrl` (create + upload original + push layout),
    // then remove the local copy. Mirrors the browser's moveProjectToServer().
    void moveLocalProjectToServer(const QString& serverUrl, const QString& id);
    // Copy a LOCAL project to `serverUrl` (default name "<name>-copy"), leaving the local one
    // in place. Mirrors the browser copyProjectToServer().
    void copyLocalProjectToServer(const QString& serverUrl, const QString& id, const QString& name);
    // Shared body of move/copy-to-server. localProjectOriginal gathers a local project's
    // original bytes + dims (live canvas when active, else the stored file); false + notify when
    // there's nothing usable. createServerFromLocal creates `pr` on `c` under `name` (upload +
    // annotated layout), reporting the new id/version; false + notify on failure.
    bool localProjectOriginal(const Project& pr, QByteArray& bytes, QString& ext, int& w, int& h);
    bool createServerFromLocal(stencil::net::ServerClient* c, const Project& pr,
                               const QString& name, const QByteArray& bytes, const QString& ext,
                               int w, int h, QString& newIdOut, qint64& newVersionOut);
    // Move a SERVER project into local storage (download bytes + layout, persist as a
    // local project), then delete it from the server. Mirrors moveProjectToLocal().
    void moveServerProjectToLocal(const QString& serverUrl, const QString& id);
    // Make a detached LOCAL copy of a server project (name defaults to "<name>-copy" via the
    // dialog), leaving the server copy in place, and open it. Mirrors copyServerProjectToLocal().
    void makeLocalCopyOfServerProject(const QString& serverUrl, const QString& id, const QString& name);
    // Shared body of the two above: fetch image + layout (incl. crop/rotation), persist a
    // fresh local project; `name` overrides the server name when non-empty. Optionally deletes
    // the server copy. Returns its new id via newIdOut.
    bool importServerProjectToLocal(const QString& serverUrl, const QString& id,
                                    bool removeFromServer, const QString& name,
                                    QString* newIdOut);
    // True if `id` is the active project in some OTHER open window — used to block
    // removing/moving a project that's open elsewhere (the desktop analogue of the
    // browser's "open in another tab" guard).
    bool projectOpenInOtherWindow(const QString& id) const;
    // "Open another image" outcomes. Open here: replace this editor's image
    // (saving the current content first unless incognito), adopting the chosen
    // incognito mode. New window: launch a fresh window loading the image (via
    // applyLaunchOptions, the --src/--incognito path), leaving this one untouched.
    void openImageHere(const QString& path, bool incognito);
    void openImageInNewWindow(const QString& path, bool incognito);
    // Replace outcome: swap the CURRENT project's image in place (same local id / server
    // link), optionally renaming the project + keeping the existing annotations. Server
    // sessions also re-upload the `original` (replaceServerOriginal). canReplaceActive()
    // gates the outcome (a saved/linked, non-incognito project must be open).
    bool canReplaceActive() const;
    void replaceProjectImage(const QString& path, bool rename, bool keepAnnotations);
    void replaceServerOriginal();
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
    // refresh, and notify. pr.meta.name == the passed name.
    void createLocalProject(const QString& name);
    // Create the project on `serverUrl` (createProject + upload the original image)
    // and link this session to it so later saves write back. Mirrors the browser's
    // createRemoteProject (remoteSync.js).
    void createServerProject(const QString& serverUrl, const QString& name);
    void saveToActiveProject();
    // Save a server-linked session back: version-guarded PUT of name+layout, then
    // upload the rendered result. Surfaces a 409 "edited elsewhere" message and
    // leaves the link untouched. Mirrors the browser's saveToServer/saveRemoteProject.
    void saveToServer();
    // Open a server-stored project: download its original image + layout, load them
    // into this canvas, and link the session to {serverUrl, id, version}. `silent`
    // suppresses the "Opened …" toast (used by the live-co-edit poll, which reloads
    // repeatedly when peers change the project).
    bool openServerProject(const QString& serverUrl, const QString& id, bool silent = false);
    // The current page format + x/y formulas (from global settings) as a layout-envelope meta,
    // passed to buildLayoutJson on server save so they round-trip to the browser/peers.
    fileStore::LayoutMeta currentLayoutMeta() const;
    // Adopt a fetched layout's page format + formulas into the toolbar + settings (only the
    // fields it carries), so a reopened server project shows its saved page and a later save
    // re-emits them instead of clobbering with the desktop's global default.
    void adoptServerLayoutMeta(const QJsonObject& layout);
    // Live co-edit. scheduleRemotePush: debounce saveToServer() after a local edit.
    // start/stopRemotePoll: subscribe/unsubscribe the live push feed + the version-check
    // backstop while a remote session is open. pollRemoteForUpdate: one backstop tick —
    // reload the canvas if a peer bumped the version. ensureLiveFeed: (re)point the push
    // feed at the active server. onRemoteProjectEvent: a push frame arrived — coalesce a
    // reload (the desktop analogue of the browser's onServerProjectEvent).
    void scheduleRemotePush();
    void startRemotePoll();
    void stopRemotePoll();
    void pollRemoteForUpdate();
    void ensureLiveFeed();
    void onRemoteProjectEvent(const QString& id, qint64 version, bool deleted);

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
    // Set a colour on a project BY id (the Projects dialog "Set colour" path). For a
    // server project (serverUrl non-empty) it PUTs UpdateProject{color}; else it
    // updates the local meta + persists. Returns true on success.
    bool setProjectColorById(const QString& id, const QString& serverUrl, const QString& color);
    // Normalise a colour for storage: "" stays "" (clear); a QColor-valid string
    // returns "#rrggbb" lower-case; anything else returns nullopt (reject the set).
    std::optional<QString> normalizeProjectColor(const QString& color) const;
    // Live-update the ✓/✗ visibility + ✓ enabled-state/tooltip as the field is edited.
    void refreshProjectNameButtons();
    // Style the name field for its mode: editing shows an accent-outlined input; read-only shows
    // a plain title with NO border/focus ring (browser parity). Keeps the project colour.
    void applyProjectNameStyle(bool editing);
    void enterNameEdit();   // browser-like: switch the read-only name field into edit mode
    void commitProjectName();
    void cancelProjectName();
    void openInfo();
    void openShortcuts();
    void updateStatusIdle();
    // S9: confirm-replace + dimension-mismatch guard, then adopt the parsed
    // layout onto the canvas. Shared by uploadLayout + pasteLayout. Mirrors
    // browser uploadJSON/applyPastedLayout (drawingApp.js ~2101-2222).
    void applyLayoutJson(const QJsonObject& obj);
    void keyPressEvent(QKeyEvent* event) override;
    // Drag-and-drop of a file onto the window (image / video / layout JSON),
    // routed through openPathFromOS — the Photoshop-style drop-to-open.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // First-show fade-in (a gentle window-opacity ramp), mirroring the browser
    // container's appReveal animation. Runs once; later shows are instant.
    void showEvent(QShowEvent* event) override;

    // ── core widgets ──
    CanvasWidget* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    SelectionPanel* selPanel_ = nullptr;
    Notifications* notify_ = nullptr;
    CanvasTooltip* tooltip_ = nullptr;
    IncognitoOverlay* incognitoOverlay_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* pageSize_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    // Live co-edit: debounced server push after a local edit + a poll that reloads when
    // a peer changes the linked project. Flags guard against self-triggering.
    QTimer* remotePushTimer_ = nullptr;
    QTimer* remotePollTimer_ = nullptr;
    // Live push feed (raw-TCP project-events subscription) + a short single-shot timer
    // that coalesces a burst of peer events into one reload, off the socket-read slot.
    stencil::net::LiveFeed* liveFeed_ = nullptr;
    QTimer* remoteReloadTimer_ = nullptr;
    // Start of the current push debounce burst (epoch ms; 0 = none) — caps the trailing
    // debounce with a max-wait so continuous editing still flushes to peers.
    qint64 remotePushBurstStart_ = 0;
    // A peer change arrived while a reload was in flight — apply one more pass when it ends.
    bool remoteReloadPending_ = false;
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
    QToolButton* projectNameAccept_ = nullptr;
    QToolButton* projectNameCancel_ = nullptr;
    // Per-project accent swatch next to the name field (browser's color control):
    // its popup chooses a custom name colour or reverts to the theme accent.
    QToolButton* projectColorBtn_ = nullptr;
    // QToolBar::addWidget wraps each button in a QWidgetAction; show/hide must toggle THESE
    // actions (not just the widgets) or the toolbar ignores it. Used by refreshProjectNameButtons.
    QAction* projectNameEditAction_ = nullptr;
    QAction* projectColorBtnAction_ = nullptr;
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
    QToolButton* filterColorBtn_ = nullptr;
    QAction* filterColorAct_ = nullptr;  // tint swatch action (hidden unless custom)
    QColor lineColorValue_{"#FFFF00"};
    QColor filterColorValue_{"#7c3aed"};

    // ── actions (shared by menu bar, toolbar, context menu) ──
    QAction* actOpen_ = nullptr;
    QAction* actOpenAnother_ = nullptr;
    QAction* actNewBlank_ = nullptr;
    QAction* actCrop_ = nullptr;
    QAction* actRotateLeft_ = nullptr;
    QAction* actRotateRight_ = nullptr;
    QAction* actCycleFilter_ = nullptr;
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
    QAction* actFullscreen_ = nullptr;
    QAction* actSettings_ = nullptr;
    QAction* actProjects_ = nullptr;
    QAction* actConnect_ = nullptr;
    QAction* actLinks_ = nullptr;
    QAction* actNewProject_ = nullptr;
    QAction* actSaveProject_ = nullptr;
    QAction* actProjectColor_ = nullptr;       // Project menu: pick the active project's name colour
    QAction* actProjectColorClear_ = nullptr;  // Project menu: revert it to the theme default
    QAction* actSaveSession_ = nullptr;
    QAction* actInfo_ = nullptr;
    QAction* actIncognito_ = nullptr;
    QAction* actShortcuts_ = nullptr;
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
    QAction* actFilterNone_ = nullptr;
    QAction* actFilterBW_ = nullptr;
    QAction* actFilterSepia_ = nullptr;
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
    core::FormulaParser formula_;
    core::ProjectsStore projectsStore_;
    std::vector<Project> projectList_;
    QString activeProjectId_;
    // Collaboration-server connections for this window (lazily created). Owns the
    // REST clients; shared projects are listed through it.
    stencil::net::ConnectionManager* connections_ = nullptr;
    // Server linkage for the current session (empty address = a purely-local
    // project). Set when a server project is opened or created on a server; drives
    // saveToActiveProject() to write back via saveToServer(). Mirrors the browser's
    // DrawingApp.remoteLink { address, remoteId, version }.
    QString remoteAddress_;
    QString remoteId_;
    QString remoteName_;
    // The linked server project's accent colour ("#rrggbb" or empty). Kept in step
    // with the server record so a server session paints its name like a local one.
    QString remoteColor_;
    qint64 remoteVersion_ = 0;
    // Provenance of the image currently on the canvas (the image/video's own URL
    // and the page it came from). Set by loadImageByUrl(); cleared on a plain local
    // open / blank image. Folded into the project meta on create/save.
    QString currentSource_;
    QString currentResource_;
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
      QString page;  // "A3"/"A4" (empty keeps the current page)
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
    // A --layout source held until the --src image has loaded, then applied once.
    QString pendingLaunchLayout_;

    // macOS Dock menu, shared across all windows (last setAsDockMenu wins, so a
    // single app-lifetime menu avoids dangling when a window closes). Owned by the
    // app, not any window. Unused off macOS.
    static QMenu* sDockMenu_;
  };

}
