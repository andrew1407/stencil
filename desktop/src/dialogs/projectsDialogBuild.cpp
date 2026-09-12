// The Projects dialog's construction phases: the search row, the multi-select batch bar, and
// the live re-list timer for shared projects. Call order lives in projectsDialog.cpp's ctor and
// is pinned by tests/projectsDialogRows.headless.cpp — re-cut these, reorder nothing.
#include "projectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include "serverClient.hpp"
#include "../support/controlReveal.hpp"
#include "../support/flowLayout.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/modalChrome.hpp"
#include "../support/searchCombo.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "iconSet.hpp"   // labelIcon
#include "../support/shimmerOverlay.hpp"   // installHoverShimmerIn
namespace stencil::gui {

  void ProjectsDialog::buildSearchRow(QVBoxLayout* layout) {
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
  }

  void ProjectsDialog::buildBatchBar(QVBoxLayout* layout) {
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
  }

  void ProjectsDialog::startRemotePolling() {
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

}  // namespace stencil::gui
