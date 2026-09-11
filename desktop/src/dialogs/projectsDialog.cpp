#include "../support/searchCombo.hpp"
#include "projectsDialog.hpp"

#include "projectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "guiHelpers.hpp"
#include "fetchGuard.hpp"
#include "iconSet.hpp"
#include "expirationDialog.hpp"
#include "projectDragZones.hpp"
#include "projectsStore.hpp"
#include "reorderableListWidget.hpp"
#include "../support/scrollReveal.hpp"  // revealOpacityForItem (scroll edge fade)
#include "../app/mainWindowHelpers.hpp"   // kNameChipBox / kNameChipGlyph — the shared chip
#include "../support/controlReveal.hpp"       // the rename ✓/✗ form/come apart as dust
#include "../support/flowLayout.hpp"           // the filter row + batch bar wrap, never clip
#include "../support/disintegrateOverlay.hpp"  // deleted rows come apart
#include "../support/displayName.hpp"          // shortName for the remove confirm
#include "../support/dissolveEffect.hpp"      // scroll-edge grain dissolve
#include "../support/filterFade.hpp"          // filtered-out rows fade + collapse
#include "../support/guiHelpers.hpp"
#include "../support/menuReveal.hpp"
#include "../support/theme.hpp"          // themePalette().danger for the Remove row
#include "../support/menuDangerRow.hpp"    // the red "Remove" row (label + glyph)
#include "../support/menuShimmer.hpp"         // ctx rows' glass hover sweep
#include "../support/shimmerOverlay.hpp"      // hovered row's glass sweep (browser ui-shimmer)
#include "../support/modalChrome.hpp"         // the browser modal shell
#include "../support/modalReveal.hpp"         // animated colour picker
#include "serverClient.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QDate>
#include <QLocale>
#include <QComboBox>
#include <QMenu>
#include <QColor>
#include <QCursor>
#include <QFont>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include "appTooltip.hpp"
#include "shimmerOverlay.hpp"
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <limits>
#include <memory>
#include <QVariant>
#include <QVariantAnimation>
#include <algorithm>
#include <optional>

namespace stencil::gui {

  namespace fetchGuard = stencil::net::fetchGuard;

