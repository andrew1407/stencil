// The Projects dialog's list and footer phases; call order pinned by tests/dialogs/projects/list/ProjectsDialogRows.headless.cpp.
#include "ProjectsDialog.hpp"
#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "../../../support/modal/modalChrome.hpp"
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include "iconSet.hpp"
#include "ReorderableListWidget.hpp"
#include "ProjectDragZones.hpp"
#include "ProjectDragMenu.hpp"
#include "../../../support/motion/ShimmerOverlay.hpp"
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QPair>
#include <QTimer>
#include <memory>
namespace stencil::gui {

  void ProjectsDialog::buildProjectList() {
    auto* reList = new ReorderableListWidget(this);
    list = reList;
    // Rows are delegate-painted (no grip), so drags are view-initiated.
    reList->setDragEnabled(true);
    // The header's ⋯ and Close while a row is held; `held` is that row's id and server.
    auto* header = new ProjectDragMenu(findChild<QLabel*>(QStringLiteral("modalTitle")),
                                       findChild<QPushButton*>(QStringLiteral("modalClosePill")), this);
    auto held = std::make_shared<QPair<QString, QString>>();
    const auto heldItem = [this, held]() -> QListWidgetItem* {
      for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* it = list->item(i);
        if (!it->data(Qt::UserRole).isNull() && it->data(Qt::UserRole).toString() == held->first &&
            it->data(Qt::UserRole + 1).toString() == held->second)
          return it;
      }
      return nullptr;
    };
    header->openMenu = [this, heldItem](const QPoint& at, const QPoint& from) { return showRowMenu(heldItem(), at, &from); };
    reList->onReorder = [this, header](int from, int to) {
      if (header->claimed()) return;
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
    reList->onDragStart = [this, header, held] {
      press.rowDragging = true;
      if (press.clickTimer) press.clickTimer->stop();
      if (dragZones) dragZones->begin(this);
      const QListWidgetItem* it = list->currentItem();
      *held = it ? qMakePair(it->data(Qt::UserRole).toString(), it->data(Qt::UserRole + 1).toString())
                 : QPair<QString, QString>();
      header->begin(!held->first.isEmpty() &&
                    (held->second.isEmpty() ? held->first == activeProjectId
                                            : held->second == press.openServerUrl && held->first == press.openServerId));
    };
    reList->onDragEnd = [this, header, heldItem] {
      press.rowDragging = false;
      if (dragZones) dragZones->end();
      const ProjectDragMenu::Release out = header->finish(QCursor::pos());
      if (out.close) QTimer::singleShot(0, this, [this] { emit closeProjectRequested(); });
      if (!out.taken) return;
      // The taken item runs as its click would, on the held row, once the drag loop is gone.
      QTimer::singleShot(0, this, [this, heldItem, act = out.taken, menu = out.menu] {
        if (QListWidgetItem* it = heldItem()) list->setCurrentItem(it);
        hover.menuKebabRect = menu ? menu->property(HELD_MENU_KEBAB_PROP).toRect() : QRect();
        if (act) act->trigger();
        hover.menuKebabRect = QRect();
        if (menu) menu->deleteLater();
      });
    };
    // New-window + Remove are LOCAL-only (mirrors the ⋯ menu).
    reList->onDragOut = [this, header](int rowIdx) {
      if (header->claimed()) return;
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
