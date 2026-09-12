#pragma once
#include "fileStore.hpp"
#include "lruCache.hpp"
#include "serverClient.hpp"
#include "tooltipRows.hpp"
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
#include <optional>
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

// Saved-projects browser (browser/js/ui/projectsModal.js). With a ConnectionManager, server
// projects are listed alongside the local ones and kept live on a short timer.
namespace stencil::gui {

  class ProjectDragZones;
  class ListFilterFade;

  struct BatchDirections { bool toServer = false; bool toLocal = false; };
  BatchDirections batchDirectionsFor(int locals, int remotes, bool haveServers);

  class ProjectsDialog : public QDialog {
    Q_OBJECT
   public:
    void setDragZones(ProjectDragZones* z) { dragZones_ = z; }

    void setOpenInAvailable(bool local, bool server) {
      openInLocalOk_ = local;
      openInServerOk_ = server;
    }

    // Beyond action(): OpenRemote → selectedServerUrl() + selectedId(); transfers add newName();
    // Batch* → batchItems(); SetColor → selectedColor(). BatchRemove emits removeRequested instead.
    enum class Action { None, Open, OpenInNewWindow, New, Rename, NewBlank,
                        OpenRemote, MoveToServer, MoveToLocal, MakeLocalCopy, CopyToServer,
                        SetColor,
                        BatchRemove, BatchMoveToServer, BatchCopyToServer,
                        BatchMoveToLocal, BatchCopyToLocal, ClearAll };

    // `now` (epoch ms) is passed in so the dialog stays free of time sources; `accentColor` is kept for ABI.
    explicit ProjectsDialog(const std::vector<Project>& projects, long long now,
                            stencil::net::ConnectionManager* connections = nullptr,
                            const QHash<QString, QPixmap>& thumbs = {},
                            QWidget* parent = nullptr,
                            const QString& activeProjectId = QString(),
                            const QColor& accentColor = QColor());

    void setTemporary(bool temporary, bool incognito = false);

    Action action() const { return action_; }
    QString selectedId() const { return selectedId_; }
    QString selectedServerUrl() const { return selectedServerUrl_; }
    QString newName() const { return newName_; }
    QString selectedColor() const { return selectedColor_; }
    const QVector<QPair<QString, QString>>& batchItems() const { return batchItems_; }

    // Called after acting on a signalled request, so the dialog STAYS OPEN; the overload carries
    // the session state in the SAME repaint.
    void setProjects(const std::vector<Project>& projects);
    void setProjects(const std::vector<Project>& projects, bool temporary, bool incognito);

   signals:
    // Every signal is the stay-open pattern: the dialog confirmed in place, the owner acts and calls setProjects().
    void clearAllRequested();
    void removeRequested(const QVector<QPair<QString, QString>>& items);
    void renameRequested(const QString& id, const QString& newName);
    void expirationRequested(const QString& id, long long expiresAt,
                             const QString& refreshPeriod, bool autoRefresh);
    // `closeRect` is the row's "⋯" chip in GLOBAL coords, where the dialog flies back to.
    void openInRequested(const QString& id, const QString& serverUrl, const QRect& closeRect);

   protected:
    // Run in THIS order; filterHoverPreview only observes, an answer from the rest ends the chain.
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void filterHoverPreview(QObject* obj, QEvent* ev);
    std::optional<bool> filterListKeys(QObject* obj, QEvent* ev);
    std::optional<bool> filterListViewport(QObject* obj, QEvent* ev);
    std::optional<bool> filterRenameBox(QObject* obj, QEvent* ev);
    // Pending retirements finalize BEFORE the close flight photographs the dialog.
    void done(int result) override;

