#pragma once
#include "fileStore.hpp"
#include "serverClient.hpp"
#include "tooltipRows.hpp"  // core::UnitFormat for the tooltip's "Line: <len> <unit>" row
#include <QColor>
#include <QDialog>
#include <QHash>
#include <QPair>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVector>
#include <vector>

class QVBoxLayout;
class QListWidget;
class QListWidgetItem;
class QComboBox;
class QLineEdit;
class QTimer;
class QNetworkAccessManager;
class QVariantAnimation;
class QLabel;
class QPushButton;
class QWidget;

namespace stencil::net {
  class ConnectionManager;
}

// Saved-projects browser. Mirrors browser/js/ui/projectsModal.js: list projects,
// open / delete one, or create a new one. exec() then read action()/selectedId()/
// newName() to apply the choice; removals (Delete / batch Remove / Clear All) are
// instead confirmed in-dialog and signalled, so the window stays open (see signals).
// When a ConnectionManager is supplied, server
// (shared) projects are listed alongside the local ones with a golden outline and
// a server badge, refreshed live on a short timer (the desktop analogue of the
// browser modal's WebSocket project-event feed → periodic listProjects refresh).
namespace stencil::gui {

  class ProjectDragZones;
  class ListFilterFade;

  // Which batch-transfer directions apply to a selection of `locals` local + `remotes`
  // server rows. Inapplicable directions are HIDDEN, not greyed (browser parity:
  // projectsModal.js updateBatchBar). toServer covers move+copy to server; toLocal
  // covers move+copy to local.
  struct BatchDirections { bool toServer = false; bool toLocal = false; };
  BatchDirections batchDirectionsFor(int locals, int remotes, bool haveServers);

  class ProjectsDialog : public QDialog {
    Q_OBJECT
   public:
    // Supply the main window's drag-out zone overlay (open here / new window / remove), shown
    // while a project row is dragged out of this (modal) dialog. Optional (nullptr = no zones).
    void setDragZones(ProjectDragZones* z) { dragZones_ = z; }

    // Whether a hand-off target is configured (a browser URL, or a Telegram bot for server
    // rows). The row menu hides "Open in another app" when nothing is available, exactly as
    // the browser hides it (ui/controlState.js) rather than offering a dead action.
    void setOpenInAvailable(bool local, bool server) {
      openInLocalOk_ = local;
      openInServerOk_ = server;
    }

    // NewBlank: create a blank solid-color image (the main window opens its
    // BlankImageDialog after this dialog closes).
    // OpenInNewWindow: like Open, but the main window loads the project into a
    // fresh top-level window instead of replacing the current canvas.
    // OpenRemote: open a server-stored project (read selectedServerUrl()+selectedId()).
    // MoveToServer: store a LOCAL project on a server, then drop the local copy
    //   (read selectedId() + selectedServerUrl()).
    // MoveToLocal: copy a SERVER project into local storage, then delete it from the
    //   server (read selectedServerUrl() + selectedId()).
    // MakeLocalCopy: copy a SERVER project into local storage (named "<name>-local")
    //   and open it, leaving the server copy in place (read selectedServerUrl()+selectedId()).
    // CopyToServer: copy a LOCAL project to a server, leaving the local one in place
    //   (read selectedId() + selectedServerUrl() + newName()).
    // MakeLocalCopy now carries newName() (the copy's name, default "<name>-copy").
    // Batch* act on the checked rows (read batchItems()): BatchMoveToServer
    //   / BatchCopyToServer (local-only checked + selectedServerUrl()), BatchMoveToLocal /
    //   BatchCopyToLocal (server-only checked). BatchRemove never reaches action() —
    //   it confirms in-dialog and emits removeRequested (kept as runBatch's dispatch tag).
    // SetColor: set (or clear) a project's accent colour — read selectedId() +
    //   selectedServerUrl() (empty = local) + selectedColor() ("" = theme default).
    enum class Action { None, Open, OpenInNewWindow, New, Rename, NewBlank,
                        OpenRemote, MoveToServer, MoveToLocal, MakeLocalCopy, CopyToServer,
                        SetColor,
                        BatchRemove, BatchMoveToServer, BatchCopyToServer,
                        BatchMoveToLocal, BatchCopyToLocal, ClearAll };

