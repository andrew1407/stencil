#include "connectDialog.hpp"
#include "../app/scrollReveal.hpp"             // revealDissolve (scroll edge fade)
#include "../support/disintegrateOverlay.hpp"  // disconnected rows come apart
#include "../support/dissolveEffect.hpp"       // scroll-edge grain dissolve
#include "../support/filterFade.hpp"           // filtered-out rows fade + collapse
#include "../support/guiHelpers.hpp"           // confirmYesNo()
#include "../support/modalReveal.hpp"          // motionReduced()

#include "connectionStore.hpp"
#include "iconSet.hpp"
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
#include <QScrollBar>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace stencil::gui {

  namespace {
    // The collaboration gold used for server points throughout the app (mirrors the
    // browser's --remote-gold), so shared servers read the same on every front-end.
    const QColor kGold("#d4a017");
    // The amber of "the credential, not the server, is the problem": the dot, the
    // expired note, and that row's outline.
    const QColor kAmber("#e0a800");
    // Row height: the 28px action buttons plus the card's own vertical padding.
    constexpr int kRowHeight = 40;

    QString rgba(const QColor& c, double a) {
      return QString("rgba(%1,%2,%3,%4)")
          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(a, 0, 'f', 3);
    }

    // A row that hovers as ONE card: Qt sends Enter/Leave to the child under the
    // pointer, so a bare :hover rule blinks off over a label.
    class RowCard : public QWidget {
     public:
      RowCard() {
        setAttribute(Qt::WA_StyledBackground, true);  // a bare QWidget won't paint one
        setProperty("hovered", false);
      }
      // Call once the row's children exist.
      void watchChildren() {
        for (QWidget* w : findChildren<QWidget*>()) w->installEventFilter(this);
      }

     protected:
      void enterEvent(QEnterEvent* e) override { QWidget::enterEvent(e); syncHover(); }
      void leaveEvent(QEvent* e) override { QWidget::leaveEvent(e); syncHover(); }
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Enter || e->type() == QEvent::Leave) syncHover();
        return QWidget::eventFilter(o, e);
      }

     private:
      void syncHover() {
        // The cursor test covers the gap between two children (Leave lands before the
        // next Enter); underMouse() is what a synthesized hover sets.
        bool on = underMouse() || rect().contains(mapFromGlobal(QCursor::pos()));
        if (!on)
          for (const QWidget* w : findChildren<QWidget*>())
            if (w->underMouse()) { on = true; break; }
        if (on == property("hovered").toBool()) return;
        setProperty("hovered", on);
        style()->unpolish(this);
        style()->polish(this);
      }
    };

    // A filled status dot: green=connected, amber=connecting, red=error — mirrors the
    // browser's connection-status dot.
    QPixmap statusDot(stencil::net::ServerClient::Status s) {
      using S = stencil::net::ServerClient::Status;
      // Amber for BOTH "connecting" and "expired": the server is fine either way,
      // only the credential is missing (browser parity — an expired row is amber,
      // never the red of an unreachable host).
      QColor c = s == S::Connected  ? QColor("#28a745")
               : s == S::Connecting ? kAmber
               : s == S::Expired    ? kAmber
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
    urlEdit_->setToolTip(tr("Collaboration server URL, e.g. http://localhost:8090 — "
                            "an invite link (…#token=…) signs in with its token"));
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

    // Section header + the credential-kind view filter (projects dialog's "Show:" idiom).
    {
      auto* head = new QHBoxLayout;
      head->addWidget(sectionLabel(tr("Connections")));
      head->addStretch(1);
      head->addWidget(new QLabel(tr("Show:")));
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
      // trash, not ✕: disconnecting FORGETS the server — the app's destructive glyph.
      discSel->setIcon(themedIcon("trash", QColor("#dc3545"), 15));
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
        if (!confirmYesNo(this, tr("Disconnect servers"),
                          tr("Disconnect and forget %1 selected server(s)?").arg(selected_.size())))
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
    list_->setObjectName(QStringLiteral("connList"));
    list_->setSpacing(6);  // vertical gaps so rows read as separate cards (projects parity)
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

    auto* hint = new QLabel(
        tr("Connections are saved and (optionally) restored on open · "
           "server projects show a golden outline — so do connections whose "
           "credential can mint invite tokens."));
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
    // …and the same for a filter fade: nothing half-faded survives into the close flight.
    if (filterFade_) filterFade_->finishNow();
    QDialog::done(r);
  }

  // The viewport minus the list's spacing, which QListView adds on BOTH sides of an
  // item — a viewport-wide slot overhung the right edge and cut the outline off.
  int ConnectDialog::rowWidth() const {
    if (!list_) return 0;
    return std::max(0, list_->viewport()->width() - 2 * list_->spacing());
  }

  // Fade rows at the list's top/bottom edges instead of cutting them mid-outline — the
  // widget twin of ProjectRowDelegate's dissolve (app/scrollReveal.hpp).
  void ConnectDialog::applyRowReveal() {
    if (!list_) return;
    QWidget* vp = list_->viewport();
    const int viewH = vp->height();
    QScrollBar* sb = list_->verticalScrollBar();
    const bool scrollable = sb && sb->maximum() > 0;   // nothing to scroll → no edges
    for (int i = 0; i < list_->count(); ++i) {
      QWidget* w = list_->itemWidget(list_->item(i));
      if (!w || w->isHidden()) continue;
      // A row mid-filter-fade owns its own opacity effect; two writers would flicker.
      if (w->property(kFilterFadeProperty).toBool()) continue;
      const int top = w->mapTo(vp, QPoint(0, 0)).y();
      const int h = w->height();
      const double d = scrollable ? revealDissolve(top, top + h, viewH) : 0.0;
      auto* fx = dynamic_cast<DissolveEffect*>(w->graphicsEffect());
      if (!fx) {
        if (d <= 0.0) continue;   // whole — don't allocate an effect to say so
        fx = new DissolveEffect(w);
        w->setGraphicsEffect(fx);
      }
      fx->setVisibleSpan(h > 0 ? std::clamp(double(-top) / h, 0.0, 1.0) : 0.0,
                         h > 0 ? std::clamp(double(viewH - top) / h, 0.0, 1.0) : 1.0);
      fx->setDissolve(d);
    }
  }

  bool ConnectDialog::eventFilter(QObject* watched, QEvent* event) {
    if (list_ && watched == list_->viewport() && event->type() == QEvent::Resize) {
      // Re-cap every row to the new viewport width (the URL label re-elides itself) —
      // same intent as the projects dialog's delegate sizeHint cap.
      const int w = rowWidth();
      for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->sizeHint().isValid())
          list_->item(i)->setSizeHint(QSize(w, list_->item(i)->sizeHint().height()));
      applyRowReveal();
    }
    return QDialog::eventFilter(watched, event);
  }

  void ConnectDialog::confirmDisconnect(const QString& url) {
    if (!manager_) return;
    if (!confirmYesNo(this, tr("Disconnect server"), tr("Disconnect and forget %1?").arg(url)))
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

  // The row transition: a filtered-out row fades and collapses its slot (support/
  // filterFade) — deliberately quicker and quieter than the disconnect scatter, so
  // "excluded by the picker" never reads as "forgotten".
  ListFilterFade* ConnectDialog::filterFade() {
    if (filterFade_ || !list_) return filterFade_;
    filterFade_ = new ListFilterFade(list_);
    filterFade_->writeRow = [this](QListWidgetItem* it, double p) {
      const QVariant full = it->data(kFilterFullHeightRole);
      if (full.isValid()) it->setSizeHint(QSize(rowWidth(), filterHeight(full.toInt(), p)));
      QWidget* w = list_->itemWidget(it);
      if (!w) return;
      if (p >= 1.0) {   // settled in — hand the row back to the scroll-edge reveal
        if (w->property(kFilterFadeProperty).toBool()) {
          w->setProperty(kFilterFadeProperty, false);
          w->setGraphicsEffect(nullptr);
        }
        return;
      }
      w->setProperty(kFilterFadeProperty, true);   // applyRowReveal skips it meanwhile
      auto* fx = dynamic_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
      if (!fx) {
        fx = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(fx);
      }
      fx->setOpacity(filterOpacity(p));
    };
    // The edge dissolve reads laid-out geometry, so it re-runs after every frame.
    filterFade_->afterFrame = [this] { applyRowReveal(); };
    return filterFade_;
  }

  void ConnectDialog::applyKindFilter() {
    if (!list_) return;
    const QString mode =
        kindFilter_ ? kindFilter_->currentData().toString() : QStringLiteral("all");
    // Any previous "nothing matches" line goes first, so the list holds only real rows
    // whenever something matches (their indices line up with manager_->urls()).
    for (int i = list_->count() - 1; i >= 0; --i)
      if (list_->item(i)->data(Qt::UserRole + 1).toBool()) delete list_->takeItem(i);
    auto wanted = [&mode](QListWidgetItem* it) {
      if (it->data(Qt::UserRole).isNull()) return true;   // "No servers connected." line
      const bool admin = it->data(Qt::UserRole).toBool();
      return mode == QLatin1String("all") || (mode == QLatin1String("admin")) == admin;
    };
    int rows = 0, shown = 0;
    for (int i = 0; i < list_->count(); ++i) {
      QListWidgetItem* it = list_->item(i);
      if (it->data(Qt::UserRole).isNull()) continue;
      ++rows;
      if (wanted(it)) ++shown;
    }
    if (auto* fade = filterFade()) fade->apply(wanted);
    if (rows == 0 || shown > 0) return;
    // Appended AFTER the rows, so the indices above stay valid while it is up.
    auto* none = new QListWidgetItem(mode == QLatin1String("admin")
                                         ? tr("No connection holds an admin credential.")
                                         : tr("Every connection holds an admin credential."),
                                     list_);
    none->setData(Qt::UserRole + 1, true);
    none->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
    none->setFlags(Qt::NoItemFlags);
  }

  // The projects list's card look (theme.cpp QListWidget::item — 6px radius, accent-soft
  // hover, ghost row buttons like QPushButton#pointDelBtn) plus the connection states:
  // admin gold and expired amber (browser .connect-expired). Set once; it cascades.
  QString ConnectDialog::rowStyleSheet() const {
    const QColor accent = palette().color(QPalette::Highlight);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QString soft = rgba(accent, dark ? 0.18 : 0.11);    // theme's %ACCENT_SOFT%
    const QString soft2 = rgba(accent, dark ? 0.30 : 0.20);   // …and %ACCENT_SOFT2%
    return QString(
               "QWidget#connRow,QWidget#connRowAdmin,QWidget#connRowExpired{"
               "border:1px solid transparent;border-radius:6px;background:transparent;}"
               "QWidget#connRowAdmin{border:2px solid %1;background:%2;}"
               "QWidget#connRowExpired{border:1px solid %3;background:%4;}"
               "QWidget#connRow[hovered=\"true\"]{background:%5;}"
               "QWidget#connRowAdmin[hovered=\"true\"]{background:%6;}"
               "QWidget#connRowExpired[hovered=\"true\"]{background:%7;}"
               "QPushButton[rowAction=\"true\"]{background:transparent;"
               "border:1px solid transparent;border-radius:6px;}"
               "QPushButton[rowAction=\"true\"]:hover{background:%5;border-color:%8;}"
               "QPushButton[rowAction=\"true\"]:pressed{background:%9;}")
        .arg(kGold.name(), rgba(kGold, 0.10), kAmber.name(), rgba(kAmber, 0.07), soft,
             rgba(kGold, 0.18), rgba(kAmber, 0.14), rgba(accent, 0.45), soft2);
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
    // A quiet, fixed-size icon button (browser's .connect-reconnect-one /
    // .connect-disconnect): transparent until hovered, like QPushButton#pointDelBtn.
    auto mkIconBtn = [](const QIcon& ic, const QString& tip) {
      auto* b = new QPushButton;
      b->setIcon(ic);
      b->setIconSize(QSize(16, 16));
      b->setFixedSize(30, 28);
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
      h->setContentsMargins(10, 6, 10, 6);   // the card's padding; the outline sits in it
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
      auto* dot = new QLabel;
      dot->setPixmap(statusDot(st));
      dot->setToolTip(st == stencil::net::ServerClient::Status::Connected ? tr("Connected")
                      : st == stencil::net::ServerClient::Status::Connecting ? tr("Connecting…")
                      : expired ? tr("Session expired — reconnect to sign in again")
                                : tr("Disconnected"));
      h->addWidget(dot);
      auto* mark = new QLabel;
      mark->setPixmap(themedIcon("server", kGold, 16).pixmap(16, 16));
      if (admin) mark->setToolTip(adminTip);
      h->addWidget(mark);
      auto* urlLbl = new ElidedLabel(url);   // elides so the buttons never clip
      if (admin) {
        QFont uf = urlLbl->font();
        uf.setBold(true);
        urlLbl->setFont(uf);
        urlLbl->setStyleSheet(QStringLiteral("color:%1;").arg(kGold.name()));
        urlLbl->setToolTip(url + "\n" + adminTip);
      }
      h->addWidget(urlLbl, 1);
      const QColor rowTxt = palette().color(QPalette::WindowText);
      // One reconnect control per row, icon-only like every other row action; on an
      // EXPIRED row it runs reauthenticate() (fresh session first, token prompt only if
      // refused). Not the browser's labelled button: the amber card already says why.
      auto* recon = mkIconBtn(themedIcon("refresh", rowTxt, 16),
                              expired ? tr("Reconnect — sign in to this server again")
                                      : tr("Reconnect this server"));
      recon->setObjectName(expired ? QStringLiteral("expiredReconnect")
                                   : QStringLiteral("rowReconnect"));
      // trash, not ✕: this FORGETS the server, the app's destructive action elsewhere.
      auto* disc = mkIconBtn(themedIcon("trash", QColor("#dc3545"), 16), tr("Disconnect"));
      disc->setObjectName(QStringLiteral("rowDisconnect"));
      if (expired) {
        // Short in the row (it competes with the URL for width), full sentence on
        // the tooltip — the row's amber outline and dot carry the same state.
        auto* note = new QLabel(tr("Session expired"));
        note->setObjectName(QStringLiteral("expiredNote"));
        note->setToolTip(tr("Session expired — reconnect to sign in again"));
        note->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kAmber.name()));
        note->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        h->addWidget(note);
      }
      // Invite: mint a fresh session with the row's credential and put the link
      // "<url>#token=<tok>" on the clipboard. ADMIN rows only — a session-token
      // credential cannot mint (the server 401s it), and an anonymous session holds
      // no credential at all.
      if (cl && st == stencil::net::ServerClient::Status::Connected && admin) {
        auto* invite = mkIconBtn(themedIcon("share", rowTxt, 16),
                                 tr("Copy an invite link (mints a fresh session token)"));
        invite->setObjectName(QStringLiteral("inviteBtn"));
        h->addWidget(invite);
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
              QMessageBox::warning(this, tr("Servers"),
                                   tr("Could not mint an invite link — %1").arg(c->lastError()));
              return;
            }
            QGuiApplication::clipboard()->setText(link);
            if (!btn) return;
            // Brief in-place feedback: the button flips to a check, then back.
            btn->setIcon(themedIcon("check", QColor("#28a745"), 16));
            btn->setToolTip(tr("Invite link copied"));
            QTimer::singleShot(1500, btn, [btn, rowTxt] {
              if (!btn) return;
              btn->setIcon(themedIcon("share", rowTxt, 16));
              btn->setToolTip(tr("Copy an invite link (mints a fresh session token)"));
            });
          });
        });
      }
      h->addWidget(recon);
      h->addWidget(disc);
      QObject::connect(recon, &QPushButton::clicked, this, [this, url, expired] {
        if (expired) {   // plain reconnect first, then a token prompt if refused
          reauthenticate(url);
          return;
        }
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
      row->watchChildren();   // hover follows the whole card, not the child under it
      auto* item = new QListWidgetItem(list_);
      item->setData(Qt::UserRole, admin);   // the kind filter's key
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
