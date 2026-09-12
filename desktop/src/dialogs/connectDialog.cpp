#include "connectDialog.hpp"
#include "connectDialogParts.hpp"
#include "../support/scrollReveal.hpp"             // revealDissolve (scroll edge fade)
#include "../support/disintegrateOverlay.hpp"  // disconnected rows come apart
#include "../support/dissolveEffect.hpp"       // scroll-edge grain dissolve
#include "../support/filterFade.hpp"           // filtered-out rows fade + collapse
#include "../support/flowLayout.hpp"           // the batch bar wraps, never clips
#include "../support/guiHelpers.hpp"           // confirmYesNo()
#include "../support/modalChrome.hpp"          // the browser modal shell
#include "../support/modalReveal.hpp"          // motionReduced()
#include "../support/controlReveal.hpp"     // the batch bar comes and goes as sand
#include "../support/shimmerOverlay.hpp"       // the row's glass hover sweep

#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "theme.hpp"   // infoBackground: the browser's --bg-info, for the row hover
#include "reorderableListWidget.hpp"
#include "searchCombo.hpp"
#include "serverClient.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QColor>
#include <QGuiApplication>
#include <QFont>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace stencil::gui {


  ConnectDialog::ConnectDialog(stencil::net::ConnectionManager* manager, QWidget* parent)
      : QDialog(parent), manager_(manager) {
    setWindowTitle(tr("Servers"));
    // The browser .app-modal width — also what fits the footer hint on one line.
    setMinimumWidth(kModalWidth);

    // Browser connectModal.js parity: shared modal shell (glyph + title + Close pill).
    ModalChrome chrome = installModalChrome(this, "server", tr("Servers"));
    QVBoxLayout* root = chrome.body;

    root->addWidget(modalSectionLabel(tr("Connect a server"), this));
    auto* form = new QGridLayout;
    form->setColumnStretch(1, 1);
    form->setHorizontalSpacing(10);
    auto* urlLbl = new QLabel(tr("URL"));
    urlLbl->setToolTip(tr("Server URL, e.g. http://localhost:8090"));
    form->addWidget(urlLbl, 0, 0);
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText("http://localhost:8090");
    form->addWidget(urlEdit_, 0, 1);
    auto* tokenLbl = new QLabel(tr("Token"));
    tokenLbl->setToolTip(tr("Optional access token (issued otherwise)"));
    form->addWidget(tokenLbl, 1, 0);
    tokenEdit_ = new QLineEdit;
    tokenEdit_->setPlaceholderText(tr("(optional)"));
    form->addWidget(tokenEdit_, 1, 1);
    root->addLayout(form);

    // Connect (left) + Reconnect all (right), grouped on one row.
    auto* actions = new QHBoxLayout;
    auto* connectBtn = new QPushButton(tr("Connect"));
    connectBtn->setToolTip(tr("Connect to the server at the URL above"));
    // Accent CTA with a white glyph (browser default-button treatment).
    makeModalCta(connectBtn, "plus-circle");
    actions->addWidget(connectBtn);
    // AFTER the layout has parented it, never before: setDefault() registers the button
    // with its QDialog by walking up its parents, so on a parentless one it only sets a
    // flag, and Qt's autoDefault juggling then cleared it with no main default to restore.
    connectBtn->setDefault(true);
    actions->addStretch(1);
    auto* reconnectAllBtn = new QPushButton(tr("Reconnect all"));
    reconnectAllBtn_ = reconnectAllBtn;
    makeModalCta(reconnectAllBtn, "refresh");
    reconnectAllBtn->setToolTip(tr("Re-establish every connection (re-validate / reissue tokens)"));
    actions->addWidget(reconnectAllBtn);
    root->addLayout(actions);

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

    // Footer (browser settings-footer): the saved-connections hint under a hairline.
    // The browser footer's exact sentence (connectModal.js .footer-hint) — the longer
    // invite-token clause made the desktop hint wrap to a second line.
    addModalFooter(chrome,
                   tr("Connections are saved and (optionally) restored on open · "
                      "server projects show a golden outline."));

    QObject::connect(reconnectAllBtn, &QPushButton::clicked, this, [this] {
      if (manager_) manager_->reconnectAllAsync();  // changed() → rebuildList() as each resolves
      rebuildList();
    });
    // Return belongs to the DEFAULT button (connectBtn), from either field or anywhere
    // else. Deliberately no returnPressed wiring beside it: a QLineEdit emits that AND
    // lets the key travel on to the default button, firing doConnect twice.
    QObject::connect(connectBtn, &QPushButton::clicked, this, &ConnectDialog::doConnect);
    if (manager_)
      QObject::connect(manager_, &stencil::net::ConnectionManager::changed, this,
                       &ConnectDialog::rebuildList);

    rebuildList();
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
    // A row action: the app's filled button chrome, compact — the browser's
    // .connect-reconnect-one / .connect-invite / .connect-disconnect are ordinary
    // accent-filled .btn-icons. `text` is empty for all but the expired row's fix.
    auto mkIconBtn = [](const QIcon& ic, const QString& tip, const QString& text = QString()) {
      auto* b = new QPushButton(text);
      b->setIcon(ic);
      b->setIconSize(QSize(15, 15));   // browser: icon({ size: 15 })
      b->setToolTip(tip);
      b->setCursor(Qt::PointingHandCursor);
      b->setProperty("rowAction", true);   // styled by rowStyleSheet()
      return b;
    };
    auto* reList = static_cast<ReorderableListWidget*>(list_);
    int rowIndex = 0;
    for (const QString& url : urls) {
      const int myIndex = rowIndex++;
      stencil::net::ServerClient* client = manager_->find(url);
      // An ADMIN row holds a credential PROVEN able to mint session tokens — the one
      // kind that can hand out invites. Marked with the collaboration gold the projects
      // list gives server rows (outline + a wash of the same colour + gold bold text).
      const bool admin = client && client->isAdmin();
      stencil::net::ServerClient* cl = client;
      const auto st = cl ? cl->status() : stencil::net::ServerClient::Status::Error;
      const bool expired = st == stencil::net::ServerClient::Status::Expired;
      const QString adminTip =
          tr("Admin credential — this connection can mint session tokens (invite links)");
      // One card per connection (rowStyleSheet): gold for an admin credential, amber
      // for an expired session, plain otherwise.
      auto* row = new RowCard;
      row->setObjectName(admin      ? QStringLiteral("connRowAdmin")
                         : expired  ? QStringLiteral("connRowExpired")
                                    : QStringLiteral("connRow"));
      if (admin) row->setToolTip(adminTip);
      auto* h = new QHBoxLayout(row);
      h->setContentsMargins(10, 8, 10, 8);   // browser .connect-row: padding 8px 10px
      // Spacing per gap: the browser's row is 12 between items, but dot · glyph · address
      // sit inside one .connect-url at gap 6, and the actions at 6. The dot's pixmap
      // carries its 2px halo (a CSS box-shadow overhangs), so its gaps give 2 back.
      h->setSpacing(0);
      // Every item rides the row's middle — the browser's .connect-row is
      // `align-items: center`. Left to their own size policies the taller ones stretched
      // to the row height and their content sat off-centre against the small ones.
      h->setAlignment(Qt::AlignVCenter);
      // Drag grip: drag onto another row to reorder, or out of the dialog to disconnect.
      auto* grip = new DragGrip;
      // Scoped by objectName: bare property rules bleed into the widget's own
      // QToolTip, which rendered the hint with the grip's -3px letter squeeze.
      grip->setObjectName("connGrip");
      grip->setStyleSheet("#connGrip { color: palette(mid); letter-spacing: -3px; }");
      grip->onDrag = [reList, myIndex] { reList->beginRowDrag(myIndex); };
      h->addWidget(grip);
      h->addSpacing(12);
      // Multi-select checkbox for batch reconnect/disconnect.
      auto* cb = new QCheckBox;
      cb->setObjectName(QStringLiteral("connRowSelect"));
      cb->setChecked(selected_.contains(url));
      cb->setToolTip(tr("Select for batch action"));
      // A checked row wears the accent outline over --bg-info (browser .connect-selected),
      // repolished in place so the sheet is re-evaluated without a rebuild.
      const auto markSelected = [row](bool on) {
        row->setProperty("selected", on);
        row->style()->unpolish(row);
        row->style()->polish(row);
      };
      markSelected(cb->isChecked());
      QObject::connect(cb, &QCheckBox::toggled, this, [this, url, markSelected](bool on) {
        if (on) selected_.insert(url); else selected_.remove(url);
        markSelected(on);
        updateBatchBar();
      });
      h->addWidget(cb);
      h->addSpacing(10);
      // The row says its state in one set of words: the dot's own tip, and the heading of
      // the URL's two-section tip below.
      const QString statusText =
          st == stencil::net::ServerClient::Status::Connected    ? tr("Connected")
          : st == stencil::net::ServerClient::Status::Connecting ? tr("Connecting…")
          : expired ? tr("Session expired — reconnect to sign in again")
                    : tr("Disconnected");
      auto* dot = new QLabel;
      dot->setPixmap(statusDot(st));
      dot->setToolTip(statusText);
      h->addWidget(dot);
      h->addSpacing(4);
      auto* mark = new QLabel;
      // Gold on EVERY row, admin or not (browser: .connect-row .connect-url .ic).
      mark->setPixmap(themedIcon("server", kGold, 14).pixmap(14, 14));
      h->addWidget(mark);
      h->addSpacing(6);
      auto* urlLbl = new ElidedLabel(url);   // elides so the buttons never clip
      // "<state> — <url>", the browser's own title on .connect-url. The app's tip parser
      // (support/tipContent) reads that dash as heading/subtitle, so the tip says what the
      // connection is and shows the address under it.
      urlLbl->setToolTip(statusText + QStringLiteral(" — ") + url);
      h->addWidget(urlLbl, 1);
      h->addSpacing(12);
      // The ADMIN badge rides beside the URL rather than inside it — its own row child,
      // so the elide that shortens a long URL can never clip the badge away with it
      // (browser .connect-admin-badge: a gold lock + "Admin", 600 at 12px).
      if (admin) {
        // A plain QWidget, NOT a QLabel: QLabel::sizeHint() is measured from its own
        // (empty) text and ignores a child layout, so a Fixed-width QLabel host collapsed
        // the lock+"Admin" pair to a sliver beside the URL.
        auto* badge = new QWidget;
        badge->setObjectName(QStringLiteral("connAdminBadge"));
        badge->setToolTip(adminTip);
        badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        auto* bl = new QHBoxLayout(badge);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(4);
        auto* lock = new QLabel;
        lock->setPixmap(themedIcon("lock", kGold, 12).pixmap(12, 12));
        auto* btxt = new QLabel(tr("Admin"));
        QFont bf = btxt->font();
        bf.setBold(true);
        bf.setPointSizeF(bf.pointSizeF() * 0.86);   // browser: 12px against the 14px row
        btxt->setFont(bf);
        btxt->setStyleSheet(QStringLiteral("color:%1;").arg(kGold.name()));
        bl->addWidget(lock);
        bl->addWidget(btxt);
        h->addWidget(badge);
        h->addSpacing(12);
      }
      // Every row glyph is white on its filled button (browser: `button { color: white }`);
      // the expired row's amber fill takes a dark one instead.
      const QColor rowTxt("#ffffff");
      // One reconnect control per row, the SAME icon-only square in every state (browser
      // .connect-reconnect-one parity): an expired row says so with its amber fill and
      // the tooltip, not with a word its neighbours don't carry. On an expired session it
      // runs reauthenticate(): fresh session first, token prompt only if refused.
      auto* recon = mkIconBtn(themedIcon("refresh", expired ? QColor("#1f1f1f") : rowTxt, 15),
                              expired ? tr("Sign in to this server again")
                                      : tr("Reconnect this server"));
      recon->setObjectName(expired ? QStringLiteral("expiredReconnect")
                                   : QStringLiteral("rowReconnect"));
      // trash, not ✕: this FORGETS the server, the app's destructive action elsewhere.
      auto* disc = mkIconBtn(themedIcon("trash", rowTxt, 15),
                             tr("Disconnect (and forget) this server"));
      disc->setObjectName(QStringLiteral("rowDisconnect"));
      // Invite: mint a fresh session with the row's credential and put the link
      // "<url>#token=<tok>" on the clipboard. ADMIN rows only — a session-token
      // credential cannot mint (the server 401s it), and an anonymous session holds
      // no credential at all.
      if (cl && st == stencil::net::ServerClient::Status::Connected && admin) {
        auto* invite = mkIconBtn(themedIcon("link", rowTxt, 15),
                                 tr("Copy an invite link (mints a fresh session token)"));
        invite->setObjectName(QStringLiteral("inviteBtn"));
        h->addWidget(invite);
        h->addSpacing(6);   // browser .connect-actions gap
        QObject::connect(invite, &QPushButton::clicked, this, [this, url, invite, rowTxt] {
          stencil::net::ServerClient* c = manager_ ? manager_->find(url) : nullptr;
          if (!c) return;
          QPointer<ConnectDialog> self(this);
          QPointer<QPushButton> btn(invite);
          // Safe to capture c: the reply dies with the client, so a disconnected
          // row's mint callback simply never runs.
          c->mintInviteAsync([this, self, btn, rowTxt, c](bool ok, QString link) {
            if (!self) return;
            if (!ok) {
              emit toast(tr("Invite failed — %1").arg(c->lastError()), true);
              return;
            }
            QGuiApplication::clipboard()->setText(link);
            if (!btn) return;
            // Brief in-place feedback: the button flips to a check, then back.
            btn->setIcon(themedIcon("check", QColor("#ffffff"), 15));
            btn->setToolTip(tr("Invite link copied"));
            QTimer::singleShot(1500, btn, [btn, rowTxt] {
              if (!btn) return;
              btn->setIcon(themedIcon("link", rowTxt, 15));
              btn->setToolTip(tr("Copy an invite link (mints a fresh session token)"));
            });
          });
        });
      }
      h->addWidget(recon);
      h->addSpacing(6);
      h->addWidget(disc);
      QObject::connect(recon, &QPushButton::clicked, this, [this, url, expired] {
        if (expired) {   // plain reconnect first, then a token prompt if refused
          reauthenticate(url);
          return;
        }
        QPointer<ConnectDialog> self(this);
        manager_->reconnectAsync(url, [this, self, url](bool ok, QString err) {
          if (!self) return;
          // Named, not a bare "Reconnected": with more than one saved server the toast
          // has to say WHICH one signed back in (browser parity).
          emit toast(ok ? tr("Reconnected to %1").arg(url) : tr("Reconnect failed — %1").arg(err), !ok);
          rebuildList();
        });
      });
      QObject::connect(disc, &QPushButton::clicked, this, [this, url] { confirmDisconnect(url); });
      row->watchChildren();   // hover follows the whole card, not the child under it
      // The glass sweep the browser plays on `.connect-row:hover::after`, on the card and
      // each of its controls. Installed per row because rows are rebuilt on every change,
      // and modalChrome's dialog-wide pass only ever saw the batch that existed at open.
      installHoverShimmer(row);
      installHoverShimmerIn(row);
      auto* item = new QListWidgetItem(list_);
      item->setData(Qt::UserRole, admin);   // the kind filter's key
      item->setData(kRowUrlRole, url);      // Select all's pool reads it back
      list_->setItemWidget(item, row);
      // Sized AFTER parenting (the cascaded sheet is then in the hint, so the outline
      // has room) and capped to the viewport, so a long URL elides instead of scrolling.
      const int rowH = std::max(row->sizeHint().height(), kRowHeight);
      item->setSizeHint(QSize(rowWidth(), rowH));
      item->setData(kFilterFullHeightRole, rowH);   // the slot the filter collapses
    }
    applyKindFilter();
    updateBatchBar();
    // Deferred a turn: the edge fade needs the rows' laid-out geometry.
    QTimer::singleShot(0, this, [this] { applyRowReveal(); });
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
          // On the CONTROL clock, not the row's: Select all arrives in the same turn (the
          // bar opens with the first row), and a row still forming after the button had
          // landed read as the two appearing one after the other.
          if (DisintegrateOverlay::over(w, this, DisintegrateOverlay::Sweep::Gather, 0, 0,
                                        kConnArriveMs)) {
            w->setVisible(false);   // the slot stays; the motes are what the eye follows
            QPointer<QWidget> wp(w);
            QTimer::singleShot(kConnArriveMs, this, [wp] { if (wp) wp->setVisible(true); });
          }
        }
      });
    }
  }
}  // namespace stencil::gui

