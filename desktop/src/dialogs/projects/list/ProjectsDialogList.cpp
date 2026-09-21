// The Projects dialog's list and footer phases; call order pinned by tests/ProjectsDialogRows.headless.cpp.
#include "ProjectsDialog.hpp"
#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ServerClient.hpp"
#include "../../../support/guiHelpers.hpp"
#include "../../../support/modal/modalChrome.hpp"
#include "../../../support/motion/MenuShimmer.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "iconSet.hpp"
#include "ReorderableListWidget.hpp"
#include "ProjectDragZones.hpp"
#include "../../../support/motion/ShimmerOverlay.hpp"
namespace stencil::gui {

  void ProjectsDialog::buildProjectList(QVBoxLayout* layout) {
    auto* reList = new ReorderableListWidget(this);
    list = reList;
    // Rows are delegate-painted (no grip), so drags are view-initiated.
    reList->setDragEnabled(true);
    reList->onReorder = [this](int from, int to) {
      const int n = list->count();
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
      if (sortCombo) { const int mi = sortCombo->findData(g_projectsSortMode); if (mi >= 0) { QSignalBlocker b(sortCombo); sortCombo->setCurrentIndex(mi); } }
      refresh();
    };
    reList->onDragStart = [this] {
      press.rowDragging = true;
      if (press.clickTimer) press.clickTimer->stop();
      if (dragZones) dragZones->begin(frameGeometry());
    };
    reList->onDragEnd = [this] {
      press.rowDragging = false;
      if (dragZones) dragZones->end();
    };
    // New-window + Remove are LOCAL-only (mirrors the ⋯ menu).
    reList->onDragOut = [this](int rowIdx) {
      const auto zone = dragZones ? dragZones->zoneAt(QCursor::pos()) : ProjectDragZones::Zone::NONE;
      if (zone == ProjectDragZones::Zone::NONE) return;
      QListWidgetItem* it = list->item(rowIdx);
      if (!it || it->data(Qt::UserRole).isNull()) return;
      list->setCurrentItem(it);
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
    list->setObjectName("projectsList");
    list->setIconSize(QSize(56, 56));
    list->setSpacing(4);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setItemDelegate(new ProjectRowDelegate(list));
    list->viewport()->setMouseTracking(true);
    list->viewport()->installEventFilter(this);
    installRowShimmer(list);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
              QListWidgetItem* it = list->itemAt(pos);
              if (!it || it->data(Qt::UserRole).isNull()) return;
              list->setCurrentItem(it);
              showRowMenu(it, list->viewport()->mapToGlobal(pos));
            });
    barSlot->addWidget(list, 1);
    refresh();
  }

  void ProjectsDialog::wireRowGestures() {
    connect(list, &QListWidget::itemClicked, this, &ProjectsDialog::scheduleRowOpen);
    connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
      // The pending single-click open must die before it can raise the confirmation.
      if (press.clickTimer) press.clickTimer->stop();
      if (press.pressOnCheck) return;
      // Browser parity: the name's dblclick renames inline (local rows) and never opens.
      if (it && !it->data(Qt::UserRole).isNull() &&
          it->data(Qt::UserRole + 1).toString().isEmpty()) {
        auto* del = static_cast<ProjectRowDelegate*>(list->itemDelegate());
        if (del && del->nameRectFor(list->row(it)).adjusted(-4, -3, 4, 3).contains(press.pressPos)) {
          beginInlineRename(it);
          return;
        }
      }
      openRow(it, isNewWindowMod(press.pressMods), /*confirm=*/false);
    });
    // Consumed so it cannot also trigger the dialog's default button.
    list->installEventFilter(this);
    installEventFilter(this);
    connect(list, &QListWidget::itemChanged, this, &ProjectsDialog::onItemChanged);
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
    this->clearAllBtn = clearAllBtn;
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
      const int n = static_cast<int>(projects.size());
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

    if (connections) {
      auto syncClearAllLabel = [this] {
        this->clearAllBtn->setText(connections->urls().isEmpty() ? "Clear All" : "Clear All Local");
      };
      syncClearAllLabel();
      connect(connections, &stencil::net::ConnectionManager::changed, this, syncClearAllLabel);
    }
    // Browser parity: disabled when only the synthetic temporary row is on screen.
    clearAllBtn->setEnabled(!projects.empty());
  }

}  // namespace stencil::gui
