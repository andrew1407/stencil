// A connection row's own controls: reconnect (reauthenticate on an expired session), the admin
// row's invite, and disconnect. Built into the card from ConnectDialogRow.cpp.
#include "ConnectDialog.hpp"
#include "ReorderableListWidget.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "../../support/control/controlReveal.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "connectDialogParts.hpp"
#include "../../support/theme/filterFade.hpp"
#include "../../support/control/FlowLayout.hpp"
#include "../../support/guiHelpers.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include "../../support/theme/theme.hpp"
#include "../../support/tip/tipContent.hpp"
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

  // Every glyph is white on its filled button (browser `button { color: white }`); an expired
  // row's amber fill takes a dark one instead.
  void ConnectDialog::addConnectionRowActions(QHBoxLayout* h, const QString& url,
                                             stencil::net::ServerClient* cl, bool expired,
                                             bool admin) {
    const QColor rowTxt("#ffffff");
    const auto st = cl ? cl->getStatus() : stencil::net::ServerClient::Status::ERROR;
    // One reconnect control per row, the SAME icon-only square in every state (browser
    // .connect-reconnect-one). On an expired session it runs reauthenticate(): session, then token.
    auto* recon = makeRowActionButton(themedIcon("refresh", expired ? QColor("#1f1f1f") : rowTxt, 15),
                            expired ? tr("Sign in to this server again")
                                    : tr("Reconnect this server"));
    recon->setObjectName(expired ? QStringLiteral("expiredReconnect")
                                 : QStringLiteral("rowReconnect"));
    // trash, not ✕: this FORGETS the server, the app's destructive action elsewhere.
    auto* disc = makeRowActionButton(themedIcon("trash", rowTxt, 15),
                           tr("Disconnect (and forget) this server"));
    disc->setObjectName(QStringLiteral("rowDisconnect"));
    // Invite: mint a fresh session and put "<url>#token=<tok>" on the clipboard. ADMIN rows only -
    // a session-token credential cannot mint (the server 401s it), and an anonymous one has none.
    if (cl && st == stencil::net::ServerClient::Status::CONNECTED && admin) {
      auto* invite = makeRowActionButton(themedIcon("link", rowTxt, 15),
                               tr("Copy an invite link (mints a fresh session token)"));
      invite->setObjectName(QStringLiteral("inviteBtn"));
      h->addWidget(invite);
      h->addSpacing(6);   // browser .connect-actions gap
      QObject::connect(invite, &QPushButton::clicked, this, [this, url, invite, rowTxt] {
        stencil::net::ServerClient* c = manager ? manager->find(url) : nullptr;
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
      manager->reconnectAsync(url, [this, self, url](bool ok, QString err) {
        if (!self) return;
        // Named, not a bare "Reconnected": with more than one saved server the toast
        // has to say WHICH one signed back in (browser parity).
        emit toast(ok ? tr("Reconnected to %1").arg(url) : tr("Reconnect failed — %1").arg(err), !ok);
        rebuildList();
      });
    });
    QObject::connect(disc, &QPushButton::clicked, this, [this, url] { confirmDisconnect(url); });
  }

}  // namespace stencil::gui
