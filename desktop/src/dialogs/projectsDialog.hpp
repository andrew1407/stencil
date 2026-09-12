#pragma once
#include "fileStore.hpp"
#include "lruCache.hpp"
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
class QVariantAnimation;
class QLabel;
class QPushButton;
class QWidget;

namespace stencil::net {
  class ConnectionManager;
}

// Saved-projects browser (browser/js/ui/projectsModal.js): exec(), then read
// action()/selectedId()/newName(). Removals are confirmed in-dialog and signalled instead, so
// the window stays open. With a ConnectionManager, server projects are listed alongside the
// local ones and refreshed on a short timer — the browser modal's project-event feed.
namespace stencil::gui {

  class ProjectDragZones;
  class ListFilterFade;

  // Inapplicable directions are HIDDEN, not greyed (browser projectsModal.js updateBatchBar).
  // toServer covers move+copy to server, toLocal move+copy to local.
  struct BatchDirections { bool toServer = false; bool toLocal = false; };
  BatchDirections batchDirectionsFor(int locals, int remotes, bool haveServers);

  class ProjectsDialog : public QDialog {
    Q_OBJECT
   public:
    // The main window's drag-out zone overlay (open here / new window / remove), shown while a
    // row is dragged out of this modal dialog. nullptr = no zones.
    void setDragZones(ProjectDragZones* z) { dragZones_ = z; }

    // A browser URL, or a Telegram bot for server rows. The row menu HIDES "Open in another
    // app" when nothing is available (browser ui/controlState.js), never greys it.
    void setOpenInAvailable(bool local, bool server) {
      openInLocalOk_ = local;
      openInServerOk_ = server;
    }

    // What the owner reads per action, beyond action(): OpenRemote → selectedServerUrl() +
    // selectedId(); the four transfers → those plus newName(), Move dropping the original and
    // Copy leaving it; Batch* → batchItems(); SetColor → selectedColor(), "" = theme default.
    // BatchRemove never reaches action() — it confirms in-dialog and emits removeRequested,
    // staying only as runBatch's dispatch tag.
    enum class Action { None, Open, OpenInNewWindow, New, Rename, NewBlank,
                        OpenRemote, MoveToServer, MoveToLocal, MakeLocalCopy, CopyToServer,
                        SetColor,
                        BatchRemove, BatchMoveToServer, BatchCopyToServer,
                        BatchMoveToLocal, BatchCopyToLocal, ClearAll };

    // `now` (epoch ms) is the reference point for the per-row expiry labels, passed in so the
    // dialog stays free of time sources. `thumbs` maps a local project id to its pre-rendered
    // EDITED-result preview. `accentColor` is unused, kept for ABI.
    explicit ProjectsDialog(const std::vector<Project>& projects, long long now,
                            stencil::net::ConnectionManager* connections = nullptr,
                            const QHash<QString, QPixmap>& thumbs = {},
                            QWidget* parent = nullptr,
                            const QString& activeProjectId = QString(),
                            const QColor& accentColor = QColor());

    // This window's session is unsaved: show the pinned "Temporary (unsaved)" row at the top,
    // or "Incognito (unsaved)" for an incognito editor.
    void setTemporary(bool temporary, bool incognito = false);

    Action action() const { return action_; }
    QString selectedId() const { return selectedId_; }
    QString selectedServerUrl() const { return selectedServerUrl_; }
    QString newName() const { return newName_; }
    QString selectedColor() const { return selectedColor_; }
    // (id, serverUrl) pairs; an empty serverUrl is a local row.
    const QVector<QPair<QString, QString>>& batchItems() const { return batchItems_; }

    // Called by the owner after acting on a request signalled below, so the dialog STAYS OPEN on
    // the new state. The overload carries the session state IN THE SAME repaint: a removal that
    // also blanks the editor must not repaint twice, or the batch bar and the rows visibly jump.
    void setProjects(const std::vector<Project>& projects);
    void setProjects(const std::vector<Project>& projects, bool temporary, bool incognito);

   signals:
    // Every signal here is the same stay-open pattern: the dialog already confirmed or
    // validated in place, over the still-open window, and the owner acts then calls
    // setProjects(). Nothing here closes the dialog.
    void clearAllRequested();
    // Single ⋯/right-click Delete, the drag-out Remove zone, or batch Remove; the doomed rows
    // are already scattering.
    void removeRequested(const QVector<QPair<QString, QString>>& items);
    void renameRequested(const QString& id, const QString& newName);
    // `expiresAt` 0 means "keep forever". The editor already ran OVER this window.
    void expirationRequested(const QString& id, long long expiresAt,
                             const QString& refreshPeriod, bool autoRefresh);
    // The owner holds the image + settings a hand-off needs, so the row just names itself.
    // `closeRect` is the row's "⋯" chip in GLOBAL coords, where the dialog flies back to.
    void openInRequested(const QString& id, const QString& serverUrl, const QRect& closeRect);

