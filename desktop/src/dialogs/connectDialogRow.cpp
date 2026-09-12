// One connection card, as rebuildList() makes it: the drag grip, the select checkbox, the
// status dot and the elided URL, then the admin/expired badge. Its own row of controls is
// connectDialogRowActions.cpp.
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

  // One connection card: the drag grip, the select checkbox, the status dot and the
  // elided URL, then the admin/expired badge. `rowIndex` is the drag-reorder slot.
  void ConnectDialog::addConnectionRow(const QString& url, int rowIndex) {
    auto* reList = static_cast<ReorderableListWidget*>(list_);
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
    addConnectionRowActions(h, url, cl, expired, admin);
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

}  // namespace stencil::gui
