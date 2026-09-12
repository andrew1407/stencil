// One connection card, as rebuildList() makes it; its row of controls is connectDialogRowActions.cpp.
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
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace stencil::gui {

  // `rowIndex` is the drag-reorder slot.
  void ConnectDialog::addConnectionRow(const QString& url, int rowIndex) {
    auto* reList = static_cast<ReorderableListWidget*>(list_);
    const int myIndex = rowIndex++;
    stencil::net::ServerClient* client = manager_->find(url);
    // ADMIN = a credential PROVEN able to mint session tokens; marked in the projects list's collaboration gold.
    const bool admin = client && client->isAdmin();
    stencil::net::ServerClient* cl = client;
    const auto st = cl ? cl->status() : stencil::net::ServerClient::Status::ERROR;
    const bool expired = st == stencil::net::ServerClient::Status::EXPIRED;
    const QString adminTip =
        tr("Admin credential — this connection can mint session tokens (invite links)");
    auto* row = new RowCard;
    row->setObjectName(admin      ? QStringLiteral("connRowAdmin")
                       : expired  ? QStringLiteral("connRowExpired")
                                  : QStringLiteral("connRow"));
    if (admin) row->setToolTip(adminTip);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(10, 8, 10, 8);
    // Browser: 12 between items, 6 inside .connect-url and the actions; the dot's pixmap carries its 2px halo.
    h->setSpacing(0);
    // Browser .connect-row align-items: center — taller items otherwise stretched and sat off-centre.
    h->setAlignment(Qt::AlignVCenter);
    auto* grip = new DragGrip;
    // Scoped by objectName: bare property rules bleed into the widget's own QToolTip.
    grip->setObjectName("connGrip");
    grip->setStyleSheet("#connGrip { color: palette(mid); letter-spacing: -3px; }");
    grip->onDrag = [reList, myIndex] { reList->beginRowDrag(myIndex); };
    h->addWidget(grip);
    h->addSpacing(12);
    auto* cb = new QCheckBox;
    cb->setObjectName(QStringLiteral("connRowSelect"));
    cb->setChecked(selected_.contains(url));
    cb->setToolTip(tr("Select for batch action"));
    // Browser .connect-selected, repolished in place.
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
    const QString statusText =
        st == stencil::net::ServerClient::Status::CONNECTED    ? tr("Connected")
        : st == stencil::net::ServerClient::Status::CONNECTING ? tr("Connecting…")
        : expired ? tr("Session expired — reconnect to sign in again")
                  : tr("Disconnected");
    auto* dot = new QLabel;
    dot->setPixmap(statusDot(st));
    dot->setToolTip(statusText);
    h->addWidget(dot);
    h->addSpacing(4);
    auto* mark = new QLabel;
    mark->setPixmap(themedIcon("server", GOLD, 14).pixmap(14, 14));
    h->addWidget(mark);
    h->addSpacing(6);
    auto* urlLbl = new ElidedLabel(url);
    // "<state> — <url>": support/tipContent reads the dash as heading/subtitle.
    urlLbl->setToolTip(statusText + QStringLiteral(" — ") + url);
    h->addWidget(urlLbl, 1);
    h->addSpacing(12);
    // Its own row child, so the URL's elide can never clip the badge (browser .connect-admin-badge).
    if (admin) {
      // A plain QWidget, NOT a QLabel: QLabel::sizeHint() ignores a child layout and collapsed the pair.
      auto* badge = new QWidget;
      badge->setObjectName(QStringLiteral("connAdminBadge"));
      badge->setToolTip(adminTip);
      badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
      auto* bl = new QHBoxLayout(badge);
      bl->setContentsMargins(0, 0, 0, 0);
      bl->setSpacing(4);
      auto* lock = new QLabel;
      lock->setPixmap(themedIcon("lock", GOLD, 12).pixmap(12, 12));
      auto* btxt = new QLabel(tr("Admin"));
      QFont bf = btxt->font();
      bf.setBold(true);
      bf.setPointSizeF(bf.pointSizeF() * 0.86);
      btxt->setFont(bf);
      btxt->setStyleSheet(QStringLiteral("color:%1;").arg(GOLD.name()));
      bl->addWidget(lock);
      bl->addWidget(btxt);
      h->addWidget(badge);
      h->addSpacing(12);
    }
    addConnectionRowActions(h, url, cl, expired, admin);
    row->watchChildren();
    // Installed per row: rows are rebuilt on every change, and modalChrome's pass only saw the batch at open.
    installHoverShimmer(row);
    installHoverShimmerIn(row);
    auto* item = new QListWidgetItem(list_);
    item->setData(Qt::UserRole, admin);
    item->setData(ROW_URL_ROLE, url);
    list_->setItemWidget(item, row);
    // Sized AFTER parenting (the cascaded sheet is then in the hint) and capped to the viewport.
    const int rowH = std::max(row->sizeHint().height(), ROW_HEIGHT);
    item->setSizeHint(QSize(rowWidth(), rowH));
    item->setData(FILTER_FULL_HEIGHT_ROLE, rowH);
  }

}  // namespace stencil::gui