   protected:
    // Watches the list viewport for the hover-magnify preview.
    bool eventFilter(QObject* obj, QEvent* ev) override;
    // Finalizes pending row retirements (and stops their scatters) BEFORE the close flight
    // photographs the dialog — a removed row must never resurface in the shrinking ghost.
    void done(int result) override;

   private:
    // The half every per-row editor shares: a local row updates the registry copy and saves; a
    // server row issues its guarded PUT and patches the cached record, so the tooltip reflects
    // it before the next live re-list.
    void commitRowEdit(const QString& id, const QString& server,
                       const std::function<void(Project&)>& mutate,
                       const std::function<void(stencil::net::ServerClient*, qint64,
                                                std::function<void(bool, qint64)>)>& push,
                       const std::function<void(stencil::net::ServerProject&)>& cache);
    void refresh();
    void refreshRemote();   // re-list across every connection and append the golden rows
    // A server project's rendered `result`, or its `original` if never saved, cached by version.
    // A server holding no stored bytes (an extension-added project that only recorded the web
    // URL) falls back to fetching that `source` URL.
    QPixmap remoteThumb(const stencil::net::ServerProject& sp);
    // The three fetches above, run ASYNCHRONOUSLY so the dialog never blocks on the network:
    // downloadFile("result") → downloadFile("original") → the `source` web URL, each swapping
    // the row icon in on arrival. `key` is the remoteThumbs_ cache key.
    void fetchServerThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    void fetchSourceThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    // Their shared tail. An empty `img` caches a miss.
    void applyRemoteThumb(const QString& key, const QString& id, const QString& serverUrl,
                          const QImage& img);
    // A uniform 56×56 tile so every row is the same height; `remote` picks a network vs file
    // glyph.
    QPixmap placeholderIcon(bool remote) const;
    QPixmap temporaryIcon(bool incognito) const;   // the pinned row's pencil / mask tile
    // The preview is sand too (browser enableThumbZoom + motion.js surfaceIn/surfaceOut): it
    // forms from motes streaming out of the row's icon cell and comes apart into motes pouring
    // back in. `it` is the row the flight belongs to.
    bool dustHoverPreview(QListWidgetItem* it, bool gather);
    void hideHoverPreview();
    // Down-right of the global cursor, flipped/clamped on-screen; called on every move, so the
    // glance follows the pointer (browser positionZoom).
    void placeHoverPreview(const QPoint& globalCursor);
    // An APPEARANCE: the preview waits behind its own gathering motes and fades up as the last
    // of them land. Shared by the first show, a swap onto another row, and the Alt re-scale.
    void revealHoverPreview(QListWidgetItem* it);
    QVariantAnimation* hoverFade();   // lazily built windowOpacity ramp for the above
    // The REAL cursor, not an event's claim: guards the Leave/deactivate backstops against the
    // spurious events our own preview window triggers as it slides under a stationary pointer.
    bool pointerOverPreviewedIcon() const;
    // The "⋯" kebab and right-click both call this. Selects `it` first, since the action slots
    // act on the current item.
    void showRowMenu(QListWidgetItem* it, const QPoint& globalPos);
    void openSelected();
    void openSelectedInNewWindow();
    // Row-open gestures (browser parity): a single click confirms then opens HERE, a double
    // click opens at once, Ctrl/⌘ makes either a NEW window, Return is a plain single click.
    // Deferring the single click by doubleClickInterval() is the crux — a double click must
    // cancel it, or the confirmation flashes up before the second click lands.
    void scheduleRowOpen(QListWidgetItem* it);
    void fireRowOpen();       // the timer expired → a genuine single click
    void openRow(QListWidgetItem* it, bool newWindow, bool confirm);
    // Confirms over the STILL-OPEN dialog, then sets `act` and accept()s; confirmOpen_ false
    // accepts straight away, and Cancel keeps the dialog up. `name` labels the question.
    void finishOpen(Action act, bool newWindow, const QString& name);
    void deleteSelected();
    // The four transfers. Move drops the original, Copy keeps it and prompts for a name; the
    // to-server pair picks a server when several are connected.
    void moveToServerSelected();
    void copyToServerSelected();
    void moveToLocalSelected();
    void makeLocalCopySelected();
    // Storage filter (All / Local / Server / a specific server) plus the search text: excluded
    // rows fade and collapse out, included ones back in.
    void applyFilter();
    // Lazily builds that transition (support/filterFade); rows that are LEFT arrive out of sand
    // via the shared ListFilterFade::dustRowIn.
    ListFilterFade* filterFade();
    // Preserves the current selection. Called when the connected-server set changes.
    void rebuildFilterOptions();
    void onItemChanged(QListWidgetItem* it);
    void updateBatchBar();
    // Over the CURRENT filtered view (browser: the per-render `selectables` pool).
    void updateSelectAll();
    bool allFilteredChecked() const;
    void toggleSelectAll();
    void runBatch(Action act);
    // Browser parity: dblclick the name for a live-validated editor with ✓/✗ over the row;
    // Enter saves via renameRequested, Esc or a click away discards.
    void beginInlineRename(QListWidgetItem* it);
    void closeInlineRename();
    // setColorSelected pops a picker seeded with the row's colour; clearColorSelected emits
    // SetColor with "" (the theme default).
    void setColorSelected();
    void clearColorSelected();
    void emitSetColor(QListWidgetItem* it, const QString& color);
    QString rowColor(const QListWidgetItem* it) const;   // "#rrggbb" or "", local meta or record
    QString currentRowColor() const;
    QString rowKeyAt(int i) const;   // (serverUrl|id), matching checked_; "" for placeholder rows
    // Empty `keys` = every data row. A painted list row has no widget, so its RECT comes apart.
    void scatterRows(const QSet<QString>& keys = {});
    // Blanks `it` the instant its scatter starts, keeping the slot open, then drops the item once
    // the animation has played: the overlay flies a snapshot, so leaving the real row painted
    // underneath hid the removal entirely.
    void retireRow(QListWidgetItem* it);
    void createNew();
    void createBlank();

