#include "connectDialog.hpp"

#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "reorderableListWidget.hpp"
#include "serverClient.hpp"

#include <QCheckBox>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // The collaboration gold used for server markers throughout the app (mirrors the
    // browser's --remote-gold), so shared servers read the same on every front-end.
    const QColor kGold("#d4a017");

    // A filled status dot: green=connected, amber=connecting, red=error — mirrors the
    // browser's connection-status dot.
    QPixmap statusDot(stencil::net::ServerClient::Status s) {
      QColor c = s == stencil::net::ServerClient::Status::Connected ? QColor("#28a745")
               : s == stencil::net::ServerClient::Status::Connecting ? QColor("#e0a800")
                                                                     : QColor("#dc3545");
      QPixmap pm(12, 12);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawEllipse(2, 2, 8, 8);
      return pm;
    }

    // A muted, slightly-tracked uppercase section header (browser's .vs-section).
    QLabel* sectionLabel(const QString& text) {
      auto* l = new QLabel(text.toUpper());
      l->setStyleSheet("color: palette(mid); font-weight: 600; letter-spacing: 1px;");
      return l;
    }

    QFrame* hLine() {
      auto* line = new QFrame;
      line->setFrameShape(QFrame::HLine);
      line->setFrameShadow(QFrame::Sunken);
      return line;
    }
  }  // namespace

  ConnectDialog::ConnectDialog(stencil::net::ConnectionManager* manager, QWidget* parent)
      : QDialog(parent), manager_(manager) {
    setWindowTitle(tr("Servers"));
    setMinimumWidth(480);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    // ── Header: server icon + title, Close at the right (mirrors the browser modal).
    auto* header = new QHBoxLayout;
    const QColor txt = palette().color(QPalette::WindowText);
    auto* iconLbl = new QLabel;
    iconLbl->setPixmap(themedIcon("server", txt, 22).pixmap(22, 22));
    header->addWidget(iconLbl);
    auto* titleLbl = new QLabel(tr("Servers"));
    QFont tf = titleLbl->font();
    tf.setPointSizeF(tf.pointSizeF() + 3);
    tf.setBold(true);
    titleLbl->setFont(tf);
    header->addWidget(titleLbl);
    header->addStretch(1);
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setToolTip(tr("Close this dialog"));
    closeBtn->setIcon(themedIcon("x", txt, 15));
    header->addWidget(closeBtn);
    root->addLayout(header);
    root->addWidget(hLine());

    root->addWidget(sectionLabel(tr("Connect a server")));
    auto* form = new QGridLayout;
    form->setColumnStretch(1, 1);
    form->setHorizontalSpacing(10);
    form->addWidget(new QLabel(tr("URL")), 0, 0);
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText("http://host:8090");
    urlEdit_->setToolTip(tr("Collaboration server URL, e.g. http://host:8090"));
    form->addWidget(urlEdit_, 0, 1);
    form->addWidget(new QLabel(tr("Token")), 1, 0);
    tokenEdit_ = new QLineEdit;
    tokenEdit_->setPlaceholderText(tr("(optional)"));
    tokenEdit_->setToolTip(tr("Optional access token for a secured server"));
    form->addWidget(tokenEdit_, 1, 1);
    root->addLayout(form);

    // Connect (left) + Reconnect all (right), grouped on one row.
    auto* actions = new QHBoxLayout;
    auto* connectBtn = new QPushButton(tr("Connect"));
    connectBtn->setToolTip(tr("Connect to the server at the URL above"));
    // White glyph: Connect is the default button (accent-filled in the theme QSS).
    connectBtn->setIcon(themedIcon("link", QColor("#ffffff"), 15));
    connectBtn->setDefault(true);
    actions->addWidget(connectBtn);
    actions->addStretch(1);
    auto* reconnectAllBtn = new QPushButton(tr("Reconnect all"));
    reconnectAllBtn->setIcon(themedIcon("refresh", txt, 15));
    reconnectAllBtn->setToolTip(tr("Re-establish every connection (re-validate / reissue tokens)"));
    actions->addWidget(reconnectAllBtn);
    root->addLayout(actions);

    // Auto-connect on open — moved here from Settings; persisted immediately.
    autoConnect_ = new QCheckBox(tr("Auto-connect on open"));
    autoConnect_->setToolTip(tr("Reconnect saved servers automatically when the app opens"));
    autoConnect_->setChecked(net::connectionStore::getAutoConnect());
    QObject::connect(autoConnect_, &QCheckBox::toggled, this,
                     [](bool on) { net::connectionStore::setAutoConnect(on); });
    root->addWidget(autoConnect_);

    root->addWidget(hLine());

    root->addWidget(sectionLabel(tr("Connections")));

    // Batch-select toolbar — appears once one or more connections are checked.
    batchBar_ = new QWidget;
    {
      auto* bh = new QHBoxLayout(batchBar_);
      bh->setContentsMargins(0, 0, 0, 0);
      batchCount_ = new QLabel(tr("0 selected"));
      bh->addWidget(batchCount_);
      bh->addStretch(1);
      auto* reSel = new QPushButton(tr("Reconnect"));
      reSel->setIcon(themedIcon("refresh", txt, 15));
      reSel->setToolTip(tr("Reconnect the selected servers"));
      auto* discSel = new QPushButton(tr("Disconnect"));
      discSel->setIcon(themedIcon("x", QColor("#dc3545"), 15));
      discSel->setToolTip(tr("Disconnect (and forget) the selected servers"));
      auto* clrSel = new QPushButton(tr("Clear"));
      clrSel->setToolTip(tr("Clear the current selection"));
      bh->addWidget(reSel);
      bh->addWidget(discSel);
      bh->addWidget(clrSel);
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
        if (QMessageBox::question(
                this, tr("Disconnect servers"),
                tr("Disconnect and forget %1 selected server(s)?").arg(selected_.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
          return;
        for (const QString& u : selected_) manager_->disconnectFrom(u);
        selected_.clear();
        rebuildList();
      });
      QObject::connect(clrSel, &QPushButton::clicked, this, [this] {
        selected_.clear();
        rebuildList();
      });
    }
    batchBar_->setVisible(false);
    root->addWidget(batchBar_);

    auto* reList = new ReorderableListWidget;
    list_ = reList;
    list_->setSpacing(4);
    root->addWidget(list_, 1);
    // Drag a row onto another to reorder the connection order (persisted via changed()→
    // saveServers). Drag a row OUT of the dialog to disconnect it (same Yes/No confirm as
    // the ✕ button); a release still inside the dialog just snaps back.
    reList->onReorder = [this](int from, int to) {
      if (manager_) manager_->reorder(from, to);  // emits changed() → rebuildList()
    };
    reList->onDragOut = [this](int rowIdx) {
      if (!manager_) return;
      const QStringList urls = manager_->urls();
      if (rowIdx < 0 || rowIdx >= urls.size()) return;
      if (frameGeometry().contains(QCursor::pos())) return;  // released inside the dialog → keep
      confirmDisconnect(urls[rowIdx]);
    };

    auto* hint = new QLabel(
        tr("Connections are saved and (optionally) restored on open · "
           "server projects show a golden outline."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(mid); font-size: 11px;");
    root->addWidget(hint);

    QObject::connect(reconnectAllBtn, &QPushButton::clicked, this, [this] {
      if (manager_) manager_->reconnectAllAsync();  // changed() → rebuildList() as each resolves
      rebuildList();
    });
    QObject::connect(connectBtn, &QPushButton::clicked, this, &ConnectDialog::doConnect);
    QObject::connect(urlEdit_, &QLineEdit::returnPressed, this, &ConnectDialog::doConnect);
    QObject::connect(tokenEdit_, &QLineEdit::returnPressed, this, &ConnectDialog::doConnect);
    QObject::connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    if (manager_)
      QObject::connect(manager_, &stencil::net::ConnectionManager::changed, this,
                       &ConnectDialog::rebuildList);

    rebuildList();
  }

  void ConnectDialog::doConnect() {
    if (!manager_) return;
    const QString url = urlEdit_->text().trimmed();
    if (url.isEmpty()) {
      QMessageBox::warning(this, tr("Servers"), tr("Enter a server URL."));
      return;
    }
    QString err;
    if (manager_->connectTo(url, tokenEdit_->text().trimmed(), err)) {
      urlEdit_->clear();
      tokenEdit_->clear();
    } else {
      QMessageBox::warning(this, tr("Servers"), tr("Could not connect — %1").arg(err));
    }
    rebuildList();
  }

  void ConnectDialog::confirmDisconnect(const QString& url) {
    if (!manager_) return;
    if (QMessageBox::question(this, tr("Disconnect server"),
                              tr("Disconnect and forget %1?").arg(url),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
      return;
    manager_->disconnectFrom(url);
    rebuildList();
  }

  void ConnectDialog::updateBatchBar() {
    if (!batchBar_) return;
    batchBar_->setVisible(!selected_.isEmpty());
    if (batchCount_) batchCount_->setText(tr("%1 selected").arg(selected_.size()));
  }

  void ConnectDialog::rebuildList() {
    if (!manager_ || !list_) return;
    list_->clear();
    const QStringList urls = manager_->urls();
    // Drop any selected urls that are no longer connected.
    for (const QString& u : selected_.values())
      if (!urls.contains(u)) selected_.remove(u);
    if (urls.isEmpty()) {
      auto* empty = new QListWidgetItem(tr("No servers connected."), list_);
      empty->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
      empty->setFlags(Qt::NoItemFlags);
      updateBatchBar();
      return;
    }
    // A compact, bordered icon button (browser's per-row .connect-reconnect-one /
    // .connect-disconnect) — subtle, fixed-size, grouped tight on the right.
    auto mkIconBtn = [](const QIcon& ic, const QString& tip) {
      auto* b = new QPushButton;
      b->setIcon(ic);
      b->setIconSize(QSize(16, 16));
      b->setFixedSize(30, 28);
      b->setToolTip(tip);
      b->setCursor(Qt::PointingHandCursor);
      return b;
    };
    auto* reList = static_cast<ReorderableListWidget*>(list_);
    int rowIndex = 0;
    for (const QString& url : urls) {
      const int myIndex = rowIndex++;
      auto* row = new QWidget;
      auto* h = new QHBoxLayout(row);
      h->setContentsMargins(8, 4, 8, 4);
      h->setSpacing(8);
      // Drag grip: drag onto another row to reorder, or out of the dialog to disconnect.
      auto* grip = new DragGrip;
      grip->setStyleSheet("color: palette(mid); letter-spacing: -3px;");
      grip->onDrag = [reList, myIndex] { reList->beginRowDrag(myIndex); };
      h->addWidget(grip);
      // Multi-select checkbox for batch reconnect/disconnect.
      auto* cb = new QCheckBox;
      cb->setChecked(selected_.contains(url));
      cb->setToolTip(tr("Select for batch action"));
      QObject::connect(cb, &QCheckBox::toggled, this, [this, url](bool on) {
        if (on) selected_.insert(url); else selected_.remove(url);
        updateBatchBar();
      });
      h->addWidget(cb);
      stencil::net::ServerClient* cl = manager_->find(url);
      const auto st = cl ? cl->status() : stencil::net::ServerClient::Status::Error;
      auto* dot = new QLabel;
      dot->setPixmap(statusDot(st));
      dot->setToolTip(st == stencil::net::ServerClient::Status::Connected ? tr("Connected")
                      : st == stencil::net::ServerClient::Status::Connecting ? tr("Connecting…")
                                                                            : tr("Disconnected"));
      h->addWidget(dot);
      auto* mark = new QLabel;
      mark->setPixmap(themedIcon("server", kGold, 16).pixmap(16, 16));
      h->addWidget(mark);
      h->addWidget(new QLabel(url), 1);
      const QColor rowTxt = palette().color(QPalette::WindowText);
      auto* recon = mkIconBtn(themedIcon("refresh", rowTxt, 16),
                              tr("Reconnect this server"));
      auto* disc = mkIconBtn(themedIcon("x", QColor("#dc3545"), 16), tr("Disconnect"));
      h->addWidget(recon);
      h->addWidget(disc);
      QObject::connect(recon, &QPushButton::clicked, this, [this, url] {
        QPointer<ConnectDialog> self(this);
        manager_->reconnectAsync(url, [this, self](bool ok, QString err) {
          if (!self) return;
          if (!ok)
            QMessageBox::warning(this, tr("Servers"),
                                 tr("Could not reconnect — %1").arg(err));
          rebuildList();
        });
      });
      QObject::connect(disc, &QPushButton::clicked, this, [this, url] { confirmDisconnect(url); });
      auto* item = new QListWidgetItem(list_);
      item->setSizeHint(row->sizeHint());
      list_->setItemWidget(item, row);
    }
    updateBatchBar();
  }

}  // namespace stencil::gui