    // `now` (epoch ms) is the reference point for the per-row expiry labels and
    // their warning/expired colouring; the caller passes its clock so the dialog
    // stays free of time sources. `connections` (nullable) supplies the shared
    // server projects shown with a golden outline. `thumbs` maps a local project
    // id to its pre-rendered EDITED-result preview (filtered image + drawn lines),
    // shown as the row icon; the caller renders them via the canvas/export path.
    // `activeProjectId` (nullable-empty) is the project open in THIS editor right now —
    // its row gets the browser-parity "(Current)" mark right after its origin badge,
    // painted in the installed palette's accent (`accentColor` is unused, kept for ABI).
    explicit ProjectsDialog(const std::vector<Project>& projects, long long now,
                            stencil::net::ConnectionManager* connections = nullptr,
                            const QHash<QString, QPixmap>& thumbs = {},
                            QWidget* parent = nullptr,
                            const QString& activeProjectId = QString(),
                            const QColor& accentColor = QColor());

    // This window's own session is unsaved (no project open): show the pinned "Temporary
    // (unsaved)" row at the top — "Incognito (unsaved)" for an incognito editor.
    void setTemporary(bool temporary, bool incognito = false);

    Action action() const { return action_; }
    QString selectedId() const { return selectedId_; }
    QString selectedServerUrl() const { return selectedServerUrl_; }
    QString newName() const { return newName_; }
    // For SetColor: the chosen colour ("#rrggbb"), or "" to clear to the theme default.
    QString selectedColor() const { return selectedColor_; }
    // For Batch* actions: the checked rows as (id, serverUrl) pairs (serverUrl empty = local).
    const QVector<QPair<QString, QString>>& batchItems() const { return batchItems_; }

    // Replace the listed projects and repaint — the owner calls this after acting on a
    // request signalled below, so the dialog STAYS OPEN and simply shows the new state.
    // The overload carries the window's session state IN THE SAME repaint: a removal can
    // empty the list and blank the editor in one breath, and two repaints showed the
    // batch bar for the stale row and took it away a beat later — the list, and the row
    // arriving in it, visibly jumped (user report).
    void setProjects(const std::vector<Project>& projects);
    void setProjects(const std::vector<Project>& projects, bool temporary, bool incognito);

   signals:
    // "Clear All (Local)": already confirmed INSIDE the dialog, so the confirmation sits
    // over the still-open window rather than replacing it. The owner does the removal and
    // calls setProjects().
    void clearAllRequested();
    // Remove (single ⋯/right-click Delete, drag-out Remove zone, batch Remove): same
    // stay-open pattern — already confirmed in-dialog, the doomed rows are scattering.
    // Items are (id, serverUrl) pairs (serverUrl empty = local); the owner removes them
    // and calls setProjects().
    void removeRequested(const QVector<QPair<QString, QString>>& items);
    // Inline rename (dblclick on the name / the ⋯ menu's Rename): already validated
    // in-dialog, same stay-open pattern — the owner renames and calls setProjects().
    void renameRequested(const QString& id, const QString& newName);
    // "Set expiration": the editor already ran OVER this window (never replacing it, browser
    // parity) and the user saved. Same stay-open pattern — the owner writes the meta and
    // calls setProjects(). `expiresAt` 0 means "keep forever".
    void expirationRequested(const QString& id, long long expiresAt,
                             const QString& refreshPeriod, bool autoRefresh);
    // "Open in another app": the owner holds the image + settings a hand-off needs, so
    // the row just names itself. `serverUrl` empty = a local row. `closeRect` is the row's
    // "⋯" chip (GLOBAL), where the dialog it opens flies back to. Stay-open, like the rest.
    void openInRequested(const QString& id, const QString& serverUrl, const QRect& closeRect);

   protected:
    // Hover-magnify: watch the list viewport so hovering a row's thumbnail pops a
    // larger floating preview that follows the cursor.
    bool eventFilter(QObject* obj, QEvent* ev) override;
    // Finalize pending row retirements (and stop their scatters) BEFORE the close
    // flight photographs the dialog — a removed row must never resurface in the
    // shrinking ghost, however early the dialog is closed.
    void done(int result) override;