   private:
    void commitRowEdit(const QString& id, const QString& server,
                       const std::function<void(Project&)>& mutate,
                       const std::function<void(stencil::net::ServerClient*, qint64,
                                                std::function<void(bool, qint64)>)>& push,
                       const std::function<void(stencil::net::ServerProject&)>& cache);
    // Construction order is observable (tab order, findChildren) — tests/projectsDialogRows.headless.cpp pins it.
    void buildSearchRow(QVBoxLayout* layout);
    void buildBatchBar(QVBoxLayout* layout);
    void buildProjectList(QVBoxLayout* layout);
    void wireRowGestures();
    void buildFooter(struct ModalChrome& chrome);
    void startRemotePolling();
    void refresh();
    // The data each phase writes onto an item is pinned by the same test.
    void buildSortedRows(const core::ProjectsStore& store);
    void addLocalProjectRow(const Project& pr, const core::ProjectsStore& store);
    void addServerProjectRow(const stencil::net::ServerProject& sp);
    QString projectRowTooltip(int w, int h, const QString& description,
                              const QString& origin) const;
    void refreshRemote();
    QPixmap remoteThumb(const stencil::net::ServerProject& sp);
    // Asynchronous, each swapping the row icon in on arrival; `key` is the remoteThumbs_ cache key.
    void fetchServerThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    void fetchSourceThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    void applyRemoteThumb(const QString& key, const QString& id, const QString& serverUrl,
                          const QImage& img);
    QPixmap placeholderIcon(bool remote) const;
    QPixmap temporaryIcon(bool incognito) const;
    // Browser enableThumbZoom: the preview forms from motes out of the row's icon cell. `it` owns the flight.
    bool dustHoverPreview(QListWidgetItem* it, bool gather);
    void hideHoverPreview();
    // Browser positionZoom.
    void placeHoverPreview(const QPoint& globalCursor);
    // Shared by the first show, a swap onto another row, and the Alt re-scale.
    void revealHoverPreview(QListWidgetItem* it);
    QVariantAnimation* hoverFade();
    // The REAL cursor, not an event's claim: our own preview window sliding under the pointer fires spurious Leaves.
    bool pointerOverPreviewedIcon() const;
    void showRowMenu(QListWidgetItem* it, const QPoint& globalPos);
    void openSelected();
    void openSelectedInNewWindow();
    // Browser parity: a single click confirms then opens HERE, a double click opens at once, Ctrl/⌘
    // makes a NEW window; the single click is deferred by doubleClickInterval() so a double click cancels it.
    void scheduleRowOpen(QListWidgetItem* it);
    void fireRowOpen();
    void openRow(QListWidgetItem* it, bool newWindow, bool confirm);
    // confirmOpen_ false accepts straight away; Cancel keeps the dialog up.
    void finishOpen(Action act, bool newWindow, const QString& name);
    void deleteSelected();
    void moveToServerSelected();
    void copyToServerSelected();
    void moveToLocalSelected();
    void makeLocalCopySelected();
    void applyFilter();
    // Rows that are LEFT arrive out of sand via the shared ListFilterFade::dustRowIn.
    ListFilterFade* filterFade();
    void rebuildFilterOptions();
    void onItemChanged(QListWidgetItem* it);
    void updateBatchBar();
    void updateSelectAll();
    bool allFilteredChecked() const;
    void toggleSelectAll();
    void runBatch(Action act);
    // Browser parity: Enter saves via renameRequested, Esc or a click away discards.
    void beginInlineRename(QListWidgetItem* it);
    void closeInlineRename();
    void setColorSelected();
    void clearColorSelected();
    void emitSetColor(QListWidgetItem* it, const QString& color);
    QString rowColor(const QListWidgetItem* it) const;
    QString currentRowColor() const;
    QString rowKeyAt(int i) const;
    // Empty `keys` = every data row.
    void scatterRows(const QSet<QString>& keys = {});
    // Blanks `it` the instant its scatter starts, drops the item once the animation has played.
    void retireRow(QListWidgetItem* it);
    void createNew();
    void createBlank();

    QWidget* batchSelectedGroup_ = nullptr;
    // The open menu's "⋯" chip in global coords — where a window raised from that menu flies back to.
    QRect menuKebabRect_;
    bool openInLocalOk_ = false;
    bool openInServerOk_ = false;
    std::vector<Project> projects_;
    long long now_ = 0;
    stencil::net::ConnectionManager* connections_ = nullptr;
    QString activeProjectId_;
    QVBoxLayout* barSlot_ = nullptr;
    bool built_ = false;
    bool temporary_ = false;
    bool incognito_ = false;
    QHash<QString, QPixmap> thumbs_;
    // Keyed "serverUrl|id|version"; bounded because every edit anywhere bumps a version.
    LruCache<QString, QPixmap> remoteThumbs_{128};
    QVector<stencil::net::ServerProject> remote_;
    QSet<QString> thumbInFlight_;
    QLabel* hoverPreview_ = nullptr;
    QListWidgetItem* hoverItem_ = nullptr;
    QVariantAnimation* hoverFade_ = nullptr;
    bool hoverClosing_ = false;
    bool hoverZoomCursor_ = false;
    // -1 = none. Only its OWN hover styles the chip.
    int kebabHoverRow_ = -1;
    class ShimmerOverlay* kebabSweep_ = nullptr;
    QString tipRowText_;
    void setKebabHover(int row);
    QTimer* remoteTimer_ = nullptr;
    bool remoteBusy_ = false;
    bool remoteLoaded_ = false;
    QListWidget* list_ = nullptr;
    QPushButton* clearAllBtn_ = nullptr;
    QComboBox* filter_ = nullptr;
    ListFilterFade* filterFade_ = nullptr;
    ProjectDragZones* dragZones_ = nullptr;
    QComboBox* sortCombo_ = nullptr;
    QComboBox* searchModeCombo_ = nullptr;
    QLineEdit* search_ = nullptr;
    QPushButton* selectAllBtn_ = nullptr;
    QStringList knownServerUrls_;
    QSet<QString> checked_;
    QWidget* batchBar_ = nullptr;
    QLabel* batchCount_ = nullptr;
    QPushButton* batchToServer_ = nullptr;
    QPushButton* batchCopyServer_ = nullptr;
    QPushButton* batchToLocal_ = nullptr;
    QPushButton* batchCopyLocal_ = nullptr;
    QPushButton* batchRemove_ = nullptr;
    QPushButton* batchClear_ = nullptr;
    bool building_ = false;
    QTimer* clickTimer_ = nullptr;
    int pendingRow_ = -1;
    bool pendingNewWindow_ = false;
    Qt::KeyboardModifiers pressMods_;
    QPoint pressPos_;
    bool pressOnCheck_ = false;
    QWidget* renameBox_ = nullptr;
    bool confirmOpen_ = true;
    bool rowDragging_ = false;
    QVector<QPair<QString, QString>> batchItems_;
    Action action_ = Action::None;
    QString selectedId_;
    QString selectedServerUrl_;
    QString newName_;
    QString selectedColor_;
  };

}
