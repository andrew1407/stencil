#include "connectDialog.hpp"

#include "connectionStore.hpp"
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
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // The collaboration gold used for server markers throughout the app (mirrors the
    // browser's --remote-gold), so shared servers read the same on every front-end.
    const QColor kGold("#d4a017");

    // A minimal two-unit "server rack" glyph (the desktop analogue of the browser's
    // `server` icon): two stacked rounded units each with a status LED, stroked in
    // `color`. Used for the header and the per-connection markers.
    QPixmap serverGlyph(int size, const QColor& color) {
      QPixmap pm(size, size);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen(color, qMax(1.0, size * 0.08)));
      p.setBrush(Qt::NoBrush);
      const qreal m = size * 0.14;
      const qreal w = size - 2 * m;
      const qreal h = size * 0.30;
      const qreal r = size * 0.09;
      const QRectF top(m, m, w, h);
      const QRectF bot(m, size - m - h, w, h);
      p.drawRoundedRect(top, r, r);
      p.drawRoundedRect(bot, r, r);
      p.setBrush(color);
      p.setPen(Qt::NoPen);
      const qreal d = size * 0.07;
      const qreal lx = m + size * 0.12;
      p.drawEllipse(QPointF(lx, top.center().y()), d, d);
      p.drawEllipse(QPointF(lx, bot.center().y()), d, d);
      return pm;
    }

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

    // A red "✕" glyph (the browser's danger-tinted disconnect icon). QStyle's
    // SP_DialogCloseButton renders near-black on macOS — invisible on the dark row.
    QPixmap crossGlyph(int size, const QColor& color) {
      QPixmap pm(size, size);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen(color, qMax(1.5, size * 0.13), Qt::SolidLine, Qt::RoundCap));
      const qreal m = size * 0.28;
      p.drawLine(QPointF(m, m), QPointF(size - m, size - m));
      p.drawLine(QPointF(size - m, m), QPointF(m, size - m));
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
    auto* iconLbl = new QLabel;
    iconLbl->setPixmap(serverGlyph(22, palette().color(QPalette::WindowText)));
    header->addWidget(iconLbl);
    auto* titleLbl = new QLabel(tr("Servers"));
    QFont tf = titleLbl->font();
    tf.setPointSizeF(tf.pointSizeF() + 3);
    tf.setBold(true);
    titleLbl->setFont(tf);
    header->addWidget(titleLbl);
    header->addStretch(1);
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    header->addWidget(closeBtn);
    root->addLayout(header);
    root->addWidget(hLine());

    // ── Connect a server.
    root->addWidget(sectionLabel(tr("Connect a server")));
    auto* form = new QGridLayout;
    form->setColumnStretch(1, 1);
    form->setHorizontalSpacing(10);
    form->addWidget(new QLabel(tr("URL")), 0, 0);
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText("http://host:8090");
    form->addWidget(urlEdit_, 0, 1);
    form->addWidget(new QLabel(tr("Token")), 1, 0);
    tokenEdit_ = new QLineEdit;
    tokenEdit_->setPlaceholderText(tr("(optional)"));
    form->addWidget(tokenEdit_, 1, 1);
    root->addLayout(form);

    // Connect (left) + Reconnect all (right), grouped on one row.
    auto* actions = new QHBoxLayout;
    auto* connectBtn = new QPushButton(tr("Connect"));
    connectBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    connectBtn->setDefault(true);
    actions->addWidget(connectBtn);
    actions->addStretch(1);
    auto* reconnectAllBtn = new QPushButton(tr("Reconnect all"));
    reconnectAllBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
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

    // ── Connections.
    root->addWidget(sectionLabel(tr("Connections")));
    list_ = new QListWidget;
    list_->setSpacing(4);
    root->addWidget(list_, 1);

    auto* hint = new QLabel(
        tr("Connections are saved and (optionally) restored on open · "
           "server projects show a golden outline."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(mid); font-size: 11px;");
    root->addWidget(hint);

    QObject::connect(reconnectAllBtn, &QPushButton::clicked, this, [this] {
      if (manager_) manager_->reconnectAll();
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

  void ConnectDialog::rebuildList() {
    if (!manager_ || !list_) return;
    list_->clear();
    const QStringList urls = manager_->urls();
    if (urls.isEmpty()) {
      auto* empty = new QListWidgetItem(tr("No servers connected."), list_);
      empty->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
      empty->setFlags(Qt::NoItemFlags);
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
    for (const QString& url : urls) {
      auto* row = new QWidget;
      auto* h = new QHBoxLayout(row);
      h->setContentsMargins(8, 4, 8, 4);
      h->setSpacing(8);
      stencil::net::ServerClient* cl = manager_->find(url);
      const auto st = cl ? cl->status() : stencil::net::ServerClient::Status::Error;
      auto* dot = new QLabel;
      dot->setPixmap(statusDot(st));
      dot->setToolTip(st == stencil::net::ServerClient::Status::Connected ? tr("Connected")
                      : st == stencil::net::ServerClient::Status::Connecting ? tr("Connecting…")
                                                                            : tr("Disconnected"));
      h->addWidget(dot);
      auto* mark = new QLabel;
      mark->setPixmap(serverGlyph(16, kGold));
      h->addWidget(mark);
      h->addWidget(new QLabel(url), 1);
      auto* recon = mkIconBtn(style()->standardIcon(QStyle::SP_BrowserReload),
                              tr("Reconnect this server"));
      auto* disc = mkIconBtn(QIcon(crossGlyph(16, QColor("#dc3545"))), tr("Disconnect"));
      h->addWidget(recon);
      h->addWidget(disc);
      QObject::connect(recon, &QPushButton::clicked, this, [this, url] {
        QString err;
        if (!manager_->reconnect(url, err))
          QMessageBox::warning(this, tr("Servers"),
                               tr("Could not reconnect — %1").arg(err));
        rebuildList();
      });
      QObject::connect(disc, &QPushButton::clicked, this, [this, url] {
        manager_->disconnectFrom(url);
        rebuildList();
      });
      auto* item = new QListWidgetItem(list_);
      item->setSizeHint(row->sizeHint());
      list_->setItemWidget(item, row);
    }
  }

}  // namespace stencil::gui