   private:
    // Persist one edited field of a row — the half every per-row editor shares. A local
    // row updates the registry copy and saves; a server row issues its guarded PUT and
    // patches the cached record so the tooltip reflects it before the next live re-list.
    // `mutate` writes the value into a local Project, `push` issues the PUT, `cache`
    // writes it into the cached ServerProject.
    void commitRowEdit(const QString& id, const QString& server,
                       const std::function<void(Project&)>& mutate,
                       const std::function<void(stencil::net::ServerClient*, qint64,
                                                std::function<void(bool, qint64)>)>& push,
                       const std::function<void(stencil::net::ServerProject&)>& cache);
    void refresh();
    // Re-list server projects across every connection and append golden rows.
    void refreshRemote();
    // The edited preview for a server project: its rendered `result` (or the
    // `original` if never saved), fetched via the connection and cached by version.
    // When the server holds no stored bytes (e.g. an extension-added project that
    // only recorded the image's web URL), falls back to fetching that `source` URL.
    QPixmap remoteThumb(const stencil::net::ServerProject& sp);
    // Fetch a server project's stored preview ASYNCHRONOUSLY: downloadFile("result") →
    // downloadFile("original") → the `source` web URL, swapping the row icon in on arrival so the
    // dialog never blocks on the network. `key` is the remoteThumbs_ cache key.
    void fetchServerThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    // Fetch the project `source` image URL ASYNCHRONOUSLY (no blocking): the row shows
    // a placeholder immediately and its icon is swapped in when the download finishes.
    // `key` is the remoteThumbs_ cache key; the result (even a miss) is cached.
    void fetchSourceThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    // Cache `img` (scaled) as `key`'s thumb and swap the matching live row's placeholder icon.
    // Shared tail of the server-download and source-URL fetch paths. An empty `img` caches a miss.
    void applyRemoteThumb(const QString& key, const QString& id, const QString& serverUrl,
                          const QImage& img);
    // A uniform 56×56 fallback tile (centered native glyph) shown when a row has no
    // image, so every row is the same height. `remote` picks a network vs file glyph.
    QPixmap placeholderIcon(bool remote) const;
    QPixmap temporaryIcon(bool incognito) const;   // the pinned row's pencil / mask tile
    // The hover-magnify preview is sand too (browser js/ui/projectsModal.js
    // enableThumbZoom, motion.js surfaceIn/surfaceOut): it forms from motes streaming
    // out of the row's icon cell and comes apart into motes pouring back in. `it` is
    // the row the flight belongs to.
    bool dustHoverPreview(QListWidgetItem* it, bool gather);
    // Hide the hover-magnify preview, dusting it back into the row it was shown for.
    void hideHoverPreview();
    // Place the preview down-right of the (global) cursor, flipped/clamped on-screen —
    // called on every move, so the glance follows the pointer (browser positionZoom).
    void placeHoverPreview(const QPoint& globalCursor);
    // An APPEARANCE: the preview waits behind its own gathering motes and fades up as
    // the last of them land (appTooltip showFor / browser surfaceIn). Shared by the
    // first show, a swap onto another row, and the Alt re-scale.
    void revealHoverPreview(QListWidgetItem* it);
    QVariantAnimation* hoverFade();   // lazily built windowOpacity ramp for the above
    // The REAL cursor (not an event's claim) is over the previewed row's thumbnail —
    // guards the Leave/deactivate backstops against spurious events our own preview
    // window triggers when it slides under a stationary pointer.
    bool pointerOverPreviewedIcon() const;
    // Per-row action menu (the "⋯" kebab + right-click both call this). Selects
    // `it` first, since the action slots act on the current item.
    void showRowMenu(QListWidgetItem* it, const QPoint& globalPos);
    void openSelected();
    void openSelectedInNewWindow();
    // ── row-open gestures (browser parity) ──
    //   single click            → confirm, then open in the CURRENT window
    //   double click            → open immediately, no confirmation
    //   Ctrl/⌘ + single click   → confirm, then open in a NEW window
    //   Ctrl/⌘ + double click   → new window immediately, no confirmation
    //   Return on a focused row → treated as a plain single click (confirms)
    // The confirmation is asked IN-DIALOG (finishOpen) over the still-open list.
    //
    // Arm the deferred single-click open. Deferring by doubleClickInterval() is
    // the crux: a double click must cancel it, or the confirmation flashes up
    // before the second click lands.
    void scheduleRowOpen(QListWidgetItem* it);
    void fireRowOpen();       // the timer expired → a genuine single click
    void openRow(QListWidgetItem* it, bool newWindow, bool confirm);
    // Confirm (over the STILL-OPEN dialog, browser parity) then set `act` + accept().
    // A double click (confirmOpen_ false) accepts straight away; Cancel keeps the
    // dialog up. `name` labels the question.
    void finishOpen(Action act, bool newWindow, const QString& name);
    void deleteSelected();
    // Move the selected LOCAL project to a server (pick one if several connected).
    void moveToServerSelected();
    // Copy the selected LOCAL project to a server (local copy kept), prompting a name.
    void copyToServerSelected();
    // Move the selected SERVER project into local storage.
    void moveToLocalSelected();
    // Make a detached local copy of the selected SERVER project (server copy kept), prompting a name.
    void makeLocalCopySelected();
    // Re-apply the storage filter (All / Local / Server / a specific server) + the search
    // text to the visible rows: excluded rows fade + collapse out, included ones back in.
    void applyFilter();
    // Lazily build that transition (support/filterFade); rows that are LEFT arrive
    // out of sand via the shared ListFilterFade::dustRowIn.
    ListFilterFade* filterFade();
    // (Re)populate the "Show:" combo with All / Local / All-servers + one entry per connected
    // server, preserving the current selection. Called when the connected-server set changes.
    void rebuildFilterOptions();
    // Multi-select: collect the checked rows + show/enable the batch toolbar; run a batch action.
    void onItemChanged(QListWidgetItem* it);
    void updateBatchBar();
    // Select-all toggle over the CURRENT filtered view (browser: the per-render
    // `selectables` pool + updateSelectAll).
    void updateSelectAll();
    bool allFilteredChecked() const;
    void toggleSelectAll();
    void runBatch(Action act);
    // Inline rename (browser parity: dblclick the name → a live-validated editor with
    // ✓/✗ over the row; Enter saves via renameRequested, Esc/click-away discards).
    void beginInlineRename(QListWidgetItem* it);
    void closeInlineRename();
    // Pop a colour picker (seeded with the row's current colour) and emit SetColor.
    void setColorSelected();
    // Clear the row's colour back to the theme default (emit SetColor with "").
    void clearColorSelected();
    // Resolve `it`'s (id, serverUrl), set the SetColor result fields, and accept().
    void emitSetColor(QListWidgetItem* it, const QString& color);
    // A row's current colour ("#rrggbb" or "") — local meta or server record.
    QString rowColor(const QListWidgetItem* it) const;
    // …the selected row's, via the same lookup.
    QString currentRowColor() const;
    // The (serverUrl|id) key for list row `i` (matches checked_ keys); "" for placeholder rows.
    QString rowKeyAt(int i) const;
    // Scatter the given rows (empty = every data row) before they leave, the way the single
    // Remove already does — a painted list row has no widget, so its RECT comes apart.
    void scatterRows(const QSet<QString>& keys = {});
    // Blank `it` the instant its scatter starts (the slot stays open), then drop the
    // item once the animation has played — the overlay flies a snapshot, so leaving
    // the real row painted underneath hid the removal entirely.
    void retireRow(QListWidgetItem* it);
    void createNew();
    void createBlank();

