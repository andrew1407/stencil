// The Projects dialog's list and footer phases; call order pinned by tests/ProjectsDialogRows.headless.cpp.
#include "ProjectsDialog.hpp"
#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ServerClient.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/modalChrome.hpp"
#include "../support/MenuShimmer.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "iconSet.hpp"
#include "ReorderableListWidget.hpp"
#include "ProjectDragZones.hpp"
#include "../support/ShimmerOverlay.hpp"
namespace stencil::gui {

  void ProjectsDialog::buildProjectList(QVBoxLayout* layout) {
    auto* reList = new ReorderableListWidget(this);
    list_ = reList;
    // Rows are delegate-painted (no grip), so drags are view-initiated.
    reList->setDragEnabled(true);
    reList->onReorder = [this](int from, int to) {
      const int n = list_->count();
      if (from < 0 || from >= n) return;
      QVector<QString> keys(n);
      for (int i = 0; i < n; ++i) keys[i] = rowKeyAt(i);
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
    reList->onDragStart = [this] {
      press_.rowDragging = true;
      if (press_.clickTimer) press_.clickTimer->stop();
      if (dragZones_) dragZones_->begin(frameGeometry());
    };
    reList->onDragEnd = [this] {
      press_.rowDragging = false;
      if (dragZones_) dragZones_->end();
    };
    // New-window + Remove are LOCAL-only (mirrors the ⋯ menu).
    reList->onDragOut = [this](int rowIdx) {
      const auto zone = dragZones_ ? dragZones_->zoneAt(QCursor::pos()) : ProjectDragZones::Zone::NONE;
      if (zone == ProjectDragZones::Zone::NONE) return;
      QListWidgetItem* it = list_->item(rowIdx);
      if (!it || it->data(Qt::UserRole).isNull()) return;
      list_->setCurrentItem(it);
      const bool remote = !it->data(Qt::UserRole + 1).toString().isEmpty();
      typedef ProjectDragZones::Zone Zone;
      // Open's confirm is shown by MainWindow AFTER the dialog closes — inside the drag release it was dismissed.
      if (zone == Zone::HERE) {
        openSelected();
      } else if (zone == Zone::NEW_WINDOW) {
        if (remote) openSelected();
        else openSelectedInNewWindow();
      } else if (zone == Zone::REMOVE) {
        if (!remote) deleteSelected();
      }
    };
    list_->setObjectName("projectsList");
    list_->setIconSize(QSize(56, 56));
    list_->setSpacing(4);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setItemDelegate(new ProjectRowDelegate(list_));
    list_->viewport()->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    installRowShimmer(list_);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
              QListWidgetItem* it = list_->itemAt(pos);
              if (!it || it->data(Qt::UserRole).isNull()) return;
              list_->setCurrentItem(it);
              showRowMenu(it, list_->viewport()->mapToGlobal(pos));
            });
    barSlot_->addWidget(list_, 1);
    refresh();
  }

  void ProjectsDialog::wireRowGestures() {
    connect(list_, &QListWidget::itemClicked, this, &ProjectsDialog::scheduleRowOpen);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
      // The pending single-click open must die before it can raise the confirmation.
      if (press_.clickTimer) press_.clickTimer->stop();
      if (press_.pressOnCheck) return;
      // Browser parity: the name's dblclick renames inline (local rows) and never opens.
      if (it && !it->data(Qt::UserRole).isNull() &&
          it->data(Qt::UserRole + 1).toString().isEmpty()) {
        auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
        if (del && del->nameRectFor(list_->row(it)).adjusted(-4, -3, 4, 3).contains(press_.pressPos)) {
          beginInlineRename(it);
          return;
        }
      }
      openRow(it, isNewWindowMod(press_.pressMods), /*confirm=*/false);
    });
    // Consumed so it cannot also trigger the dialog's default button.
    list_->installEventFilter(this);
    installEventFilter(this);
    connect(list_, &QListWidget::itemChanged, this, &ProjectsDialog::onItemChanged);
  }

  void ProjectsDialog::buildFooter(ModalChrome& chrome) {
    // Browser settings-footer; Close lives in the header pill.
    QHBoxLayout* row = addModalFooter(
        chrome, tr("Projects auto-save · unopened projects expire after 7 days"));
    auto* blankBtn = new QPushButton("Blank image", this);
    makeModalCta(blankBtn, "image");
    blankBtn->setToolTip("Create a blank image (white, black, or any color) to draw on");
    auto* newBtn = new QPushButton("New editor", this);
    makeModalCta(newBtn, "plus-circle");
    newBtn->setToolTip("Create a new empty project from the current canvas");
    // "Clear All Local" while a server is connected — the removal is local-only.
    auto* clearAllBtn = new QPushButton("Clear All", this);
    clearAllBtn_ = clearAllBtn;
    clearAllBtn->setObjectName("dangerButton");
    clearAllBtn->setIcon(labelIcon("trash", QColor("#ffffff"), 15));
    clearAllBtn->setToolTip("Remove all local projects (server projects are not affected)");
    row->addWidget(blankBtn);
    row->addWidget(newBtn);
    row->addWidget(clearAllBtn);

    connect(newBtn, &QPushButton::clicked, this, &ProjectsDialog::createNew);
    connect(blankBtn, &QPushButton::clicked, this, &ProjectsDialog::createBlank);
    connect(clearAllBtn, &QPushButton::clicked, this, [this] {
      // Parented to this dialog so the question sits ON TOP of the still-open list.
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
      scatterRows();
      emit clearAllRequested();
    });

    if (connections_) {
      auto syncClearAllLabel = [this] {
        clearAllBtn_->setText(connections_->urls().isEmpty() ? "Clear All" : "Clear All Local");
      };
      syncClearAllLabel();
      connect(connections_, &stencil::net::ConnectionManager::changed, this, syncClearAllLabel);
    }
    // Browser parity: disabled when only the synthetic temporary row is on screen.
    clearAllBtn->setEnabled(!projects_.empty());
  }

}  // namespace stencil::gui
