// Three of the Servers dialog's construction phases; call order lives in ConnectDialog.cpp's ctor.
#include "ConnectDialog.hpp"
#include "ReorderableListWidget.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "../support/controlReveal.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "connectDialogParts.hpp"
#include "../support/filterFade.hpp"
#include "../support/FlowLayout.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/modalChrome.hpp"
#include "../support/SearchCombo.hpp"
#include "../support/ShimmerOverlay.hpp"
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
    // Browser .vs-checks (gap 26). Auto-connect persists to connectionStore at once; Sync is MainWindow's setting.
    {
      auto* checks = new QHBoxLayout;
      checks->setSpacing(26);
      autoConnect = new QCheckBox(tr("Auto-connect on open"));
      autoConnect->setChecked(net::connectionStore::getAutoConnect());
      QObject::connect(autoConnect, &QCheckBox::toggled, this,
                       [](bool on) { net::connectionStore::setAutoConnect(on); });
      checks->addWidget(autoConnect);
      syncToServer = new QCheckBox(tr("Sync changes to server"));
      syncToServer->setObjectName(QStringLiteral("connSyncToServer"));
      syncToServer->setChecked(true);
      QObject::connect(syncToServer, &QCheckBox::toggled, this,
                       [this](bool on) { emit syncToServerToggled(on); });
      checks->addWidget(syncToServer);
      checks->addStretch(1);
      root->addLayout(checks);
    }

    root->addWidget(modalDivider(this));

    {
      auto* head = new QHBoxLayout;
      head->addWidget(modalSectionLabel(tr("Connections"), this));
      head->addStretch(1);
      auto* kind = new SearchComboBox(this, /*searchable=*/false);
      kindFilter = kind;
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
    // The projects bar's shape (browser connectModal.js): it STAYS while the list has rows (it hosts Select all).
    batchBar = new QWidget;
    {
      // Browser .connect-batch-bar, value for value. min-height is the 14px line box: a QSS min-height IS
      // the minimumSizeHint, and at 0 a squeezed dialog crushes the buttons to their padding.
      batchBar->setObjectName(QStringLiteral("connBatchBar"));
      batchBar->setAttribute(Qt::WA_StyledBackground, true);
      {
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        // --border-main, not QPalette::Mid: Mid is the MUTED TEXT grey (near-white on dark).
        batchBar->setStyleSheet(
            QStringLiteral("QWidget#connBatchBar{background:%1;border:1px solid %2;"
                           "border-radius:8px;}"
                           "QLabel#connBatchCount{font-weight:600;color:%3;}"
                           "QWidget#connBatchBar QPushButton{border-radius:4px;"
                           "padding:6px 10px;min-height:17px;}")
                .arg(infoBackground(dark).name(), themePalette(dark).borderMain.name(),
                     palette().color(QPalette::Link).name()));
      }
      // FlowLayout, as the browser's flex-wrap: a squeezed dialog stacks the actions under the count.
      auto* bh = new FlowLayout(batchBar, 0, 10, 6);
      bh->setContentsMargins(10, 6, 10, 6);
      batchCount = new QLabel(tr("0 selected"));
      batchCount->setObjectName(QStringLiteral("connBatchCount"));
      batchCount->setVisible(false);
      bh->addWidget(batchCount);
      // Browser .btn-icon-text, left-packed after the count (no stretch).
      auto* actions = new QWidget(batchBar);
      auto* ah = new FlowLayout(actions, 0, 6, 6);
      ah->setLineSizeHint(true);
      const auto accentBtn = [](const QString& label, const QString& icon, const QString& tip) {
        auto* b = new QPushButton(label);
        b->setProperty("accentCta", true);
        b->setIcon(labelIcon(icon, QColor("#ffffff"), 13));
        b->setToolTip(tip);
        return b;
      };
      // Browser connect-select-all.
      selectAllBtn = accentBtn(tr("Select all"), "check",
                                tr("Select every listed connection (the current filter's rows)"));
      selectAllBtn->setObjectName(QStringLiteral("connSelectAll"));
      selectAllBtn->setVisible(false);
      ah->addWidget(selectAllBtn);
      QObject::connect(selectAllBtn, &QPushButton::clicked, this, &ConnectDialog::toggleSelectAll);
      // No separate Clear: Select all ↔ Deselect all is the bar's one toggle (user decision).
      auto* reSel = accentBtn(tr("Reconnect"), "refresh", tr("Reconnect the selected servers"));
      auto* discSel = new QPushButton(tr("Disconnect"));
      discSel->setObjectName(QStringLiteral("dangerButton"));
      discSel->setIcon(labelIcon("trash", QColor("#ffffff"), 13));
      discSel->setToolTip(tr("Disconnect (and forget) the selected servers"));
      // ONE group so the bar's swap is a single flight: a control's dust is photographed where it sits, and
      // siblings revealed in the same turn are still animating their width. Browser: .connect-batch-selected.
      batchSelectedGroup = new QWidget(actions);
      auto* gh = new FlowLayout(batchSelectedGroup, 0, 6, 6);
      gh->setLineSizeHint(true);
      gh->setHoldsLineWhileCapped(true);
      gh->addWidget(reSel);
      gh->addWidget(discSel);
      batchSelectedGroup->setVisible(false);
      ah->addWidget(batchSelectedGroup);
      bh->addWidget(actions);
      QObject::connect(reSel, &QPushButton::clicked, this, [this] {
        for (const QString& u : selected)
          manager->reconnectAsync(u, [](bool, QString) {});
        selected.clear();
        rebuildList();
      });
      QObject::connect(discSel, &QPushButton::clicked, this, [this] {
        if (selected.isEmpty()) return;
        if (!confirmYesNo(this, tr("Disconnect servers"),
                          tr("Disconnect and forget %1 selected server(s)?").arg(selected.size())))
          return;
        // Captured FIRST: a retired row drops out of selected, so reading the set afterwards disconnects nothing.
        const QStringList targets = selected.values();
        scatterRows(targets);
        for (const QString& u : targets) manager->disconnectFrom(u);
        selected.clear();
        rebuildList();
      });
    }
    batchBar->setVisible(false);
    root->addWidget(batchBar);
  }

  void ConnectDialog::buildConnectionList(QVBoxLayout* root) {
    auto* reList = new ReorderableListWidget;
    list = reList;
    list->setObjectName(QStringLiteral("connList"));
    list->setSpacing(4);
    list->setStyleSheet(rowStyleSheet());
    // Minimum on the LIST, not the dialog: execMaybePopover drops the dialog-level minimumWidth and shrinks to sizeHint.
    list->setMinimumWidth(420);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->viewport()->installEventFilter(this);
    QObject::connect(list->verticalScrollBar(), &QScrollBar::valueChanged, this,
                     [this] { applyRowReveal(); });
    QObject::connect(list->verticalScrollBar(), &QScrollBar::rangeChanged, this,
                     [this](int, int) { applyRowReveal(); });
    root->addWidget(list, 1);
    // Drag onto another row reorders (persisted via changed()→saveServers); drag OUT disconnects with the ✕ confirm.
    reList->onReorder = [this](int from, int to) {
      if (!doomed.isEmpty()) return;
      if (manager) manager->reorder(from, to);
    };
    reList->onDragOut = [this](int rowIdx) {
      if (!manager || !doomed.isEmpty()) return;
      const QStringList urls = manager->urls();
      if (rowIdx < 0 || rowIdx >= urls.size()) return;
      if (frameGeometry().contains(QCursor::pos())) return;
      confirmDisconnect(urls[rowIdx]);
    };
  }

}  // namespace stencil::gui
