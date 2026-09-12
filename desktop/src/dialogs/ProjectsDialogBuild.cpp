// The Projects dialog's construction phases; call order in ProjectsDialog.cpp's ctor, pinned by
// tests/ProjectsDialogRows.headless.cpp — re-cut these, reorder nothing.
#include "ProjectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include "ServerClient.hpp"
#include "../support/controlReveal.hpp"
#include "../support/FlowLayout.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/modalChrome.hpp"
#include "../support/SearchCombo.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "iconSet.hpp"
#include "../support/ShimmerOverlay.hpp"
namespace stencil::gui {

  void ProjectsDialog::buildSearchRow(QVBoxLayout* layout) {
    // Search on its own full-width row ABOVE the filter selects, as the browser modal lays it.
    {
      auto* srow = new QHBoxLayout;
      search_ = new QLineEdit(this);
      search_->setPlaceholderText(tr("Search projects…"));
      search_->setClearButtonEnabled(true);
      srow->addWidget(search_, 1);
      layout->addLayout(srow);
      connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    }

    // A FlowLayout (browser .modal-search-bar flex-wrap): squeezed, the selects wrap at their natural widths.
    {
      auto* frow = new FlowLayout(nullptr, 0, 8, 6);
      filter_ = new SearchComboBox(this, /*searchable=*/false);
      filter_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
      filter_->setMaximumWidth(260);
      filter_->setToolTip("Filter the list: all, local only, or a specific server");
      rebuildFilterOptions();
      frow->addWidget(filter_);
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
    // Shown whenever the filtered view has selectable rows (it hosts Select all); direction buttons show by
    // selection homogeneity. WRAPS (browser .projects-batch-bar flex-wrap, gap 10).
    {
      batchBar_ = new QWidget(this);
      auto* bh = new FlowLayout(batchBar_, 0, 10, 6);
      batchCount_ = new QLabel("0 selected", this);
      bh->addWidget(batchCount_);
      // The accent rides the shared accentCta property (theme.cpp), leaving objectNames free for the tests.
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
      selectAllBtn_->setObjectName("projectsSelectAll");
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
      // ONE group so the bar's swap is a single flight: a control's dust is photographed where it sits, and
      // siblings revealed in the same turn are still animating their width. Browser: .projects-batch-selected.
      batchSelectedGroup_ = new QWidget(batchBar_);
      auto* gh = new FlowLayout(batchSelectedGroup_, 0, 6, 6);
      gh->setLineSizeHint(true);
      gh->setHoldsLineWhileCapped(true);
      gh->addWidget(batchToServer_);
      gh->addWidget(batchCopyServer_);
      gh->addWidget(batchToLocal_);
      gh->addWidget(batchCopyLocal_);
      gh->addWidget(batchClear_);
      gh->addWidget(batchRemove_);
      batchSelectedGroup_->setVisible(false);
      bh->addWidget(batchSelectedGroup_);
      batchBar_->setVisible(false);
      // The bar and the list share ONE zero-spacing slot; the gap under the bar is the bar's OWN bottom
      // margin, so a closing strip slides its whole footprint away (controlReveal closeBarSlot). 10px above, 10px below.
      bh->setContentsMargins(0, 0, 0, BODY_SPACING);
      barSlot_ = new QVBoxLayout;
      barSlot_->setContentsMargins(0, 0, 0, 0);
      barSlot_->setSpacing(0);
      barSlot_->addWidget(batchBar_);
      layout->addLayout(barSlot_, 1);
      connect(batchToServer_, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_MOVE_TO_SERVER); });
      connect(batchCopyServer_, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_COPY_TO_SERVER); });
      connect(batchToLocal_, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_MOVE_TO_LOCAL); });
      connect(batchCopyLocal_, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_COPY_TO_LOCAL); });
      connect(batchRemove_, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_REMOVE); });
      connect(batchClear_, &QPushButton::clicked, this, [this] { checked_.clear(); refresh(); });
    }
  }

  void ProjectsDialog::startRemotePolling() {
    installHoverShimmerIn(this);

    // REST only: this polling stands in for the browser modal's WebSocket project-event feed.
    if (connections_ && !connections_->urls().isEmpty()) {
      // Deferred a turn so the dialog paints at once with local rows + the "Loading shared projects…" placeholder.
      QTimer::singleShot(0, this, &ProjectsDialog::refreshRemote);
      remoteTimer_ = new QTimer(this);
      remoteTimer_->setInterval(5000);
      connect(remoteTimer_, &QTimer::timeout, this, &ProjectsDialog::refreshRemote);
      remoteTimer_->start();
    }
  }

}  // namespace stencil::gui
