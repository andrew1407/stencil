#include "connectDialog.hpp"

#include "serverClient.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  ConnectDialog::ConnectDialog(stencil::net::ConnectionManager* manager, QWidget* parent)
      : QDialog(parent), manager_(manager) {
    setWindowTitle(tr("Servers"));
    setMinimumWidth(420);

    auto* root = new QVBoxLayout(this);

    auto* form = new QHBoxLayout;
    form->addWidget(new QLabel(tr("URL")));
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText("http://host:8090");
    form->addWidget(urlEdit_, 1);
    form->addWidget(new QLabel(tr("Token")));
    tokenEdit_ = new QLineEdit;
    tokenEdit_->setPlaceholderText(tr("(optional)"));
    form->addWidget(tokenEdit_);
    auto* connectBtn = new QPushButton(tr("Connect"));
    form->addWidget(connectBtn);
    root->addLayout(form);

    auto* head = new QHBoxLayout;
    head->addWidget(
        new QLabel(tr("Connections — server projects appear with a golden outline")), 1);
    auto* reconnectAllBtn = new QPushButton(tr("Reconnect all"));
    reconnectAllBtn->setToolTip(tr("Re-establish every connection (re-validate / reissue tokens)"));
    head->addWidget(reconnectAllBtn);
    root->addLayout(head);
    list_ = new QListWidget;
    root->addWidget(list_, 1);

    QObject::connect(reconnectAllBtn, &QPushButton::clicked, this, [this] {
      if (manager_) manager_->reconnectAll();
      rebuildList();
    });

    auto* close = new QPushButton(tr("Close"));
    close->setDefault(true);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    footer->addWidget(close);
    root->addLayout(footer);

    QObject::connect(connectBtn, &QPushButton::clicked, this, &ConnectDialog::doConnect);
    QObject::connect(urlEdit_, &QLineEdit::returnPressed, this, &ConnectDialog::doConnect);
    QObject::connect(close, &QPushButton::clicked, this, &QDialog::accept);
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
      list_->addItem(tr("No servers connected."));
      return;
    }
    for (const QString& url : urls) {
      auto* row = new QWidget;
      auto* h = new QHBoxLayout(row);
      h->setContentsMargins(4, 2, 4, 2);
      h->addWidget(new QLabel(url), 1);
      auto* recon = new QPushButton(tr("Reconnect"));
      recon->setToolTip(tr("Re-establish this connection"));
      h->addWidget(recon);
      auto* disc = new QPushButton(tr("Disconnect"));
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