    // The "⋯" chip of the row whose menu is open, in global coords — where a window raised
    // from that menu flies back to once the menu is gone. Empty on every other path.
    // The selection-only batch actions, revealed as ONE group (see updateBatchBar).
    QWidget* batchSelectedGroup_ = nullptr;
    QRect menuKebabRect_;
    bool openInLocalOk_ = false;    // a browser URL is set → local rows can hand off
    bool openInServerOk_ = false;   // …that, or a Telegram bot → server rows can too
    std::vector<Project> projects_;
    long long now_ = 0;
    stencil::net::ConnectionManager* connections_ = nullptr;
    QString activeProjectId_;  // the project open in THIS editor right now (its "(Current)" row)
    QVBoxLayout* barSlot_ = nullptr;   // batch bar + list, spacing 0 (see the .cpp)
    bool built_ = false;       // the list has been built at least once (arrivals animate after that)
    bool temporary_ = false;   // setTemporary: this window is an unsaved session → the pinned row
    bool incognito_ = false;   // …an incognito one
    // id -> pre-rendered local-project preview (edited result), shown as the row icon.
    QHash<QString, QPixmap> thumbs_;
    // Cached server-project previews, keyed "serverUrl|id|version" so the periodic
    // remote re-list reuses them instead of re-downloading unchanged projects.
    QHash<QString, QPixmap> remoteThumbs_;
    QVector<stencil::net::ServerProject> remote_;
    // Lazily-created network manager for fetching server projects' `source` image
    // URLs when the server itself holds no stored bytes.
    QNetworkAccessManager* thumbNet_ = nullptr;
    // Cache keys with an in-flight async source fetch, so a re-list doesn't kick off
    // a duplicate download for the same project.
    QSet<QString> thumbInFlight_;
    // Frameless floating label showing the magnified thumbnail under the cursor.
    QLabel* hoverPreview_ = nullptr;
    QListWidgetItem* hoverItem_ = nullptr;   // the row the shown preview belongs to
    QVariantAnimation* hoverFade_ = nullptr; // its opacity ramp (in behind dust, out behind it)
    bool hoverClosing_ = false;              // the ramp is running towards hide()
    bool hoverZoomCursor_ = false;           // the viewport shows the magnifier cursor
    // The row whose "⋯" chip the cursor is on (-1 = none), and the looping sweep that
    // plays over it while it is. Only its OWN hover styles the chip — a row hover used to
    // brighten it from anywhere on the row.
    int kebabHoverRow_ = -1;
    class ShimmerOverlay* kebabSweep_ = nullptr;   // the app's shared glass sweep
    QString tipRowText_;   // the row text the visible tooltip belongs to (moves vs retires)
    void setKebabHover(int row);
    QTimer* remoteTimer_ = nullptr;
    bool remoteBusy_ = false;
    // False until the first server listing resolves — drives the "Loading shared
    // projects…" placeholder so the dialog can open instantly (remote fetch deferred).
    bool remoteLoaded_ = false;
    QListWidget* list_ = nullptr;
    // "Clear All" (local-only) button; relabelled "Clear All Local" live while a server
    // is connected so the label tracks the actual removal across the remote poll.
    QPushButton* clearAllBtn_ = nullptr;
    QComboBox* filter_ = nullptr;   // All / Local / Server / per-server row filter
    ListFilterFade* filterFade_ = nullptr;  // its enter/exit transition (owned by the list)
    ProjectDragZones* dragZones_ = nullptr;  // main-window drag-out overlay (nullptr = none)
    QComboBox* sortCombo_ = nullptr;  // Name / Local first / Server first / Newest / Oldest / Manual
    QComboBox* searchModeCombo_ = nullptr;  // Name + keywords / Names only / Keywords only
    QLineEdit* search_ = nullptr;   // name search box (mirrors the browser modal)
    QPushButton* selectAllBtn_ = nullptr;  // Select all / Deselect all over the filtered view
    QStringList knownServerUrls_;   // last server set the filter combo was built from
    // Multi-select: checked row keys ("serverUrl|id"; serverUrl empty = local), the batch
    // toolbar + its buttons, and the resolved (id, serverUrl) pairs for the chosen batch action.
    QSet<QString> checked_;
    QWidget* batchBar_ = nullptr;
    QLabel* batchCount_ = nullptr;
    QPushButton* batchToServer_ = nullptr;
    QPushButton* batchCopyServer_ = nullptr;
    QPushButton* batchToLocal_ = nullptr;
    QPushButton* batchCopyLocal_ = nullptr;
    QPushButton* batchRemove_ = nullptr;
    QPushButton* batchClear_ = nullptr;
    bool building_ = false;   // suppress itemChanged while refresh() sets check states
    // Row-open gesture state (see scheduleRowOpen/openRow).
    QTimer* clickTimer_ = nullptr;      // pending single-click open
    int pendingRow_ = -1;               // row it applies to
    bool pendingNewWindow_ = false;     // Ctrl/⌘ was down for that click
    Qt::KeyboardModifiers pressMods_;   // modifiers of the last press on the list
    QPoint pressPos_;                   // viewport pos of that press (name dblclick hit test)
    bool pressOnCheck_ = false;         // last press landed on a row's checkbox
    // Inline rename editor (child of the list viewport).
    QWidget* renameBox_ = nullptr;
    bool confirmOpen_ = true;           // single click / drag-out ask; double click doesn't
    bool rowDragging_ = false;          // a drag must not open anything on release
    QVector<QPair<QString, QString>> batchItems_;
    Action action_ = Action::None;
    QString selectedId_;
    QString selectedServerUrl_;
    QString newName_;
    QString selectedColor_;
  };

}