  ProjectsDialog::ProjectsDialog(const std::vector<Project>& projects, long long now,
                                 stencil::net::ConnectionManager* connections,
                                 const QHash<QString, QPixmap>& thumbs,
                                 QWidget* parent,
                                 const QString& activeProjectId,
                                 const QColor& accentColor)
      : QDialog(parent), projects_(projects), now_(now),
        connections_(connections), thumbs_(thumbs),
        activeProjectId_(activeProjectId) {
    // `accentColor` is unused now: the delegate reads the installed palette's
    // Highlight/Link (theme.cpp publishes accent + accent-2 there). Kept in the
    // signature so callers stay untouched.
    Q_UNUSED(accentColor);
    setWindowTitle("Projects");
    // The browser modal's footprint (app-modal width, min-height min(560px, 82vh))
    // — the row text elides / stacks instead of demanding width.
    setMinimumSize(kModalWidth, 420);
    resize(kModalWidth, 560);

    // Most-recently-updated first, matching the browser store ordering.
    std::sort(projects_.begin(), projects_.end(),
              [](const Project& a, const Project& b) {
                return a.meta.updatedAt > b.meta.updatedAt;
              });

    // Browser projectsModal.js parity: the shared modal shell — glyph + "Projects"
    // title with the outlined Close pill — instead of a bare bold caption.
    ModalChrome chrome = installModalChrome(this, "layers", tr("Projects"));
    QVBoxLayout* layout = chrome.body;

    // Search sits on its own full-width row, ABOVE the filter selects — mirroring the
    // browser modal's layout (a shared single-row bar squeezed the search box; the
    // browser resolved that by giving search the whole row first).
    {
      auto* srow = new QHBoxLayout;
      search_ = new QLineEdit(this);
      search_->setPlaceholderText(tr("Search projects…"));
      search_->setClearButtonEnabled(true);
      srow->addWidget(search_, 1);
      layout->addLayout(srow);
      connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    }

    // Filter row: a compact "Show:" dropdown (All / Local / all-servers / a specific connected
    // server) + sort/search-mode selects + Select all (mirrors the browser modal's row below).
    // A FlowLayout: squeezed, the selects wrap onto a second line at their natural widths
    // instead of being crushed or cut off at the edge (browser .modal-search-bar flex-wrap).
    {
      auto* frow = new FlowLayout(nullptr, 0, 8, 6);
      filter_ = new SearchComboBox(this, /*searchable=*/false);
      filter_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
      filter_->setMaximumWidth(260);
      filter_->setToolTip("Filter the list: all, local only, or a specific server");
      rebuildFilterOptions();
      frow->addWidget(filter_);                       // natural width — no stretch
      // Sort mode (mirrors the browser modal): the data role carries the mode key.
      sortCombo_ = new SearchComboBox(this, /*searchable=*/false);
      sortCombo_->setToolTip("Sort projects (drag a row to set a manual order)");
      sortCombo_->addItem(tr("Name"), QStringLiteral("name"));
      sortCombo_->addItem(tr("Local first"), QStringLiteral("local"));
      sortCombo_->addItem(tr("Server first"), QStringLiteral("server"));
      sortCombo_->addItem(tr("Newest"), QStringLiteral("date-desc"));
      sortCombo_->addItem(tr("Oldest"), QStringLiteral("date-asc"));
      sortCombo_->addItem(tr("Manual order"), QStringLiteral("manual"));
      {
        const int mi = sortCombo_->findData(g_projectsSortMode);
        sortCombo_->setCurrentIndex(mi >= 0 ? mi : 0);
      }
      frow->addWidget(sortCombo_);
      // Search-mode (what the search box matches): name+keywords (default), names, keywords.
      searchModeCombo_ = new SearchComboBox(this, /*searchable=*/false);
      searchModeCombo_->setToolTip("What the search box matches");
      searchModeCombo_->addItem(tr("Name + keywords"), QStringLiteral("common"));
      searchModeCombo_->addItem(tr("Names only"), QStringLiteral("names"));
      searchModeCombo_->addItem(tr("Keywords only"), QStringLiteral("keywords"));
      frow->addWidget(searchModeCombo_);
      layout->addLayout(frow);
      connect(filter_, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
      connect(sortCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        g_projectsSortMode = sortCombo_->currentData().toString();
        refresh();
      });
      connect(searchModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
    }

    // Batch-select toolbar — shown whenever the filtered view has selectable rows
    // (it hosts Select all), with the selection-only controls coming and going with
    // the checked set. Labels + glyphs mirror the browser modal's batch bar; direction
    // buttons show by selection homogeneity (all-local → to-server; all-server → to-local).
    // The bar WRAPS (browser .projects-batch-bar flex-wrap, gap 10): a narrow dialog
    // never cuts "Remove selected" off at its edge.
    {
      batchBar_ = new QWidget(this);
      auto* bh = new FlowLayout(batchBar_, 0, 10, 6);
      batchCount_ = new QLabel("0 selected", this);
      bh->addWidget(batchCount_);
      // Select/deselect every row in the CURRENT filtered view (browser: `selectables`).
      // Accent-filled batch actions (browser parity): the accent rides the shared
      // accentCta property (theme.cpp), leaving objectNames free for the tests.
      const auto accentBtn = [this](const QString& label, const QString& icon,
                                    const QString& tip) {
        auto* b = new QPushButton(label, this);
        b->setToolTip(tip);
        b->setProperty("accentCta", true);
        b->setIcon(labelIcon(icon, QColor("#ffffff"), 13));
        return b;
      };
      selectAllBtn_ = accentBtn(tr("Select all"), "check",
                                "Select every project in the current filtered view");
      selectAllBtn_->setObjectName("projectsSelectAll");   // the batch-bar test finds it
      bh->addWidget(selectAllBtn_);
      connect(selectAllBtn_, &QPushButton::clicked, this, &ProjectsDialog::toggleSelectAll);
      batchToServer_ = accentBtn(tr("Move to server"), "server",
                                 "Move the checked local projects to a server");
      batchCopyServer_ = accentBtn(tr("Copy to server"), "copy",
                                   "Copy the checked local projects to a server");
      batchToLocal_ = accentBtn(tr("Move to local"), "download",
                                "Move the checked server projects to local storage");
      batchCopyLocal_ = accentBtn(tr("Copy to local"), "copy",
                                  "Copy the checked server projects to local storage");
      batchRemove_ = new QPushButton("Remove selected", this);
      batchRemove_->setToolTip("Remove the checked projects");
      batchRemove_->setObjectName("dangerButton");
      batchRemove_->setIcon(labelIcon("trash", QColor("#ffffff"), 13));
      batchClear_ = accentBtn("Clear", "x", "Clear the current checkbox selection");
      // The selection-only actions live in ONE group so the bar's swap is a single flight,
      // not one per button: a control's dust is photographed where it sits at that instant,
      // and siblings revealed in the same turn are still animating their own width.
      // Browser twin: .projects-batch-selected in projectsModal.js. At rest the group
      // wraps too (gap 6); only while its slot slides open or shut does it hold one line.
      batchSelectedGroup_ = new QWidget(batchBar_);
      auto* gh = new FlowLayout(batchSelectedGroup_, 0, 6, 6);
      gh->setLineSizeHint(true);
      gh->setHoldsLineWhileCapped(true);
      gh->addWidget(batchToServer_);
      gh->addWidget(batchCopyServer_);
      gh->addWidget(batchToLocal_);
      gh->addWidget(batchCopyLocal_);
      gh->addWidget(batchClear_);
      gh->addWidget(batchRemove_);   // destructive last, as in the browser's bar
      batchSelectedGroup_->setVisible(false);
      bh->addWidget(batchSelectedGroup_);
      batchBar_->setVisible(false);
      // The bar and the list share ONE zero-spacing slot, and the gap under the bar is the
      // bar's OWN bottom margin. So a closing strip slides its whole footprint away
      // together (support/controlReveal closeBarSlot) instead of dropping the layout's
      // spacing in one frame at the end. Metrics unchanged: 10px above, 10px below.
      bh->setContentsMargins(0, 0, 0, kBodySpacing);
      barSlot_ = new QVBoxLayout;
      barSlot_->setContentsMargins(0, 0, 0, 0);
      barSlot_->setSpacing(0);
      barSlot_->addWidget(batchBar_);
      layout->addLayout(barSlot_, 1);   // the list joins it below (see addWidget(list_))
      connect(batchToServer_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchMoveToServer); });
      connect(batchCopyServer_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchCopyToServer); });
      connect(batchToLocal_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchMoveToLocal); });
      connect(batchCopyLocal_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchCopyToLocal); });
      connect(batchRemove_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchRemove); });
      connect(batchClear_, &QPushButton::clicked, this, [this] { checked_.clear(); refresh(); });
    }

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

    // Every control in the window gets the app's glass hover sweep, the way the toolbar
    // and the selection panel do; the list already had its row version. The browser's rule
    // covers the same set: `button, .btn-icon, .btn-icon-text` plus its text inputs.
    installHoverShimmerIn(this);

    // Server (shared) projects: list them now and keep them live with a periodic
    // re-list while the dialog is open. The desktop talks REST only, so this
    // polling stands in for the browser modal's WebSocket project-event feed.
    if (connections_ && !connections_->urls().isEmpty()) {
      // Defer the (synchronous) first listing to the next event-loop turn so the
      // dialog paints immediately with local rows + a "Loading shared projects…"
      // placeholder, instead of freezing on the network before it even shows.
      QTimer::singleShot(0, this, &ProjectsDialog::refreshRemote);
      remoteTimer_ = new QTimer(this);
      remoteTimer_->setInterval(5000);
      connect(remoteTimer_, &QTimer::timeout, this, &ProjectsDialog::refreshRemote);
      remoteTimer_->start();
    }
  }

  bool ProjectsDialog::eventFilter(QObject* obj, QEvent* ev) {
    // The hover preview is a ToolTip window: switching window or app delivers the list no
    // Leave, and the popup would float over whatever came forward. Verified against the
    // CURSOR — the preview materializing under a stationary pointer makes the platform
    // emit spurious Leave/deactivate, and an unconditional hide loops the dust.
    if (hoverPreview_ && obj == this &&
        (ev->type() == QEvent::WindowDeactivate || ev->type() == QEvent::ApplicationDeactivate) &&
        !(QGuiApplication::applicationState() == Qt::ApplicationActive &&
          pointerOverPreviewedIcon())) {
      hideHoverPreview();
    }
    // Alt pressed/released while the preview is up: re-scale it in place from the
    // source pixmap it carries (held = 2x, released = back to the glance size), and
    // replay its gather — the size change is a real appearance, same as landing on a
    // new row.
    if (hoverPreview_ && hoverPreview_->isVisible() && hoverItem_ &&
        (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) &&
        static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(ev)->isAutoRepeat()) {
      const QPixmap src = hoverPreview_->property("srcPixmap").value<QPixmap>();
      if (!src.isNull()) {
        const int edge = (ev->type() == QEvent::KeyPress) ? kHoverPreviewAltPx : kHoverPreviewPx;
        hoverPreview_->setPixmap(
            src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        hoverPreview_->adjustSize();
        // Re-clamped for the new size (the doubled glance can run off the screen) and
        // re-formed: the size change IS a re-appearance, dust and hold-fade included.
        placeHoverPreview(QCursor::pos());
        revealHoverPreview(hoverItem_);
      }
    }
    // Return/Enter on the focused row = a plain open (with confirmation), and
    // consumed so the dialog's default button can't also fire.
    if (list_ && obj == list_ && ev->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(ev);
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
        if (clickTimer_) clickTimer_->stop();
        openRow(list_->currentItem(), isNewWindowMod(ke->modifiers()), /*confirm=*/true);
        return true;
      }
    }
    if (list_ && obj == list_->viewport()) {
      // On a width change, recompute item sizeHints so rows re-clamp + re-elide to the new
      // viewport width (the delegate caps width to the viewport).
      if (ev->type() == QEvent::Resize) list_->doItemsLayout();
      // Left-click on the "⋯" kebab strip pops the row's menu (consume it so it
      // doesn't also start a drag/selection); it's the right-click menu's twin.
      // The magnified preview IS the tooltip — never stack the text one on it.
      if (ev->type() == QEvent::ToolTip && hoverPreview_ && hoverPreview_->isVisible())
        return true;
      if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        // itemClicked/itemDoubleClicked carry no modifiers — remember the ones
        // that were down for the press that produces them (and where it landed,
        // for the name-dblclick rename hit test).
        pressMods_ = me->modifiers();
        const QPoint vpos = me->position().toPoint();
        pressPos_ = vpos;
        QListWidgetItem* it = list_->itemAt(vpos);
        // A press on the checkbox strip is a SELECTION gesture: it must toggle
        // the box and nothing else (no row-open, no confirm, dialog stays).
        const QRect vr = it ? list_->visualItemRect(it) : QRect();
        pressOnCheck_ = me->button() == Qt::LeftButton && it &&
                        QRect(vr.left(), vr.top(), 34, vr.height()).contains(vpos);
        if (me->button() == Qt::LeftButton && it &&
            !it->data(Qt::UserRole).isNull() &&
            kebabZone(list_->visualItemRect(it)).contains(vpos)) {
          list_->setCurrentItem(it);
          showRowMenu(it, me->globalPosition().toPoint());
          return true;
        }
      }
      if (ev->type() == QEvent::ToolTip) {
        // The rows' tooltips go through the app's tooltip, not Qt's plain label:
        // AppTooltip's filter skips item views by design (they resolve a per-index tip in
        // viewportEvent), so these rows were the one place still showing Qt's box.
        auto* he = static_cast<QHelpEvent*>(ev);
        QListWidgetItem* it = list_->itemAt(he->pos());
        // The "⋯" carries its OWN tip, as the browser's per-row button does
        // (projectsModal.js menuBtn.title), not the row's picture info.
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list_->visualItemRect(it)).contains(he->pos());
        const QString tip = !it ? QString()
                                : (onKebab ? kKebabTip : it->toolTip());
        if (tip.isEmpty()) { gui::appTooltip()->hideTip(); return true; }
        // …forming out of the CURSOR, which is where the browser's own tooltip flies from
        // and back into (ui/tooltip.js dust). Left to the default it grew out of the
        // owner's centre — here the whole viewport, i.e. the middle of the list.
        tipRowText_ = tip;
        gui::appTooltip()->showFor(list_->viewport(), tip, he->globalPos(),
                                   QRect(he->globalPos(), QSize(1, 1)));
        return true;
      }
      if (ev->type() == QEvent::MouseMove) {
        const QPoint vpos = static_cast<QMouseEvent*>(ev)->position().toPoint();
        QListWidgetItem* it = list_->itemAt(vpos);
        // Which row's "⋯" the pointer is actually on — the chip styles itself only for
        // that, never for a hover anywhere else on the row.
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list_->visualItemRect(it)).contains(vpos);
        setKebabHover(onKebab ? list_->row(it) : -1);
        // …and the tooltip TRAVELS with the pointer while it is up: moveTo SLIDES it, with
        // no re-measure and no entrance. Going back through showFor on every move re-ran
        // its appearance bookkeeping and made the tip stutter and jump.
        if (auto* tip = gui::appTooltip(); tip->isVisible() && tip->owner() == list_->viewport()) {
          // Which tip belongs HERE — the kebab's own, or the row's picture info. Reading
          // only the row's would slide it on over the "⋯", where a different tip is due.
          const QString text = !it ? QString() : (onKebab ? kKebabTip : it->toolTip());
          if (text.isEmpty() || text != tipRowText_) tip->hideTip();
          else tip->moveTo(static_cast<QMouseEvent*>(ev)->globalPosition().toPoint());
        }
        const QPixmap src = it ? it->data(Qt::UserRole + 2).value<QPixmap>() : QPixmap();
        // Magnify only while over the THUMBNAIL itself — the decoration rect the
        // delegate recorded at paint time (checkbox excluded), so the hit test
        // matches the pixels exactly and cannot flicker against a re-derived guess.
        bool overIcon = false;
        if (it && !src.isNull()) {
          const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
          const QRect dec = del ? del->iconRectFor(list_->row(it)) : QRect();
          overIcon = dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
        }
        // The magnifiable thumb advertises itself (browser .project-thumb img
        // cursor:zoom-in); back to the default arrow the moment the pointer leaves it.
        if (overIcon != hoverZoomCursor_) {
          if (overIcon) list_->viewport()->setCursor(zoomInCursor());
          else list_->viewport()->unsetCursor();
          hoverZoomCursor_ = overIcon;
        }
        if (overIcon) {
          // An APPEARANCE (first show, or a swap onto a different row) forms out of
          // the row and anchors there; moves that stay on the same thumb keep the
          // preview's dust alone (it replayed per move before, scattering motes with
          // every pixel of travel) but still carry the box along with
          // the pointer, browser positionZoom parity.
          const QPoint cur = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
          const bool appearing = !hoverPreview_ || !hoverPreview_->isVisible() ||
                                 hoverClosing_ || hoverItem_ != it;
          if (appearing) {
            // A different row's preview still up? Dust it back into ITS row first —
            // the browser's old-thumb mouseleave plays surfaceOut before the new
            // thumb's mouseenter gathers.
            if (hoverPreview_ && hoverPreview_->isVisible() && hoverItem_ && hoverItem_ != it)
              hideHoverPreview();
            if (!hoverPreview_) {
              // Input-transparent, like the browser zoom's pointer-events:none — the
              // clamped glance (the doubled Alt one especially) can land UNDER the
              // pointer, and a window that eats the hover makes the viewport churn
              // Leave/Enter, replaying the dust on every move.
              hoverPreview_ = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint |
                                                   Qt::WindowTransparentForInput |
                                                   Qt::WindowDoesNotAcceptFocus);
              hoverPreview_->setAttribute(Qt::WA_ShowWithoutActivating, true);
              hoverPreview_->setStyleSheet(
                  "QLabel{background:#1e1e1e;border:2px solid #d4a017;"
                  "border-radius:8px;padding:4px;}");
            }
            // Alt HELD magnifies the glance (chat HoverPreview / browser parity). The
            // SOURCE rides along so the Alt toggle below can re-scale without a move.
            const int edge =
                (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
                    ? kHoverPreviewAltPx
                    : kHoverPreviewPx;
            hoverPreview_->setProperty("srcPixmap", src);
            hoverPreview_->setPixmap(
                src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            hoverPreview_->adjustSize();
            placeHoverPreview(cur);
            // Shown, but held invisible behind its gathering motes (revealHoverPreview
            // ramps it up); reduced motion shows the end state at once.
            hoverPreview_->setWindowOpacity(support::motionReduced() ? 1.0 : 0.0);
            hoverPreview_->show();
            hoverItem_ = it;
            revealHoverPreview(it);
          } else {
            placeHoverPreview(cur);
          }
        } else if (hoverPreview_) {
          hideHoverPreview();
        }
      } else if (ev->type() == QEvent::Leave) {
        setKebabHover(-1);   // …or the chip stays lit after the pointer has gone
        if (auto* tip = gui::appTooltip(); tip->owner() == list_->viewport()) tip->hideTip();
        // Same verified-against-the-cursor rule as the deactivate backstop above: a
        // Leave fired by our own preview window sliding under the pointer must not
        // hide what the pointer is still hovering.
        if (!pointerOverPreviewedIcon()) {
          if (hoverPreview_) hideHoverPreview();
          if (hoverZoomCursor_) {
            list_->viewport()->unsetCursor();
            hoverZoomCursor_ = false;
          }
        }
      }
    }
    // Inline rename editor: Esc cancels (and must NOT fall through to the dialog's
    // own reject), click-away (focus loss) discards — browser blur parity. The ✓/✗
    // buttons are NoFocus, so their clicks land before any focus-out can fire.
    if (renameBox_ && obj->parent() == renameBox_) {
      if (ev->type() == QEvent::KeyPress &&
          static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
        closeInlineRename();
        return true;
      }
      if (ev->type() == QEvent::FocusOut) closeInlineRename();
    }
    return QDialog::eventFilter(obj, ev);
  }

  // Replace the listed projects and repaint (see the header): lets the owner act on a
  // request without the dialog having to close and be reopened.
  void ProjectsDialog::setProjects(const std::vector<Project>& projects) {
    setProjects(projects, temporary_, incognito_);
  }

  // …and the same repaint carrying the owner window's session state, so a removal that
  // also blanks the editor lands as ONE frame (see the header).
  void ProjectsDialog::setProjects(const std::vector<Project>& projects, bool temporary,
                                   bool incognito) {
    projects_ = projects;
    temporary_ = temporary;
    incognito_ = incognito;
    if (clearAllBtn_) clearAllBtn_->setEnabled(!projects_.empty());   // nothing left to clear
    refresh();
  }

  // A row's identity across a rebuild: the server url + id it stands for, "temp" for the
  // pinned session row. Placeholders ("Loading…", "No projects yet") have none — they are
  // never counted as arrivals.
  static QString rebuildKeyOf(const QListWidgetItem* it) {
    if (!it) return QString();
    if (it->data(kTempRole).toBool()) return QStringLiteral("temp");
    const QVariant id = it->data(Qt::UserRole);
    if (id.isNull()) return QString();
    return it->data(Qt::UserRole + 1).toString() + "|" + id.toString();
  }

  void ProjectsDialog::refresh() {
    // Preserve the selected row across a live remote re-list so the polling timer
    // doesn't yank the user's selection out from under them.
    const int prevRow = list_->currentRow();
    // What this list holds RIGHT NOW: a row the rebuild ADDS arrives out of the filter's
    // sand at the end rather than simply being there next frame (browser motion.js
    // filterDust). The very first build dusts nothing — the dialog has its own flight.
    QSet<QString> keysBefore;
    for (int i = 0; i < list_->count(); ++i) {
      const QString k = rebuildKeyOf(list_->item(i));
      if (!k.isEmpty()) keysBefore.insert(k);
    }
    // Whether this dialog has EVER built its list — not whether the list has rows in it
    // right now. A removal takes its row out of the view when the scatter ends, so the
    // rebuild that answers it can find the list empty; reading that as "this is the
    // opening build" is what made the pinned row simply appear, with no arrival at all.
    const bool wasBuilt = built_;
    built_ = true;
    // Keep the "Show:" per-server entries in step if servers were connected/disconnected.
    if (filter_ && connections_ && connections_->urls() != knownServerUrls_)
      rebuildFilterOptions();
    building_ = true;   // ignore the itemChanged storm from setCheckState below
    hideHoverPreview();   // clear() is about to delete whatever hoverItem_ points to
    closeInlineRename();  // …and the row the inline editor floats over
    list_->clear();
    const core::ProjectsStore store;  // pure helpers only; reads meta, no state

    // Multi-line row tooltip: image size with its orientation under it, the description
    // when set, then the origin note. The separator is the browser's " · ", which is what
    // tipContent splits a heading on. No drawn-line length — nobody hovers a row for it.
    auto rowTooltip = [&](int w, int h, const QString& description,
                          const QString& origin) {
      QStringList lines;
      if (w > 0 && h > 0)
        lines << QString("%1x%2 px · %3").arg(w).arg(h).arg(
            h >= w ? QStringLiteral("portrait") : QStringLiteral("landscape"));
      if (!description.isEmpty()) lines << QString("Description: %1").arg(description);
      if (!origin.isEmpty()) lines << origin;
      return lines.join('\n');
    };

    // Build one LOCAL project row (edited-result thumb, expiry-aware name colour, checkbox).
    auto buildLocalRow = [&](const Project& pr) {
      // Name · created · expiry only — no line/point counts (mirrors the browser projects list).
      const QString expiry = expiryText(store, pr.meta, now_);
      QString label = QString::fromStdString(pr.meta.name);
      const QString created = createdText(pr.meta.createdAt);
      if (!created.isEmpty()) label += QString("   ·   %1").arg(created);
      if (!expiry.isEmpty()) label += QString("   ·   %1").arg(expiry);
      auto* it = new QListWidgetItem(label, list_);
      it->setData(Qt::UserRole, QString::fromStdString(pr.meta.id));
      it->setData(Qt::UserRole + 3, QString::fromStdString(pr.meta.name));  // search key (name)
      // The stacked row's muted middle line + the accent its kebab chip paints with.
      {
        QStringList metaBits;
        if (!created.isEmpty()) metaBits << created;
        if (!expiry.isEmpty()) metaBits << expiry;
        it->setData(kMetaRole, metaBits.join(QStringLiteral(" · ")));
      }
      // Multi-select checkbox (key "|<id>" — empty server marks a local row).
      it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
      it->setCheckState(checked_.contains("|" + QString::fromStdString(pr.meta.id))
                            ? Qt::Checked : Qt::Unchecked);
      // Edited-result preview, pre-rendered by the caller through the canvas/export
      // path. Absent for pathless (in-memory) sources — those fall back to a
      // uniform placeholder tile so every row keeps the same height.
      const auto thumb = thumbs_.constFind(QString::fromStdString(pr.meta.id));
      if (thumb != thumbs_.constEnd() && !thumb->isNull()) {
        it->setIcon(QIcon(squareThumb(*thumb, 112)));
        it->setData(Qt::UserRole + 2, *thumb);
      } else {
        it->setIcon(QIcon(placeholderIcon(false)));
      }
      // NAME colour (UserRole+4) — the delegate paints ONLY the name in it. Red once
      // expired, amber within a day of expiry (warnings win over the swatch), else
      // the per-project colour, else the shared neutral grey (browser/CLI default).
      const QString pcol = QString::fromStdString(pr.meta.color);
      const QColor custom(pcol);
      QColor nameCol;
      if (store.isExpired(pr.meta, now_)) nameCol = QColor("#dc3545");
      else if (store.isExpiringSoon(pr.meta, now_)) nameCol = QColor("#e0a800");
      else if (!pcol.isEmpty() && custom.isValid()) nameCol = custom;
      else nameCol = QColor("#80868f");
      it->setData(Qt::UserRole + 4, nameCol);
      // UserRole+5: space-joined keywords, the search key for the keyword/common modes.
      QStringList kw;
      for (const auto& k : pr.meta.keywords) kw << QString::fromStdString(k);
      it->setData(Qt::UserRole + 5, kw.join(' '));
      // UserRole+6: file-origin flag (opened from a .stencil) → the delegate's bronze outline
      // + file glyph. The tooltip names where the project lives (local disk vs a .stencil file).
      it->setData(Qt::UserRole + 6, pr.meta.fromFile);
      // UserRole+8: this row is the project open in THIS editor right now → the
      // delegate's "(Current)" mark, painted in the palette's live accent.
      if (!activeProjectId_.isEmpty() && QString::fromStdString(pr.meta.id) == activeProjectId_)
        it->setData(kActiveRole, true);
      // A LOCAL project says nothing about its origin here: the row already carries the
      // "computer" badge, and the browser's own tip carries no origin line at all. A .stencil
      // project keeps its note, which tells you more than that badge's one word does.
      it->setToolTip(rowTooltip(pr.meta.imageW, pr.meta.imageH,
                                QString::fromStdString(pr.meta.description),
                                pr.meta.fromFile ? QStringLiteral("Opened from a .stencil project file")
                                                 : QString()));
    };

    // Build one SERVER (shared) project row: golden outline (delegate) + server badge.
    // UserRole+1 carries the origin server URL; a non-empty value marks the row as remote so
    // Open routes to OpenRemote (and tells the delegate to draw the outline).
    auto buildRemoteRow = [&](const stencil::net::ServerProject& sp) {
      QString label = QString("%1   —   %2")
                          .arg(sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name)
                          .arg(sp.serverUrl);
      const QString spCreated = createdText(sp.createdAt);
      if (!spCreated.isEmpty()) label += QString("   ·   %1").arg(spCreated);
      const QString spExpires = expiresText(sp.expiresAt);
      if (!spExpires.isEmpty()) label += QString("   ·   %1").arg(spExpires);
      auto* it = new QListWidgetItem(label, list_);
      it->setData(Qt::UserRole, sp.id);
      it->setData(Qt::UserRole + 1, sp.serverUrl);
      {
        QStringList metaBits;
        if (!spCreated.isEmpty()) metaBits << spCreated;
        if (!spExpires.isEmpty()) metaBits << spExpires;
        it->setData(kMetaRole, metaBits.join(QStringLiteral(" · ")));
      }
      it->setData(Qt::UserRole + 3, sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name);  // search key
      it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
      it->setCheckState(checked_.contains(sp.serverUrl + "|" + sp.id) ? Qt::Checked : Qt::Unchecked);
      // The NAME colour (UserRole+4) — the delegate paints ONLY the name in it, so the "— <url>"
      // suffix stays the default colour. Per-project colour when set, else the shared neutral grey
      // (same as local rows + the browser default — not gold). The gold outline marks server rows.
      const QColor custom(sp.color);
      it->setData(Qt::UserRole + 4,
                  (!sp.color.isEmpty() && custom.isValid()) ? custom : QColor("#80868f"));
      it->setData(Qt::UserRole + 5, sp.keywords.join(' '));  // keyword search key
      it->setToolTip(rowTooltip(sp.imageW, sp.imageH, sp.description,
                                QString("Server project on %1").arg(sp.serverUrl)));
      // Edited preview: the rendered `result`, falling back to `original` (browser
      // makeRemoteRow parity). Cached by id+version so the periodic re-list
      // doesn't re-download an unchanged project.
      const QPixmap pm = remoteThumb(sp);
      if (pm.isNull()) {
        it->setIcon(QIcon(placeholderIcon(true)));
      } else {
        it->setIcon(QIcon(squareThumb(pm, 112)));
        it->setData(Qt::UserRole + 2, pm);
      }
    };

    // Assemble a combined, sortable entry list (local + server) and order it per the active
    // sort mode, so name/date modes interleave local and server rows (mirrors the browser modal;
    // see js/ui/projectSort.js). Then build the rows in that order.
    struct Entry { bool remote; int idx; QString key; QString name; long long date; };
    std::vector<Entry> entries;
    for (int i = 0; i < static_cast<int>(projects_.size()); ++i) {
      const auto& m = projects_[i].meta;
      entries.push_back({ false, i, "|" + QString::fromStdString(m.id),
                          QString::fromStdString(m.name).toLower(), static_cast<long long>(m.updatedAt) });
    }
    for (int i = 0; i < remote_.size(); ++i) {
      const auto& sp = remote_[i];
      entries.push_back({ true, i, sp.serverUrl + "|" + sp.id, sp.name.toLower(), static_cast<long long>(sp.createdAt) });
    }
    const QString mode = g_projectsSortMode;
    QHash<QString, int> manualPos;
    if (mode == "manual")
      for (int i = 0; i < g_projectsManualOrder.size(); ++i) manualPos.insert(g_projectsManualOrder[i], i);
    auto cmpName = [](const Entry& a, const Entry& b) -> int {
      int c = QString::localeAwareCompare(a.name, b.name);
      if (c) return c;
      if (a.date != b.date) return a.date > b.date ? -1 : 1;  // newest first on a name tie
      return QString::compare(a.key, b.key);
    };
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
      if (mode == "local") { if (a.remote != b.remote) return !a.remote; return cmpName(a, b) < 0; }
      if (mode == "server") { if (a.remote != b.remote) return a.remote; return cmpName(a, b) < 0; }
      if (mode == "date-desc") { if (a.date != b.date) return a.date > b.date; return cmpName(a, b) < 0; }
      if (mode == "date-asc") { if (a.date != b.date) return a.date < b.date; return cmpName(a, b) < 0; }
      if (mode == "manual") {
        const int pa = manualPos.value(a.key, std::numeric_limits<int>::max());
        const int pb = manualPos.value(b.key, std::numeric_limits<int>::max());
        if (pa != pb) return pa < pb;
        return cmpName(a, b) < 0;
      }
      return cmpName(a, b) < 0;  // name (default): server + local interleaved
    });
    // This window's own unsaved session, pinned above the sorted rows (browser parity).
    // Inert: no id, so no open, rename, checkbox, drag or "⋯".
    if (temporary_) {
      auto* it = new QListWidgetItem(incognito_ ? QStringLiteral("Incognito (unsaved)")
                                                : QStringLiteral("Temporary (unsaved)"), list_);
      it->setFlags(Qt::ItemIsEnabled);
      it->setData(kTempRole, true);
      it->setData(Qt::UserRole + 3, it->text());   // the search key, like every row's
      it->setData(kMetaRole, incognito_ ? QStringLiteral("Current window · incognito · never saved")
                                        : QStringLiteral("Current window · not saved to storage"));
      it->setData(Qt::UserRole + 4, QColor("#80868f"));   // the shared name grey, like every row
      it->setIcon(QIcon(temporaryIcon(incognito_)));
    }
    for (const auto& e : entries) {
      if (e.remote) buildRemoteRow(remote_[e.idx]);
      else buildLocalRow(projects_[e.idx]);
    }

    // While the first server listing is still in flight, show a loading hint rather
    // than a misleading "No projects yet" — the dialog itself already opened (the
    // remote fetch is deferred); this row is replaced when the listing resolves.
    if (connections_ && !connections_->urls().isEmpty() && !remoteLoaded_) {
      auto* it = new QListWidgetItem(QStringLiteral("Loading shared projects…"), list_);
      it->setFlags(Qt::NoItemFlags);
      it->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
    }

    // Drop selections whose project is GONE — removed here, or from another window: the
    // bar reads checked_.size(), so a dead key kept "1 selected" on screen over an empty
    // list. A LOCAL key is "|<id>" (UserRole+1, its server url, is empty);
    // a remote key is left alone — a listing that has not answered is not proof it is gone.
    if (!checked_.isEmpty()) {
      QSet<QString> liveIds;
      liveIds.reserve(projects_.size());
      for (const Project& p : projects_) liveIds.insert(QString::fromStdString(p.meta.id));
      for (auto it = checked_.begin(); it != checked_.end();) {
        if (it->startsWith(QLatin1Char('|')) && !liveIds.contains(it->mid(1)))
          it = checked_.erase(it);
        else
          ++it;
      }
    }

    if (list_->count() == 0) {
      auto* it = new QListWidgetItem("No projects yet", list_);
      it->setFlags(Qt::NoItemFlags);
      building_ = false;
      updateBatchBar();
      return;
    }
    list_->setCurrentRow(prevRow >= 0 && prevRow < list_->count() ? prevRow : 0);
    applyFilter();   // re-hide rows the current filter excludes (survives the live re-list)
    building_ = false;
    updateBatchBar();
    // …and every row this rebuild ADDED forms out of sand on the filter's shared budget
    // but the longer ARRIVAL clock (browser twin: materialize, not the filter animator).
    // Last, with the bookkeeping settled: dustRowIn writes a role per row under the same
    // beforeFrame/afterFrame guard the filter's own frames use.
    if (wasBuilt)
      for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        const QString k = rebuildKeyOf(it);
        if (k.isEmpty() || it->isHidden() || keysBefore.contains(k)) continue;
        if (auto* fade = filterFade()) fade->dustRowIn(it, window(), kRowArriveMs);
      }
  }

}
