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
      search = new QLineEdit(this);
      search->setPlaceholderText(tr("Search projects…"));
      search->setClearButtonEnabled(true);
      srow->addWidget(search, 1);
      layout->addLayout(srow);
      connect(search, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    }

    // A FlowLayout (browser .modal-search-bar flex-wrap): squeezed, the selects wrap at their natural widths.
    {
      auto* frow = new FlowLayout(nullptr, 0, 8, 6);
      filter = new SearchComboBox(this, /*searchable=*/false);
      filter->setSizeAdjustPolicy(QComboBox::AdjustToContents);
      filter->setMaximumWidth(260);
      filter->setToolTip("Filter the list: all, local only, or a specific server");
      rebuildFilterOptions();
      frow->addWidget(filter);
      sortCombo = new SearchComboBox(this, /*searchable=*/false);
      sortCombo->setToolTip("Sort projects (drag a row to set a manual order)");
      sortCombo->addItem(tr("Name"), QStringLiteral("name"));
      sortCombo->addItem(tr("Local first"), QStringLiteral("local"));
      sortCombo->addItem(tr("Server first"), QStringLiteral("server"));
      sortCombo->addItem(tr("Newest"), QStringLiteral("date-desc"));
      sortCombo->addItem(tr("Oldest"), QStringLiteral("date-asc"));
      sortCombo->addItem(tr("Manual order"), QStringLiteral("manual"));
      {
        const int mi = sortCombo->findData(g_projectsSortMode);
        sortCombo->setCurrentIndex(mi >= 0 ? mi : 0);
      }
      frow->addWidget(sortCombo);
      searchModeCombo = new SearchComboBox(this, /*searchable=*/false);
      searchModeCombo->setToolTip("What the search box matches");
      searchModeCombo->addItem(tr("Name + keywords"), QStringLiteral("common"));
      searchModeCombo->addItem(tr("Names only"), QStringLiteral("names"));
      searchModeCombo->addItem(tr("Keywords only"), QStringLiteral("keywords"));
      frow->addWidget(searchModeCombo);
      layout->addLayout(frow);
      connect(filter, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
      connect(sortCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        g_projectsSortMode = sortCombo->currentData().toString();
        refresh();
      });
      connect(searchModeCombo, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
    }
  }

  void ProjectsDialog::buildBatchBar(QVBoxLayout* layout) {
    // Shown whenever the filtered view has selectable rows (it hosts Select all); direction buttons show by
    // selection homogeneity. WRAPS (browser .projects-batch-bar flex-wrap, gap 10).
    {
      batch.batchBar = new QWidget(this);
      auto* bh = new FlowLayout(batch.batchBar, 0, 10, 6);
      batch.batchCount = new QLabel("0 selected", this);
      bh->addWidget(batch.batchCount);
      // The accent rides the shared accentCta property (theme.cpp), leaving objectNames free for the tests.
      const auto accentBtn = [this](const QString& label, const QString& icon,
                                    const QString& tip) {
        auto* b = new QPushButton(label, this);
        b->setToolTip(tip);
        b->setProperty("accentCta", true);
        b->setIcon(labelIcon(icon, QColor("#ffffff"), 13));
        return b;
      };
      selectAllBtn = accentBtn(tr("Select all"), "check", "");   // the label says it already
      selectAllBtn->setObjectName("projectsSelectAll");
      bh->addWidget(selectAllBtn);
      connect(selectAllBtn, &QPushButton::clicked, this, &ProjectsDialog::toggleSelectAll);
      batch.batchToServer = accentBtn(tr("Move to server"), "server",
                                 "Move the checked local projects to a server");
      batch.batchCopyServer = accentBtn(tr("Copy to server"), "copy",
                                   "Copy the checked local projects to a server");
      batch.batchToLocal = accentBtn(tr("Move to local"), "download",
                                "Move the checked server projects to local storage");
      batch.batchCopyLocal = accentBtn(tr("Copy to local"), "copy",
                                  "Copy the checked server projects to local storage");
      batch.batchRemove = new QPushButton("Remove selected", this);
      batch.batchRemove->setToolTip("Remove the checked projects");
      batch.batchRemove->setObjectName("dangerButton");
      batch.batchRemove->setIcon(labelIcon("trash", QColor("#ffffff"), 13));
      batch.batchClear = accentBtn("Clear", "x", "Clear the current checkbox selection");
      // ONE group so the bar's swap is a single flight: a control's dust is photographed where it sits, and
      // siblings revealed in the same turn are still animating their width. Browser: .projects-batch-selected.
      batch.batchSelectedGroup = new QWidget(batch.batchBar);
      auto* gh = new FlowLayout(batch.batchSelectedGroup, 0, 6, 6);
      gh->setLineSizeHint(true);
      gh->setHoldsLineWhileCapped(true);
      gh->addWidget(batch.batchToServer);
      gh->addWidget(batch.batchCopyServer);
      gh->addWidget(batch.batchToLocal);
      gh->addWidget(batch.batchCopyLocal);
      gh->addWidget(batch.batchClear);
      gh->addWidget(batch.batchRemove);
      batch.batchSelectedGroup->setVisible(false);
      bh->addWidget(batch.batchSelectedGroup);
      batch.batchBar->setVisible(false);
      // The bar and the list share ONE zero-spacing slot; the gap under the bar is the bar's OWN bottom
      // margin, so a closing strip slides its whole footprint away (controlReveal closeBarSlot). 10px above, 10px below.
      bh->setContentsMargins(0, 0, 0, BODY_SPACING);
      barSlot = new QVBoxLayout;
      barSlot->setContentsMargins(0, 0, 0, 0);
      barSlot->setSpacing(0);
      barSlot->addWidget(batch.batchBar);
      layout->addLayout(barSlot, 1);
      connect(batch.batchToServer, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_MOVE_TO_SERVER); });
      connect(batch.batchCopyServer, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_COPY_TO_SERVER); });
      connect(batch.batchToLocal, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_MOVE_TO_LOCAL); });
      connect(batch.batchCopyLocal, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_COPY_TO_LOCAL); });
      connect(batch.batchRemove, &QPushButton::clicked, this, [this] { runBatch(Action::BATCH_REMOVE); });
      connect(batch.batchClear, &QPushButton::clicked, this, [this] { batch.checked.clear(); refresh(); });
    }
  }

  void ProjectsDialog::startRemotePolling() {
    installHoverShimmerIn(this);
    // The caret lands in the filter box, as the browser window does (InfoDialog parity).
    if (search) QTimer::singleShot(0, search, [s = search] { s->setFocus(); });

    // REST only: this polling stands in for the browser modal's WebSocket project-event feed.
    if (connections && !connections->urls().isEmpty()) {
      // Deferred a turn so the dialog paints at once with local rows + the "Loading shared projects…" placeholder.
      QTimer::singleShot(0, this, &ProjectsDialog::refreshRemote);
      remoteTimer = new QTimer(this);
      remoteTimer->setInterval(5000);
      connect(remoteTimer, &QTimer::timeout, this, &ProjectsDialog::refreshRemote);
      remoteTimer->start();
    }
  }

}  // namespace stencil::gui
