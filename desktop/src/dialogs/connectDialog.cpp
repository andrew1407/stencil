#include "connectDialog.hpp"
#include "../support/disintegrateOverlay.hpp"  // disconnected rows come apart
#include "../support/modalReveal.hpp"          // motionReduced()

#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "reorderableListWidget.hpp"
#include "serverClient.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // The collaboration gold used for server points throughout the app (mirrors the
    // browser's --remote-gold), so shared servers read the same on every front-end.
    const QColor kGold("#d4a017");

    // A filled status dot: green=connected, amber=connecting, red=error — mirrors the
    // browser's connection-status dot.
    QPixmap statusDot(stencil::net::ServerClient::Status s) {
      using S = stencil::net::ServerClient::Status;
      // Amber for BOTH "connecting" and "expired": the server is fine either way,
      // only the credential is missing (browser parity — an expired row is amber,
      // never the red of an unreachable host).
      QColor c = s == S::Connected  ? QColor("#28a745")
               : s == S::Connecting ? QColor("#e0a800")
               : s == S::Expired    ? QColor("#e0a800")
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

    // A label that elides to whatever width the row gives it (browser: CSS
    // text-overflow). Its size hint stays narrow so a long URL can never force a
    // horizontal scrollbar; the full text lives on the tooltip.
    class ElidedLabel : public QLabel {
     public:
      explicit ElidedLabel(const QString& text) : QLabel(text), full_(text) {
        setToolTip(full_);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
      }
      QSize sizeHint() const override { return QSize(40, QLabel::sizeHint().height()); }
      QSize minimumSizeHint() const override {
        return QSize(40, QLabel::minimumSizeHint().height());
      }

     protected:
      void resizeEvent(QResizeEvent* e) override {
        QLabel::resizeEvent(e);
        setText(fontMetrics().elidedText(full_, Qt::ElideMiddle, width()));
      }

     private:
      QString full_;
    };

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
    urlEdit_->setPlaceholderText("http://localhost:8090");
    urlEdit_->setToolTip(tr("Collaboration server URL, e.g. http://localhost:8090"));
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
    reconnectAllBtn_ = reconnectAllBtn;
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
        scatterRows(selected_.values());   // …and they come apart on the way out
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
    // Minimum on the LIST, not the dialog: execMaybePopover drops the dialog-level
    // minimumWidth(480) and shrinks to sizeHint, which would leave the URL label
    // ~70px. The list minimum survives that pass; long URLs still middle-elide.
    list_->setMinimumWidth(420);
    // Rows are sized to the viewport (the URL elides), so nothing scrolls sideways.
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->viewport()->installEventFilter(this);  // re-cap row widths on resize
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

  void ConnectDialog::scatterRows(const QStringList& urls) {
    if (!manager_ || !list_ || urls.isEmpty()) return;
    if (support::motionReduced()) return;        // no dust → removal finalizes at once
    const QStringList all = manager_->urls();   // rows are built in this order
    // Shared mote budget across the rows leaving together (projectsDialog says why).
    const int budget = std::max<int>(1, DisintegrateOverlay::kDustMaxCells / int(urls.size()));
    QStringList doomedNow;
    for (const QString& u : urls) {
      const int row = all.indexOf(u);
      QListWidgetItem* it = row >= 0 ? list_->item(row) : nullptr;
      if (!it) continue;
      if (!DisintegrateOverlay::overRect(list_->viewport(), list_->visualItemRect(it), this,
                                         DisintegrateOverlay::Sweep::Rows, /*dust=*/true, budget))
        continue;   // nothing to animate (hidden/tiny) → this row just removes instantly
      // Retire the row at once (projectsDialog::retireRow parity): the snapshot is what
      // flies, so the real row blanks and its empty slot is held until the dust settles.
      list_->removeItemWidget(it);
      it->setFlags(Qt::NoItemFlags);
      doomed_.insert(u);
      doomedNow.append(u);
    }
    if (doomedNow.isEmpty()) return;
    // Finalize when the dust settles: slots collapse and (only now) the empty state may
    // appear. Dies with the dialog — done() covers an early close.
    QTimer::singleShot(DisintegrateOverlay::kMs, this, [this, doomedNow] {
      for (const QString& u : doomedNow) doomed_.remove(u);
      if (doomed_.isEmpty()) rebuildList();
    });
  }

  void ConnectDialog::done(int r) {
    if (!doomed_.isEmpty()) {   // close beat the dust — finalize pending removals now
      doomed_.clear();
      rebuildList();
    }
    QDialog::done(r);
  }

  bool ConnectDialog::eventFilter(QObject* watched, QEvent* event) {
    if (list_ && watched == list_->viewport() && event->type() == QEvent::Resize) {
      // Re-cap every row to the new viewport width (the URL label re-elides itself) —
      // same intent as the projects dialog's delegate sizeHint cap.
      const int w = list_->viewport()->width();
      for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->sizeHint().isValid())
          list_->item(i)->setSizeHint(QSize(w, list_->item(i)->sizeHint().height()));
    }
    return QDialog::eventFilter(watched, event);
  }

  void ConnectDialog::confirmDisconnect(const QString& url) {
    if (!manager_) return;
    if (QMessageBox::question(this, tr("Disconnect server"),
                              tr("Disconnect and forget %1?").arg(url),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
      return;
    scatterRows({url});
    manager_->disconnectFrom(url);
    rebuildList();
  }

  void ConnectDialog::updateBatchBar() {
    if (!batchBar_) return;
    batchBar_->setVisible(!selected_.isEmpty());
    if (batchCount_) batchCount_->setText(tr("%1 selected").arg(selected_.size()));
  }

  // The expired row's way back in (browser parity): ask the SERVER for a fresh
  // session first — an open server just signs you in — and only if it refuses ask
  // for a token. Either a session token or the server's ADMIN token works: the
  // client's connect path already mints a session from an admin token.
  void ConnectDialog::reauthenticate(const QString& url) {
    QPointer<ConnectDialog> self(this);
    manager_->reconnectAsync(url, [this, self, url](bool ok, QString err) {
      if (!self) return;
      if (ok) {
        rebuildList();
        return;
      }
      bool got = false;
      const QString token = QInputDialog::getText(
          this, tr("Reconnect"),
          tr("%1 refused a fresh session (%2).\n\nPaste a session token — or the "
             "server's admin token — to sign in again:")
              .arg(url, err),
          QLineEdit::Password, QString(), &got);
      if (!self || !got || token.trimmed().isEmpty()) return;
      QString cerr;
      const bool signedIn = manager_->connectTo(url, token.trimmed(), cerr);
      if (!self) return;
      if (!signedIn)
        QMessageBox::warning(this, tr("Servers"),
                             tr("Could not reconnect — %1").arg(cerr));
      rebuildList();
    });
  }

  void ConnectDialog::rebuildList() {
    if (!manager_ || !list_) return;
    // While removal dust is playing, the retired rows keep their blank slots and the
    // empty state must NOT appear yet — the settle callback in scatterRows finalizes.
    if (!doomed_.isEmpty()) {
      updateBatchBar();
      return;
    }
    list_->clear();
    // Nothing to re-establish -> the button could only fail.
    if (reconnectAllBtn_) reconnectAllBtn_->setEnabled(!manager_->clients().isEmpty());
    const QStringList urls = manager_->urls();
    // Rows the previous build didn't have — they get the gather-in below.
    QSet<QString> fresh;
    for (const QString& u : urls)
      if (!known_.contains(u)) fresh.insert(u);
    known_ = QSet<QString>(urls.begin(), urls.end());
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
      // Scoped by objectName: bare property rules bleed into the widget's own
      // QToolTip, which rendered the hint with the grip's -3px letter squeeze.
      grip->setObjectName("connGrip");
      grip->setStyleSheet("#connGrip { color: palette(mid); letter-spacing: -3px; }");
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
      const bool expired = st == stencil::net::ServerClient::Status::Expired;
      dot->setToolTip(st == stencil::net::ServerClient::Status::Connected ? tr("Connected")
                      : st == stencil::net::ServerClient::Status::Connecting ? tr("Connecting…")
                      : expired ? tr("Session expired — reconnect to sign in again")
                                : tr("Disconnected"));
      h->addWidget(dot);
      auto* mark = new QLabel;
      mark->setPixmap(themedIcon("server", kGold, 16).pixmap(16, 16));
      h->addWidget(mark);
      h->addWidget(new ElidedLabel(url), 1);  // elides so the buttons never clip
      const QColor rowTxt = palette().color(QPalette::WindowText);
      auto* recon = mkIconBtn(themedIcon("refresh", rowTxt, 16),
                              tr("Reconnect this server"));
      auto* disc = mkIconBtn(themedIcon("x", QColor("#dc3545"), 16), tr("Disconnect"));
      // An expired row says so IN the row and offers a LABELLED way back in — the
      // icon-only refresh is easy to miss when it is the one thing to press (browser
      // parity: "Session expired — reconnect to sign in again").
      if (expired) {
        // Short in the row (it competes with the URL for width), full sentence on
        // the tooltip — and the labelled button says what to do about it.
        auto* note = new QLabel(tr("Session expired"));
        note->setObjectName(QStringLiteral("expiredNote"));
        note->setToolTip(tr("Session expired — reconnect to sign in again"));
        note->setStyleSheet(QStringLiteral("color:#e0a800;font-size:11px;"));
        note->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        h->addWidget(note);
        auto* signIn = new QPushButton(tr("Reconnect"));
        signIn->setObjectName(QStringLiteral("expiredReconnect"));
        signIn->setCursor(Qt::PointingHandCursor);
        h->addWidget(signIn);
        QObject::connect(signIn, &QPushButton::clicked, this,
                         [this, url] { reauthenticate(url); });
      }
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
      // Width capped to the viewport (eventFilter keeps it there on resize), so a long
      // URL elides instead of forcing a horizontal scrollbar that clipped the buttons.
      item->setSizeHint(QSize(list_->viewport()->width(), row->sizeHint().height()));
      list_->setItemWidget(item, row);
    }
    updateBatchBar();
    // Newly-connected rows materialize as the removal played backwards: the slot opens
    // blank and the dust GATHERS into the row (Sweep::Gather — browser ghostIn parity).
    if (!fresh.isEmpty() && isVisible() && !support::motionReduced()) {
      // Deferred a turn so the view has laid the new rows out (the grab needs geometry).
      QTimer::singleShot(0, this, [this, fresh] {
        list_->doItemsLayout();   // geometry first — the grab is only as good as it
        const QStringList now = manager_ ? manager_->urls() : QStringList();
        for (const QString& u : fresh) {
          const int row = now.indexOf(u);
          QListWidgetItem* it = (row >= 0 && row < list_->count()) ? list_->item(row) : nullptr;
          QWidget* w = it ? list_->itemWidget(it) : nullptr;
          if (!w) continue;
          if (!w->isVisible()) w->show();   // the view may not have polished it yet
          if (DisintegrateOverlay::over(w, this, DisintegrateOverlay::Sweep::Gather)) {
            w->setVisible(false);   // the slot stays; the motes are what the eye follows
            QPointer<QWidget> wp(w);
            QTimer::singleShot(DisintegrateOverlay::kMs, this,
                               [wp] { if (wp) wp->setVisible(true); });
          }
        }
      });
    }
  }

}  // namespace stencil::gui
