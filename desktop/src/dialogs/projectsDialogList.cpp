// The Projects dialog's list and footer phases: the reorderable list widget with its row
// delegate and gestures, and the footer's create actions plus the danger Clear All. Call order:
// projectsDialog.cpp's ctor, pinned by tests/projectsDialogRows.headless.cpp.
#include "projectsDialog.hpp"
#include "projectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "serverClient.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/modalChrome.hpp"
#include "../support/menuShimmer.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "iconSet.hpp"   // labelIcon
#include "reorderableListWidget.hpp"
#include "projectDragZones.hpp"
#include "../support/shimmerOverlay.hpp"
namespace stencil::gui {

  void ProjectsDialog::buildProjectList(QVBoxLayout* layout) {
    auto* reList = new ReorderableListWidget(this);
    list_ = reList;
    // Drag a row onto another to set a per-session Manual order (switches the Sort combo to
    // Manual). Rows are delegate-painted (no grip), so drags are view-initiated.
    reList->setDragEnabled(true);
    reList->onReorder = [this](int from, int to) {
      const int n = list_->count();
      if (from < 0 || from >= n) return;
      QVector<QString> keys(n);
      for (int i = 0; i < n; ++i) keys[i] = rowKeyAt(i);  // "" for placeholder rows
      if (to < 0) to = 0;
      if (to >= n) to = n - 1;
      const QString moved = keys[from];
      keys.remove(from);
      keys.insert(to, moved);
      QStringList order;
      for (const auto& k : keys) if (!k.isEmpty()) order << k;
      g_projectsManualOrder = order;
      g_projectsSortMode = QStringLiteral("manual");
      if (sortCombo_) { const int mi = sortCombo_->findData(g_projectsSortMode); if (mi >= 0) { QSignalBlocker b(sortCombo_); sortCombo_->setCurrentIndex(mi); } }
      refresh();
    };
    // Show/hide the main-window drag-out zones (Open here / Open in a new window / Remove) for the
    // duration of a row drag. The dialog covers the centre; zones are reachable in its margins.
    reList->onDragStart = [this] {
      rowDragging_ = true;
      if (clickTimer_) clickTimer_->stop();  // a drag is not a click
      if (dragZones_) dragZones_->begin(frameGeometry());
    };
    reList->onDragEnd = [this] {
      rowDragging_ = false;
      if (dragZones_) dragZones_->end();
    };
    // Drag a row OUT of the dialog and release in a zone → run that action. Open uses
    // openSelected() (local Open / remote OpenRemote); new-window + Remove are LOCAL-only (mirrors
    // the ⋯ menu; Remove routes through deleteSelected → its in-dialog Yes/No confirm).
    reList->onDragOut = [this](int rowIdx) {
      const auto zone = dragZones_ ? dragZones_->zoneAt(QCursor::pos()) : ProjectDragZones::Zone::None;
      if (zone == ProjectDragZones::Zone::None) return;  // released over the dialog / nowhere → keep
      QListWidgetItem* it = list_->item(rowIdx);
      if (!it || it->data(Qt::UserRole).isNull()) return;
      list_->setCurrentItem(it);
      const bool remote = !it->data(Qt::UserRole + 1).toString().isEmpty();
      using Zone = ProjectDragZones::Zone;
      // Open sets action_ + accept(); its confirm is shown by MainWindow AFTER the dialog
      // closes — never inside the drag release, which dismissed it. Remove confirms
      // in-dialog instead, deferred a turn (deleteSelected) for the same reason.
      if (zone == Zone::Here) {
        openSelected();  // local Open / remote OpenRemote
      } else if (zone == Zone::NewWindow) {
        if (remote) openSelected();  // no remote-in-new-window → open here
        else openSelectedInNewWindow();
      } else if (zone == Zone::Remove) {
        if (!remote) deleteSelected();  // server delete has no dialog action
      }
    };
    list_->setObjectName("projectsList");  // scopes the clearer row-checkbox style (theme.cpp)
    // Row icons hold each project's edited-result preview (local) or its stored
    // result/original image (server); size the list's icon column to fit them.
    list_->setIconSize(QSize(56, 56));
    list_->setSpacing(4);  // 8px gaps between row cards (browser .project-row margin-bottom)
    // Rows fit the viewport (the delegate clamps their width + elides) — never scroll sideways.
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Golden outline (not fill) around shared rows + the per-row "⋯" kebab,
    // mirroring the browser modal.
    list_->setItemDelegate(new ProjectRowDelegate(list_));
    // Hover-magnify + kebab clicks: track moves over the viewport to pop a larger
    // preview, and catch left-clicks on the "⋯" zone (handled in eventFilter).
    list_->viewport()->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    // Hover glass shimmer over the hovered row (browser .project-row's ui-shimmer
    // sweep — the same overlay the other rows/buttons in the app already play).
    installRowShimmer(list_);
    // Right-click anywhere on a row opens the same actions as the "⋯" kebab.
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
              QListWidgetItem* it = list_->itemAt(pos);
              if (!it || it->data(Qt::UserRole).isNull()) return;
              list_->setCurrentItem(it);
              showRowMenu(it, list_->viewport()->mapToGlobal(pos));
            });
    barSlot_->addWidget(list_, 1);   // directly under the bar, no layout gap of its own
    refresh();
  }

  void ProjectsDialog::wireRowGestures() {
    // Row-open gestures (see the header's scheduleRowOpen mapping).
    connect(list_, &QListWidget::itemClicked, this, &ProjectsDialog::scheduleRowOpen);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
      // THE crux: kill the pending single-click open before it can raise the
      // confirmation, so the dialog never flashes on the way to a double click.
      if (clickTimer_) clickTimer_->stop();
      if (pressOnCheck_) return;   // double-tapping the checkbox never opens
      // Dblclick on the NAME edits it inline (browser parity: the name's dblclick
      // never opens the row) — local rows only; anywhere else still opens.
      if (it && !it->data(Qt::UserRole).isNull() &&
          it->data(Qt::UserRole + 1).toString().isEmpty()) {
        auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
        if (del && del->nameRectFor(list_->row(it)).adjusted(-4, -3, 4, 3).contains(pressPos_)) {
          beginInlineRename(it);
          return;
        }
      }
      openRow(it, isNewWindowMod(pressMods_), /*confirm=*/false);
    });
    // Return/Enter on the focused row opens it like a single click (confirms).
    // Consumed so it can't also trigger the dialog's default button.
    list_->installEventFilter(this);
    installEventFilter(this);   // own deactivation → hide the hover preview
    connect(list_, &QListWidget::itemChanged, this, &ProjectsDialog::onItemChanged);
  }

  void ProjectsDialog::buildFooter(ModalChrome& chrome) {
    // Footer (browser settings-footer): the auto-save hint left, then the create actions
    // + danger Clear All, every enabled button accent-filled; Close lives in the header
    // pill. Per-row actions live on the "⋯" kebab + right-click menu, multi-row ones on
    // the batch toolbar above.
    QHBoxLayout* row = addModalFooter(
        chrome, tr("Projects auto-save · unopened projects expire after 7 days"));
    auto* blankBtn = new QPushButton("Blank image", this);
    makeModalCta(blankBtn, "image");
    blankBtn->setToolTip("Create a blank image (white, black, or any color) to draw on");
    auto* newBtn = new QPushButton("New editor", this);
    makeModalCta(newBtn, "plus-circle");
    newBtn->setToolTip("Create a new empty project from the current canvas");
    // "Clear All" only ever wipes local projects. When a server is connected, label it
    // "Clear All Local" so the button matches the actual (local-only) removal; the label is
    // kept in step reactively via ConnectionManager::changed() (see below).
    auto* clearAllBtn = new QPushButton("Clear All", this);
    clearAllBtn_ = clearAllBtn;
    clearAllBtn->setObjectName("dangerButton");  // red danger styling (mirrors the browser modal)
    clearAllBtn->setIcon(labelIcon("trash", QColor("#ffffff"), 15));
    clearAllBtn->setToolTip("Remove all local projects (server projects are not affected)");
    row->addWidget(blankBtn);
    row->addWidget(newBtn);
    row->addWidget(clearAllBtn);

    connect(newBtn, &QPushButton::clicked, this, &ProjectsDialog::createNew);
    connect(blankBtn, &QPushButton::clicked, this, &ProjectsDialog::createBlank);
    connect(clearAllBtn, &QPushButton::clicked, this, [this] {
      // Confirm HERE, parented to this dialog: the question then sits ON TOP of the still
      // open Projects window. Closing first and asking afterwards left the user answering
      // about a list they could no longer see.
      const int n = static_cast<int>(projects_.size());
      if (n == 0) return;
      ConfirmSpec spec;
      spec.title = tr("Clear all projects");
      spec.message = tr("Are you sure? This removes all %1 local project(s) and cannot be "
                        "undone. Server projects are not affected.")
                         .arg(n);
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) return;
      scatterRows();              // the whole list comes apart before it empties
      emit clearAllRequested();   // the owner removes them, then calls setProjects()
    });

    // Keep the local-only "Clear All" label honest: "Clear All Local" while any server is
    // connected, plain "Clear All" otherwise. Driven off ConnectionManager::changed() so it
    // flips the moment a server connects/disconnects (no poll), matching the browser modal.
    if (connections_) {
      auto syncClearAllLabel = [this] {
        clearAllBtn_->setText(connections_->urls().isEmpty() ? "Clear All" : "Clear All Local");
      };
      syncClearAllLabel();
      connect(connections_, &stencil::net::ConnectionManager::changed, this, syncClearAllLabel);
    }
    // Nothing local to clear ⇒ nothing to offer (browser parity: projectsModal disables it
    // when only the synthetic "temporary (unsaved)" row is on screen).
    clearAllBtn->setEnabled(!projects_.empty());
  }

}  // namespace stencil::gui