    // The selection-only batch actions, revealed as ONE group (see updateBatchBar).
    QWidget* batchSelectedGroup_ = nullptr;
    // The "⋯" chip of the row whose menu is open, in global coords — where a window raised from
    // that menu flies back to once the menu is gone. Empty on every other path.
    QRect menuKebabRect_;
    bool openInLocalOk_ = false;    // a browser URL is set → local rows can hand off
    bool openInServerOk_ = false;   // …that, or a Telegram bot → server rows can too
    std::vector<Project> projects_;
    long long now_ = 0;
    stencil::net::ConnectionManager* connections_ = nullptr;
    QString activeProjectId_;   // its row carries the "(Current)" tag
    QVBoxLayout* barSlot_ = nullptr;   // batch bar + list, spacing 0 (see the .cpp)
    bool built_ = false;       // built at least once — arrivals animate only after that
    bool temporary_ = false;   // an unsaved session → the pinned row
    bool incognito_ = false;
    QHash<QString, QPixmap> thumbs_;   // id → pre-rendered edited-result preview
    // Keyed "serverUrl|id|version" so the periodic re-list reuses them instead of re-downloading
    // unchanged projects. Bounded because every edit anywhere bumps a version, and the re-list
    // timer would otherwise cache them all.
    LruCache<QString, QPixmap> remoteThumbs_{128};
    QVector<stencil::net::ServerProject> remote_;
    // Keys with an in-flight async source fetch, so a re-list starts no duplicate download.
    QSet<QString> thumbInFlight_;
    QLabel* hoverPreview_ = nullptr;   // frameless floating magnified thumbnail
    QListWidgetItem* hoverItem_ = nullptr;   // the row the shown preview belongs to
    QVariantAnimation* hoverFade_ = nullptr; // its opacity ramp (in behind dust, out behind it)
    bool hoverClosing_ = false;              // the ramp is running towards hide()
    bool hoverZoomCursor_ = false;           // the viewport shows the magnifier cursor
    // The row whose "⋯" chip the cursor is on (-1 = none). Only its OWN hover styles the chip,
    // never a hover anywhere else on the row.
    int kebabHoverRow_ = -1;
    class ShimmerOverlay* kebabSweep_ = nullptr;   // the app's shared glass sweep
    QString tipRowText_;   // the row text the visible tooltip belongs to (moves vs retires)
    void setKebabHover(int row);
    QTimer* remoteTimer_ = nullptr;
    bool remoteBusy_ = false;
    // False until the first server listing resolves: drives the "Loading shared projects…"
    // placeholder, so the dialog opens instantly with the remote fetch deferred.
    bool remoteLoaded_ = false;
    QListWidget* list_ = nullptr;
    // Relabelled "Clear All Local" live while a server is connected, so the label tracks what
    // the removal actually covers across the remote poll.
    QPushButton* clearAllBtn_ = nullptr;
    QComboBox* filter_ = nullptr;   // All / Local / Server / per-server row filter
    ListFilterFade* filterFade_ = nullptr;  // its enter/exit transition (owned by the list)
    ProjectDragZones* dragZones_ = nullptr;  // main-window drag-out overlay (nullptr = none)
    QComboBox* sortCombo_ = nullptr;  // Name / Local first / Server first / Newest / Oldest / Manual
    QComboBox* searchModeCombo_ = nullptr;  // Name + keywords / Names only / Keywords only
    QLineEdit* search_ = nullptr;   // name search box (mirrors the browser modal)
    QPushButton* selectAllBtn_ = nullptr;  // Select all / Deselect all over the filtered view
    QStringList knownServerUrls_;   // last server set the filter combo was built from
    // Checked row keys, "serverUrl|id" with an empty serverUrl for a local row.
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
