// Headless check for the Servers dialog's connection rows (dialogs/connectDialog):
// a long URL elides inside the viewport, each row is a projects-style card whose
// outline is never clipped (and hovers as one), a row the viewport cuts dissolves at
// the edge, removal retires-then-finalizes, and a new row gathers in. A mock
// QTcpServer stands in for the collaboration server, so no Go server is needed.
#include "connectDialog.hpp"
#include "disintegrateOverlay.hpp"
#include "dissolveEffect.hpp"   // the scroll-edge fade the rows carry
#include "serverClient.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>
#include <functional>

using stencil::gui::ConnectDialog;
using stencil::gui::DisintegrateOverlay;
using stencil::gui::DissolveEffect;
using stencil::net::ConnectionManager;

#include "support/check.hpp"

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static void pumpUntil(const std::function<bool()>& pred, int timeoutMs = 3000) {
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeoutMs)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  // Keep the dialog's QSettings reads/writes out of the real per-user config.
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("connectRowHeadless");

  // ── Mock server: any request gets 200 {"token":"tok"} (serves POST /auth/token).
  QTcpServer server;
  check(server.listen(QHostAddress::LocalHost, 0), "mock server listens");
  QObject::connect(&server, &QTcpServer::newConnection, [&] {
    while (QTcpSocket* s = server.nextPendingConnection()) {
      QObject::connect(s, &QTcpSocket::readyRead, [s] {
        s->readAll();
        const QByteArray body = "{\"token\":\"tok\"}";
        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->flush();
        s->disconnectFromHost();
      });
    }
  });
  const quint16 port = server.serverPort();
  // Userinfo survives normalizeBase (scheme + authority), so this yields a row URL far
  // wider than the dialog — exactly the overflow the elide has to absorb.
  const QString longUrl =
      QStringLiteral("http://a-very-long-user-name-meant-to-stretch-the-connection-row-"
                     "well-past-any-sane-dialog-viewport-width@127.0.0.1:%1")
          .arg(port);
  const QString shortUrl = QStringLiteral("http://u2@127.0.0.1:%1").arg(port);

  ConnectionManager mgr;
  QString err;
  check(mgr.connectTo(longUrl, QString(), err), "connects the long-URL server");

  ConnectDialog dlg(&mgr);
  dlg.resize(480, 420);
  dlg.show();
  pumpFor(50);  // let the show-time layout (and the viewport resize re-cap) settle

  auto* list = dlg.findChild<QListWidget*>();
  check(list != nullptr, "finds the connections list");
  if (!list) return 1;

  // ── Row layout: everything fits the viewport, the URL elides, nothing scrolls sideways.
  check(list->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
        "horizontal scrollbar is AlwaysOff");
  check(list->count() == 1, "one connection row");
  QWidget* row = list->itemWidget(list->item(0));
  check(row != nullptr, "row hosts a widget");
  const int vpw = list->viewport()->width();
  check(list->item(0)->sizeHint().width() <= vpw, "item size hint capped to the viewport");
  check(row && row->width() <= vpw, "row widget no wider than the viewport");
  QLabel* urlLabel = nullptr;
  for (QLabel* l : row->findChildren<QLabel*>())
    if (l->toolTip() == longUrl) urlLabel = l;
  check(urlLabel != nullptr, "URL label carries the full url on its tooltip");
  check(urlLabel && urlLabel->text() != longUrl, "long URL is not shown verbatim");
  check(urlLabel && urlLabel->text().contains(QChar(0x2026)), "long URL is elided (…)");
  const auto btns = row->findChildren<QPushButton*>();
  check(btns.size() == 2, "row keeps both trailing action buttons");
  for (QPushButton* b : btns) {
    const int right = b->mapTo(list->viewport(), QPoint(b->width(), 0)).x();
    check(right <= vpw, "action button sits fully inside the viewport");
  }

  // ── The row is a CARD whose outline is never clipped. QListView insets every item
  // by the list's spacing on BOTH sides, so a viewport-wide slot overhung the right
  // edge — which is where the gold admin outline was lost.
  check(list->item(0)->sizeHint().width() + 2 * list->spacing() <= vpw,
        "the row slot leaves the list's spacing on both sides");
  check(row && row->mapTo(list->viewport(), QPoint(row->width(), 0)).x() <= vpw - list->spacing(),
        "…so the row's right edge (and its outline) stays inside the viewport");
  // Height likewise: measured after parenting, so the cascaded card sheet is in it.
  check(list->item(0)->sizeHint().height() >= row->sizeHint().height(),
        "the row slot is at least as tall as the row wants to be");
  check(row->height() == list->item(0)->sizeHint().height(),
        "…and the row widget fills exactly that slot");
  check(row->objectName() == QStringLiteral("connRow"),
        "a plain connection row is a card like the projects rows");
  check(list->styleSheet().contains(QStringLiteral("border-radius:6px")),
        "…styled by the list's own cascading row sheet");

  // ── Hover follows the whole card: Qt sends Enter/Leave to the child under the
  // pointer, and hovering a label must not blink the wash off.
  QLabel* hoverTarget = urlLabel ? urlLabel : row->findChild<QLabel*>();
  if (hoverTarget) {
    hoverTarget->setAttribute(Qt::WA_UnderMouse, true);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(hoverTarget, &enter);
    check(row->property("hovered").toBool(), "hovering a child hovers the whole row card");
    hoverTarget->setAttribute(Qt::WA_UnderMouse, false);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(hoverTarget, &leave);
    check(!row->property("hovered").toBool(), "…and leaving it un-hovers the card");
  }

  // ── Removal: retire-then-finalize — slot held, empty state only after the dust.
  QPushButton* disc = row->findChild<QPushButton*>(QStringLiteral("rowDisconnect"));
  check(disc != nullptr, "finds the row's disconnect button");
  if (disc) check(disc->toolTip() == QStringLiteral("Disconnect"), "…which says what it does");
  auto* dismiss = new QTimer;  // answers the confirm box that blocks disc->click()
  dismiss->setInterval(20);
  QObject::connect(dismiss, &QTimer::timeout, [dismiss] {
    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
      if (auto* yes = box->button(QMessageBox::Yes)) {
        yes->click();
        dismiss->stop();
        dismiss->deleteLater();
      }
  });
  dismiss->start();
  if (disc) disc->click();
  check(!mgr.urls().contains(longUrl), "model drops the url immediately");
  check(list->count() == 1, "the row's slot is held while the dust plays");
  check(list->itemWidget(list->item(0)) == nullptr, "retired row is blanked at once");
  check(list->item(0)->text().isEmpty(), "no premature empty-state during removal");
  check(dlg.findChild<QWidget*>(DisintegrateOverlay::kObjectName) != nullptr,
        "removal dust overlay is playing");
  pumpFor(DisintegrateOverlay::kMs / 3);
  check(list->count() == 1 && list->item(0)->text().isEmpty(),
        "empty-state still absent mid-animation");
  pumpUntil([&] {
    return list->count() == 1 &&
           list->item(0)->text() == QStringLiteral("No servers connected.");
  });
  check(list->count() == 1 &&
            list->item(0)->text() == QStringLiteral("No servers connected."),
        "empty-state appears once the animation completes");

  // ── Add: the reverse (gather) — slot opens blank, then the row materializes.
  auto* urlEdit = dlg.findChild<QLineEdit*>();
  QPushButton* connectBtn = nullptr;
  for (QPushButton* b : dlg.findChildren<QPushButton*>())
    if (b->text() == QStringLiteral("Connect")) connectBtn = b;
  check(urlEdit != nullptr && connectBtn != nullptr, "finds the connect form");
  if (urlEdit && connectBtn) {
    urlEdit->setText(shortUrl);
    connectBtn->click();
    check(mgr.urls().contains(shortUrl), "connects the second server");
    pumpUntil([&] {
      QWidget* w = list->count() == 1 ? list->itemWidget(list->item(0)) : nullptr;
      return w && !w->isVisible();
    }, 1000);
    QWidget* fresh = list->count() == 1 ? list->itemWidget(list->item(0)) : nullptr;
    check(fresh && !fresh->isVisible(), "new row starts hidden under the gather motes");
    check(dlg.findChild<QWidget*>(DisintegrateOverlay::kObjectName) != nullptr,
          "gather overlay is playing");
    pumpUntil([&] {
      QWidget* w = list->count() == 1 ? list->itemWidget(list->item(0)) : nullptr;
      return w && w->isVisible();
    });
    fresh = list->count() == 1 ? list->itemWidget(list->item(0)) : nullptr;
    check(fresh && fresh->isVisible(), "row materializes once the gather completes");
  }

  // ── Compact POPOVER shape (mainWindow execMaybePopover): the dialog-level
  // minimumWidth is dropped and the box shrinks to sizeHint. The row's fixed controls
  // (grip/checkbox/status/icon/reconnect/disconnect) used to eat that budget, leaving
  // the URL ~70px ("htt…090"); the list's own minimum keeps the mini form wide enough
  // for the URL to take real width. Same rows, same styling as the full dialog.
  {
    ConnectDialog mini(&mgr);
    mini.setWindowFlags(mini.windowFlags() | Qt::FramelessWindowHint);
    const QSize cap(470, 590);           // execMaybePopover's compact cap, verbatim
    mini.setMinimumSize(0, 0);
    mini.setMaximumSize(cap);
    mini.resize(qMin(mini.sizeHint().width(), cap.width()),
                qMin(mini.sizeHint().height(), cap.height()));
    mini.show();
    pumpFor(80);
    auto* mlist = mini.findChild<QListWidget*>();
    check(mlist != nullptr, "compact popover builds the connections list");
    if (mlist && mlist->count() == 1) {
      QWidget* mrow = mlist->itemWidget(mlist->item(0));
      check(mrow != nullptr, "compact popover row hosts a widget");
      QLabel* murl = nullptr;
      for (QLabel* l : mrow->findChildren<QLabel*>())
        if (l->toolTip() == shortUrl) murl = l;
      check(murl != nullptr, "compact row keeps the URL label");
      // The whole point: a URL this short is shown in FULL, not crushed to "htt…090".
      check(murl && murl->text() == shortUrl,
            "compact popover shows the full URL (no premature elide)");
      check(murl && murl->width() >= 150, "URL label gets real width in the compact form");
      // …and the trailing controls still sit inside the viewport, right-aligned.
      const auto mbtns = mrow->findChildren<QPushButton*>();
      check(mbtns.size() == 2, "compact row keeps both trailing action buttons");
      for (QPushButton* b : mbtns)
        check(b->mapTo(mlist->viewport(), QPoint(b->width(), 0)).x() <= mlist->viewport()->width(),
              "compact row action button sits inside the viewport");
    }
  }

  // ── Scroll edges: a row the viewport cuts dissolves instead of being sliced
  // across its outline (projects-list parity).
  {
    ConnectionManager many;
    QString e;
    for (int i = 0; i < 8; ++i)
      many.connectTo(QStringLiteral("http://row%1@127.0.0.1:%2").arg(i).arg(port), QString(), e);
    ConnectDialog tall(&many);
    tall.resize(520, 430);   // shorter than eight rows: the list has to scroll
    tall.show();
    pumpFor(120);
    auto* tl = tall.findChild<QListWidget*>(QStringLiteral("connList"));
    check(tl != nullptr && tl->count() == 8, "eight rows in the short list");
    if (tl && tl->count() == 8) {
      check(tl->verticalScrollBar()->maximum() > 0, "…which therefore scrolls");
      const int viewH = tl->viewport()->height();
      int whole = -1, cut = -1;
      for (int i = 0; i < tl->count(); ++i) {
        QWidget* w = tl->itemWidget(tl->item(i));
        if (!w) continue;
        const int top = w->mapTo(tl->viewport(), QPoint(0, 0)).y();
        if (top >= 0 && top + w->height() <= viewH) { if (whole < 0) whole = i; }
        else if (top < viewH && top + w->height() > viewH) cut = i;
      }
      check(whole >= 0 && cut >= 0, "…with both a whole row and one the bottom edge cuts");
      if (whole >= 0 && cut >= 0) {
        auto* wholeFx =
            dynamic_cast<DissolveEffect*>(tl->itemWidget(tl->item(whole))->graphicsEffect());
        auto* cutFx =
            dynamic_cast<DissolveEffect*>(tl->itemWidget(tl->item(cut))->graphicsEffect());
        check(!wholeFx || wholeFx->dissolve() <= 0.0, "a fully visible row is not dissolved");
        check(cutFx && cutFx->dissolve() > 0.0, "…while the clipped row fades at the edge");
      }
    }
  }

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
