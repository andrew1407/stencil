// Headless check for the Servers dialog's connection rows (dialogs/connectDialog):
//   - a long URL ELIDES inside the list viewport (tooltip keeps the full text), the
//     trailing per-row buttons stay inside the row, rows never exceed the viewport
//     width, and the horizontal scrollbar is off — the row used to grow past the
//     viewport and clip mid-character behind a horizontal scrollbar;
//   - removal retires-then-finalizes (projectsDialog parity): the row blanks at once,
//     its empty slot is held while the disintegrate dust plays, and the "No servers
//     connected." empty state appears only AFTER the animation completes;
//   - a newly-connected row materializes with the reverse (Sweep::Gather): the slot
//     opens with the row hidden under the motes, then the real row appears.
// A mock QTcpServer stands in for the collaboration server (POST /auth/token → 200),
// so no Go server is needed. Built only when Qt is present, like the other *.headless.
#include "connectDialog.hpp"
#include "disintegrateOverlay.hpp"
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

  // ── Removal: retire-then-finalize — slot held, empty state only after the dust.
  QPushButton* disc = nullptr;
  for (QPushButton* b : btns)
    if (b->toolTip() == QStringLiteral("Disconnect")) disc = b;
  check(disc != nullptr, "finds the row's disconnect button");
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

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
