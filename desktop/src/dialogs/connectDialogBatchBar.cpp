// Three of the Servers dialog's construction phases (call order lives in connectDialog.cpp's
// ctor): the two connection preferences, the multi-select batch bar, and the list itself. The
// bar keeps the shape the Projects dialog's has — it stays while the list has rows, and only
// the count and the selection actions come and go.
#include "connectDialog.hpp"
#include "reorderableListWidget.hpp"
#include "serverClient.hpp"
#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "../support/controlReveal.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "connectDialogParts.hpp"
#include "../support/filterFade.hpp"
#include "../support/flowLayout.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/modalChrome.hpp"
#include "../support/searchCombo.hpp"
#include "../support/shimmerOverlay.hpp"
#include "../support/theme.hpp"
#include "../support/tipContent.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QClipboard>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace stencil::gui {

  void ConnectDialog::buildConnectionPrefs(QVBoxLayout* root) {
    // The two connection preferences on one row (browser .vs-checks: gap 26), no
    // tooltips — the label is the whole story there too. Auto-connect persists to
    // connectionStore at once; Sync is MainWindow's setting, seeded by setSyncToServer().
    {
      auto* checks = new QHBoxLayout;
      checks->setSpacing(26);
      autoConnect_ = new QCheckBox(tr("Auto-connect on open"));
      autoConnect_->setChecked(net::connectionStore::getAutoConnect());
      QObject::connect(autoConnect_, &QCheckBox::toggled, this,
                       [](bool on) { net::connectionStore::setAutoConnect(on); });
      checks->addWidget(autoConnect_);
      syncToServer_ = new QCheckBox(tr("Sync changes to server"));
      syncToServer_->setObjectName(QStringLiteral("connSyncToServer"));
      syncToServer_->setChecked(true);
      QObject::connect(syncToServer_, &QCheckBox::toggled, this,
                       [this](bool on) { emit syncToServerToggled(on); });
      checks->addWidget(syncToServer_);
      checks->addStretch(1);
      root->addLayout(checks);
    }

    root->addWidget(modalDivider(this));

    // Section header + the credential-kind view filter (projects dialog's "Show:" idiom).
    {
      auto* head = new QHBoxLayout;
      head->addWidget(modalSectionLabel(tr("Connections"), this));
      head->addStretch(1);
      auto* kind = new SearchComboBox(this, /*searchable=*/false);
      kindFilter_ = kind;
      kind->setObjectName(QStringLiteral("connKindFilter"));
      kind->setSizeAdjustPolicy(QComboBox::AdjustToContents);
      kind->setToolTip(tr("Filter the rows: all connections, only those holding an admin "
                          "credential, or only the rest"));
      kind->addItem(tr("All"), QStringLiteral("all"));
      kind->addItem(tr("Admin"), QStringLiteral("admin"));
      kind->addItem(tr("Non-admin"), QStringLiteral("nonadmin"));
      head->addWidget(kind);
      root->addLayout(head);
      QObject::connect(kind, &QComboBox::currentIndexChanged, this,
                       [this](int) { applyKindFilter(); });
    }
  }

  void ConnectDialog::buildConnectBatchBar(QVBoxLayout* root) {
    // Batch-select toolbar — the projects bar's shape (projectsDialog / browser
    // connectModal.js): it STAYS while the list has rows on view (it hosts Select all),
    // and only the count + the selection actions come and go with the checked set.
    batchBar_ = new QWidget;
    {
      // Browser .connect-batch-bar, value for value: a --bg-info card with a
      // --border-main hairline at radius 8, padding 6px 10px, the count in --text-key at
      // 13/600, and compact buttons (6px 10px at radius 4) so they read as the row's own
      // family. min-height is the 14px font's line box — a QSS min-height IS the
      // minimumSizeHint, and at 0 a squeezed dialog crushes the buttons to their padding.
      batchBar_->setObjectName(QStringLiteral("connBatchBar"));
      batchBar_->setAttribute(Qt::WA_StyledBackground, true);
      {
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        // The hairline is the theme's --border-main, not QPalette::Mid: Mid is the
        // MUTED TEXT grey, which drew the bar (and the rows) in near-white on dark.
        batchBar_->setStyleSheet(
            QStringLiteral("QWidget#connBatchBar{background:%1;border:1px solid %2;"
                           "border-radius:8px;}"
                           "QLabel#connBatchCount{font-weight:600;color:%3;}"
                           "QWidget#connBatchBar QPushButton{border-radius:4px;"
                           "padding:6px 10px;min-height:17px;}")
                .arg(infoBackground(dark).name(), themePalette(dark).borderMain.name(),
                     palette().color(QPalette::Link).name()));
      }
      // Wrapping rows (FlowLayout), as the browser's flex-wrap: a squeezed dialog
      // stacks the actions under the count rather than cutting them off at the edge.
      auto* bh = new FlowLayout(batchBar_, 0, 10, 6);   // browser gap: count → actions
      bh->setContentsMargins(10, 6, 10, 6);
      batchCount_ = new QLabel(tr("0 selected"));
      batchCount_->setObjectName(QStringLiteral("connBatchCount"));
      batchCount_->setVisible(false);
      bh->addWidget(batchCount_);
      // Accent-filled batch actions, glyph + label at 13 (browser .btn-icon-text), with
      // the destructive one in the danger fill. Left-packed after the count (no stretch),
      // exactly as the browser lays them.
      auto* actions = new QWidget(batchBar_);
      auto* ah = new FlowLayout(actions, 0, 6, 6);   // browser .connect-batch-actions gap
      ah->setLineSizeHint(true);   // asks for its one line; wraps inside when refused
      const auto accentBtn = [](const QString& label, const QString& icon, const QString& tip) {
        auto* b = new QPushButton(label);
        b->setProperty("accentCta", true);
        b->setIcon(labelIcon(icon, QColor("#ffffff"), 13));   // 6px to the label, as the browser
        b->setToolTip(tip);
        return b;
      };
      // Select/deselect every row on view (the kind filter's rows — browser
      // connect-select-all; the projects bar's projectsSelectAll).
      selectAllBtn_ = accentBtn(tr("Select all"), "check",
                                tr("Select every listed connection (the current filter's rows)"));
      selectAllBtn_->setObjectName(QStringLiteral("connSelectAll"));
      selectAllBtn_->setVisible(false);
      ah->addWidget(selectAllBtn_);
      QObject::connect(selectAllBtn_, &QPushButton::clicked, this, &ConnectDialog::toggleSelectAll);
      // No separate Clear: Select all ↔ Deselect all is the bar's one toggle, and a
      // Clear beside it did exactly what Deselect all does (user decision).
      auto* reSel = accentBtn(tr("Reconnect"), "refresh", tr("Reconnect the selected servers"));
      auto* discSel = new QPushButton(tr("Disconnect"));
      // trash, not ✕: disconnecting FORGETS the server — the app's destructive glyph.
      discSel->setObjectName(QStringLiteral("dangerButton"));
      discSel->setIcon(labelIcon("trash", QColor("#ffffff"), 13));
      discSel->setToolTip(tr("Disconnect (and forget) the selected servers"));
      // The selection-only actions live in ONE group so the bar's swap is a single
      // flight, not one per button: a control's dust is photographed where it sits at that
      // instant, and siblings revealed in the same turn are still animating their width.
      // Browser twin: .connect-batch-selected. Destructive last, as everywhere else.
      // One line while its slot slides (the width is what animates); wrapping at rest.
      batchSelectedGroup_ = new QWidget(actions);
      auto* gh = new FlowLayout(batchSelectedGroup_, 0, 6, 6);
      gh->setLineSizeHint(true);
      gh->setHoldsLineWhileCapped(true);
      gh->addWidget(reSel);
      gh->addWidget(discSel);
      batchSelectedGroup_->setVisible(false);
      ah->addWidget(batchSelectedGroup_);
      bh->addWidget(actions);
      QObject::connect(reSel, &QPushButton::clicked, this, [this] {
        // Async reconnect each selected server; the manager emits changed() as each resolves,
        // which is wired to rebuildList() below, so the rows refresh without blocking the UI.
        for (const QString& u : selected_)
          manager_->reconnectAsync(u, [](bool, QString) {});
        selected_.clear();
        rebuildList();
      });
      QObject::connect(discSel, &QPushButton::clicked, this, [this] {
        if (selected_.isEmpty()) return;
        if (!confirmYesNo(this, tr("Disconnect servers"),
                          tr("Disconnect and forget %1 selected server(s)?").arg(selected_.size())))
          return;
        // Captured FIRST: scatterRows retires each row, and a retired row drops out of
        // selected_ so the bar can leave with it — reading the set afterwards would
        // disconnect nothing at all.
        const QStringList targets = selected_.values();
        scatterRows(targets);   // …and they come apart on the way out
        for (const QString& u : targets) manager_->disconnectFrom(u);
        selected_.clear();
        rebuildList();
      });
    }
    batchBar_->setVisible(false);
    root->addWidget(batchBar_);
  }

  void ConnectDialog::buildConnectionList(QVBoxLayout* root) {
    auto* reList = new ReorderableListWidget;
    list_ = reList;
    list_->setObjectName(QStringLiteral("connList"));
    list_->setSpacing(4);  // 8px gaps between row cards (browser .connect-row margin-bottom)
    list_->setStyleSheet(rowStyleSheet());   // cascades to every row widget below
    // Minimum on the LIST, not the dialog: execMaybePopover drops the dialog-level
    // minimumWidth(480) and shrinks to sizeHint, which would leave the URL label
    // ~70px. The list minimum survives that pass; long URLs still middle-elide.
    list_->setMinimumWidth(420);
    // Rows are sized to the viewport (the URL elides), so nothing scrolls sideways.
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->viewport()->installEventFilter(this);  // re-cap row widths on resize
    // Rows fade at the list's edges instead of being cut mid-outline (projects parity).
    QObject::connect(list_->verticalScrollBar(), &QScrollBar::valueChanged, this,
                     [this] { applyRowReveal(); });
    QObject::connect(list_->verticalScrollBar(), &QScrollBar::rangeChanged, this,
                     [this](int, int) { applyRowReveal(); });
    root->addWidget(list_, 1);
    // Drag a row onto another to reorder the connection order (persisted via changed()→
    // saveServers). Drag a row OUT of the dialog to disconnect it (same Yes/No confirm as
    // the ✕ button); a release still inside the dialog just snaps back.
    reList->onReorder = [this](int from, int to) {
      if (!doomed_.isEmpty()) return;  // list indices are stale while removal dust plays
      if (manager_) manager_->reorder(from, to);  // emits changed() → rebuildList()
    };
    reList->onDragOut = [this](int rowIdx) {
      if (!manager_ || !doomed_.isEmpty()) return;
      const QStringList urls = manager_->urls();
      if (rowIdx < 0 || rowIdx >= urls.size()) return;
      if (frameGeometry().contains(QCursor::pos())) return;  // released inside the dialog → keep
      confirmDisconnect(urls[rowIdx]);
    };
  }

}  // namespace stencil::gui
