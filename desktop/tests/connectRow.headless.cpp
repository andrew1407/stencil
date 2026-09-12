// Headless check for the Servers dialog's connection rows (dialogs/connectDialog):
// a long URL elides inside the viewport, each row is a projects-style card whose
// outline is never clipped (and hovers as one), a row the viewport cuts dissolves at
// the edge, removal retires-then-finalizes, and a new row gathers in. The kind
// filter is a question re-answered: what it excludes is gone at once with nothing to
// watch, the rows that are LEFT arrive, none of the removal's dust is spent, and reduced
// motion skips to the end. A mock
// QTcpServer stands in for the collaboration server, so no Go server is needed.
#include "connectDialog.hpp"
#include "theme.hpp"   // the app stylesheet these metrics are measured under
#include "disintegrateOverlay.hpp"
#include "dissolveEffect.hpp"   // the scroll-edge fade the rows carry
#include "filterFade.hpp"       // …and the lighter one a FILTER change plays
#include "serverClient.hpp"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGraphicsOpacityEffect>
#include <QHostAddress>
#include <QCheckBox>
#include <QLabel>
#include "flowLayout.hpp"
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSize>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>
#include <functional>

using stencil::gui::ConnectDialog;
using stencil::gui::DisintegrateOverlay;
using stencil::gui::DissolveEffect;
using stencil::gui::FILTER_FADE_MS;
using stencil::gui::FILTER_FADE_PROPERTY;
using stencil::gui::FILTER_FULL_HEIGHT_ROLE;
using stencil::net::ConnectionManager;

#include "support/check.hpp"
#include "support/connectNow.hpp"

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
  // The APP's stylesheet, as main() sets it — without it these metrics are not the app's:
  // `QListWidget::item { padding: 4px }` takes 8px out of every slot, which is the squeeze
  // that pushed the row's buttons off their line.
  app.setStyleSheet(stencil::gui::buildStylesheet(true, "violet"));
  app.setPalette(stencil::gui::buildQPalette(true, "violet"));

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
  check(stencil::test::connectNow(mgr, longUrl, QString(), err), "connects the long-URL server");

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
    if (l->toolTip().endsWith(longUrl)) urlLabel = l;   // "<state> — <url>"
  check(urlLabel != nullptr, "URL label carries the full url on its tooltip");
  check(urlLabel && urlLabel->text() != longUrl, "long URL is not shown verbatim");
  check(urlLabel && urlLabel->text().contains(QChar(0x2026)), "long URL is elided (…)");
  // ── Row metrics, measured against the browser's .connect-row: a 43-tall card at
  // padding 8px 10px / gap 12 / radius 8, action buttons 31x25, every child on one centre
  // line. Qt reaches those only with `border: none` (a 1px one adds 2px to both axes),
  // and the labelled expired button is pinned to the same box as the trash beside it.
  check(row->height() == 43, "the row card is the browser's own height");
  {
    int centre = -1;
    bool aligned = true;
    for (QWidget* w : row->findChildren<QWidget*>()) {
      if (w->objectName() == QStringLiteral("shimmerOverlay")) continue;   // rides its target
      const QRect g(w->mapTo(row, QPoint(0, 0)), w->size());
      // ±1: a widget whose own box is an even height inside an odd slot rounds one way
      // (the checkbox). Anything further is a real drift off the line.
      if (centre < 0) centre = g.center().y();
      else if (qAbs(g.center().y() - centre) > 1) aligned = false;
    }
    check(aligned, "every item in the row rides one centre line");
  }
  for (QPushButton* b : row->findChildren<QPushButton*>())
    check(b->size() == QSize(31, 25), "each row action is the browser's 31x25 button");
  // The row shimmers as one card on hover (browser .connect-row:hover::after), and so
  // does every control in it — installed per row, since rows are rebuilt on every change
  // and the dialog-wide pass only ever saw the batch that existed at open.
  {
    bool cardSweep = false;
    for (QWidget* w : row->findChildren<QWidget*>(QStringLiteral("shimmerOverlay")))
      if (w->parentWidget() == row && w->size() == row->size()) cardSweep = true;
    check(cardSweep, "the whole row carries a hover shimmer of its own");
  }
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
  check(list->styleSheet().contains(QStringLiteral("border-radius:8px")),
        "…styled by the list's own cascading row sheet, at the browser's radius");

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
  if (disc) check(disc->toolTip() == QStringLiteral("Disconnect (and forget) this server"),
                  "…which says what it does, in the browser's own words");
  auto* dismiss = new QTimer;  // answers the styled confirm that blocks disc->click()
  dismiss->setInterval(20);
  QObject::connect(dismiss, &QTimer::timeout, [dismiss] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m || m->objectName() != QLatin1String("stencilConfirmModal")) return;
    for (QPushButton* b : m->findChildren<QPushButton*>())
      if (b->text() == QLatin1String("OK")) {
        b->click();
        dismiss->stop();
        dismiss->deleteLater();
        return;
      }
  });
  dismiss->start();
  if (disc) disc->click();
  check(!mgr.urls().contains(longUrl), "model drops the url immediately");
  check(list->count() == 1, "the row's slot is held while the dust plays");
  check(list->itemWidget(list->item(0)) == nullptr, "retired row is blanked at once");
  check(list->item(0)->text().isEmpty(), "no premature empty-state during removal");
  check(dlg.findChild<QWidget*>(DisintegrateOverlay::OBJECT_NAME) != nullptr,
        "removal dust overlay is playing");
  pumpFor(DisintegrateOverlay::DUST_MS / 3);
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
    pumpUntil([&] { return mgr.urls().contains(shortUrl); });   // the connect is async now
    check(mgr.urls().contains(shortUrl), "connects the second server");
    pumpUntil([&] {
      QWidget* w = list->count() == 1 ? list->itemWidget(list->item(0)) : nullptr;
      return w && !w->isVisible();
    }, 1000);
    QWidget* fresh = list->count() == 1 ? list->itemWidget(list->item(0)) : nullptr;
    check(fresh && !fresh->isVisible(), "new row starts hidden under the gather motes");
    check(dlg.findChild<QWidget*>(DisintegrateOverlay::OBJECT_NAME) != nullptr,
          "gather overlay is playing");
    pumpUntil([&] {
      QWidget* w = list->count() == 1 ? list->itemWidget(list->item(0)) : nullptr;
      return w && w->isVisible();
    });
    fresh = list->count() == 1 ? list->itemWidget(list->item(0)) : nullptr;
    check(fresh && fresh->isVisible(), "row materializes once the gather completes");
  }

  // ── Compact POPOVER shape (mainWindow execMaybePopover): the dialog-level minimumWidth
  // is dropped and the box shrinks to sizeHint. The row's fixed controls used to eat that
  // budget, leaving the URL ~70px; the list's own minimum keeps the mini form wide enough.
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
        if (l->toolTip().endsWith(shortUrl)) murl = l;   // "<state> — <url>"
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
      stencil::test::connectNow(many, QStringLiteral("http://row%1@127.0.0.1:%2").arg(i).arg(port), QString(), e);
    ConnectDialog tall(&many);
    // Shorter than eight rows, so the list has to scroll — but tall enough that a few
    // fit whole below the modal chrome (header pill + footer hint) the dialog now wears.
    tall.resize(560, 540);
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

    // ── The FILTER's own transition, over the same eight rows. What it excludes is gone
    // at once (support/filterFade) — a filtered-out row was never disconnected, so there
    // is no exit to play — and the rows that are LEFT arrive. Deliberately not the
    // disconnect's dust either way: excluded is not forgotten.
    auto* kind = tall.findChild<QComboBox*>(QStringLiteral("connKindFilter"));
    check(kind != nullptr, "the tall list carries the kind filter");
    if (tl && kind && tl->count() == 8) {
      const int fullH = tl->item(0)->data(FILTER_FULL_HEIGHT_ROLE).toInt();
      check(fullH > 0, "rows record the slot height a filter opens");
      // Every one of these is non-admin, so "Admin" empties the whole list.
      kind->setCurrentIndex(kind->findData(QStringLiteral("admin")));
      QWidget* w0 = tl->itemWidget(tl->item(0));
      bool allGone = true;
      for (int i = 0; i < 8; ++i)
        allGone = allGone && tl->item(i)->isHidden() && tl->item(i)->sizeHint().height() == 0;
      check(allGone, "Admin empties a list of non-admin rows at once, every slot closed");
      check(w0 && !w0->property(FILTER_FADE_PROPERTY).toBool(),
            "…owning no fade: a row that is out of the answer has nothing to play");
      check(w0 && w0->graphicsEffect() == nullptr,
            "…and no stale effect either, so the scroll-edge reveal gets it back");
      check(tall.findChild<QWidget*>(DisintegrateOverlay::OBJECT_NAME) == nullptr,
            "…and it spends none of the removal's dust");
      check(tl->count() == 9 && tl->item(8)->text().contains(QStringLiteral("admin credential")),
            "…and the explanatory line sits after them");

      // Settled = visible, full slot, and the fade has handed the widget back (the last
      // pixel of height rounds up a tick before presence actually lands on 1).
      auto settled = [&] {
        for (int i = 0; i < 8; ++i) {
          if (tl->item(i)->isHidden() || tl->item(i)->sizeHint().height() != fullH) return false;
          QWidget* w = tl->itemWidget(tl->item(i));
          if (w && w->property(FILTER_FADE_PROPERTY).toBool()) return false;
        }
        return true;
      };
      kind->setCurrentIndex(kind->findData(QStringLiteral("all")));
      check(!tl->item(0)->isHidden() && tl->item(0)->sizeHint().height() < fullH,
            "…and the rows that are left arrive, still expanding");
      pumpFor(FILTER_FADE_MS / 4);
      check(w0 && w0->property(FILTER_FADE_PROPERTY).toBool(),
            "an arriving row owns its own fade while it comes in");
      check(w0 && dynamic_cast<QGraphicsOpacityEffect*>(w0->graphicsEffect()) != nullptr,
            "…a plain opacity fade, not the scroll edge's grain");
      check(w0 && dynamic_cast<DissolveEffect*>(w0->graphicsEffect()) == nullptr,
            "…so the two motions never fight over one effect");
      pumpUntil(settled);
      check(settled(), "every row returns to its full slot");
      check(w0 && !w0->property(FILTER_FADE_PROPERTY).toBool() &&
                dynamic_cast<QGraphicsOpacityEffect*>(w0->graphicsEffect()) == nullptr,
            "…handing its opacity back to the scroll-edge reveal");

      // Rapid changes: whatever is mid-flight, the LAST pick decides the visible set.
      for (const char* mode : {"admin", "nonadmin", "admin", "all"}) {
        kind->setCurrentIndex(kind->findData(QString::fromLatin1(mode)));
        pumpFor(FILTER_FADE_MS / 6);   // each flip interrupts the one before it
      }
      pumpUntil(settled);
      check(settled(), "no row is stuck hidden or part-collapsed after a rapid sequence");
      check(tl->count() == 8, "…and no stale explanatory line is left behind");

      // Reduced motion: the same result, reached with no transition at all.
      qputenv("STENCIL_NO_ANIM", "1");
      kind->setCurrentIndex(kind->findData(QStringLiteral("admin")));
      bool instantGone = true;
      for (int i = 0; i < 8; ++i)
        instantGone = instantGone && tl->item(i)->isHidden() && tl->item(i)->sizeHint().height() == 0;
      check(instantGone, "reduced motion filters straight to the end state");
      kind->setCurrentIndex(kind->findData(QStringLiteral("all")));
      check(settled(), "…and restores every row the same way");
      qunsetenv("STENCIL_NO_ANIM");
    }
  }

  // ── Return in the URL field connects ONCE ────────────────────────────────────
  // A QLineEdit emits returnPressed and then lets the key reach the dialog's DEFAULT
  // button, so wiring both fired doConnect twice: the first call connected and cleared
  // the field, the second found it empty and toasted "Enter a server URL" over the
  // connection that had just been made.
  {
    ConnectionManager fresh;
    ConnectDialog d(&fresh);
    d.resize(480, 420);
    d.show();
    pumpFor(50);
    QLineEdit* urlField = nullptr;
    for (QLineEdit* e : d.findChildren<QLineEdit*>())
      if (e->placeholderText() == QLatin1String("http://localhost:8090")) urlField = e;
    check(urlField != nullptr, "finds the URL field");
    if (urlField) {
      QStringList toasts;
      QObject::connect(&d, &ConnectDialog::toast, &d,
                       [&toasts](const QString& text, bool) { toasts << text; });
      urlField->setFocus();
      urlField->setText(QStringLiteral("http://127.0.0.1:%1").arg(port));
      QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
      QCoreApplication::sendEvent(urlField, &enter);
      pumpUntil([&fresh] { return !fresh.urls().isEmpty(); });
      pumpFor(80);   // …and let any second, spurious attempt land too
      check(fresh.urls().size() == 1, "Return added the connection");
      check(urlField->text().isEmpty(), "…and cleared the field it came from");
      check(!toasts.contains(QStringLiteral("Enter a server URL")),
            "…without complaining that the URL is missing over the connection it just made");
      check(toasts.isEmpty(), "one Return, one attempt, nothing to report");
      // The field's own filter is what carries it — not the default button, whose flag
      // Qt's autoDefault juggling takes away the moment another button here holds focus.
      check(fresh.urls().first().contains(QStringLiteral("127.0.0.1")),
            "…and it connected to what was typed");

      // …and the token field, which lost its own returnPressed with the URL field's:
      // the default button is what carries Return from anywhere in the dialog.
      QLineEdit* tokenField = nullptr;
      for (QLineEdit* e : d.findChildren<QLineEdit*>())
        if (e->placeholderText() == QLatin1String("(optional)")) tokenField = e;
      check(tokenField != nullptr, "finds the token field");
      if (tokenField) {
        urlField->setText(QStringLiteral("http://u2@127.0.0.1:%1").arg(port));
        tokenField->setFocus();
        QKeyEvent enter2(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        QCoreApplication::sendEvent(tokenField, &enter2);
        pumpUntil([&fresh] { return fresh.urls().size() == 2; });
        pumpFor(80);
        check(fresh.urls().size() == 2, "Return from the token field connects as well");
        check(toasts.isEmpty(), "…and still has nothing to complain about");
      }
    }
  }

  // ── A refused credential clears the fields; an unreachable host does not ─────
  // The refused one still leaves a row (Status::EXPIRED, URL intact, Reconnect on it), so
  // the fields that put it there are done — leaving them typed in invited adding the same
  // server twice. Nothing left behind keeps its text, to be corrected.
  {
    ConnectionManager fresh;
    ConnectDialog d(&fresh);
    d.resize(480, 420);
    d.show();
    pumpFor(50);
    QLineEdit* urlField = nullptr;
    QLineEdit* tokenField = nullptr;
    for (QLineEdit* e : d.findChildren<QLineEdit*>()) {
      if (e->placeholderText() == QLatin1String("http://localhost:8090")) urlField = e;
      if (e->placeholderText() == QLatin1String("(optional)")) tokenField = e;
    }
    check(urlField != nullptr && tokenField != nullptr, "finds both fields");
    if (urlField && tokenField) {
      // Nothing is listening on this port, so the attempt leaves no client behind.
      QTcpServer probe;
      probe.listen(QHostAddress::LocalHost, 0);
      const quint16 dead = probe.serverPort();
      probe.close();
      urlField->setText(QStringLiteral("http://127.0.0.1:%1").arg(dead));
      tokenField->setText(QStringLiteral("tok"));
      QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
      QCoreApplication::sendEvent(&d, &ret);
      pumpFor(400);
      // The rule, whichever way this surface treats an unreachable host (it keeps a
      // client for one, so a row DOES appear and the fields do clear — the browser drops
      // it instead and keeps the text): the fields clear exactly when the URL ended up
      // in the list, never on a "connection added but the text still sitting there".
      const bool leftRow = fresh.find(QStringLiteral("http://127.0.0.1:%1").arg(dead)) != nullptr;
      check(urlField->text().isEmpty() == leftRow,
            "the fields clear exactly when the attempt left a row behind");
      // …and one that DID leave a row clears both (the mock server always answers).
      urlField->setText(QStringLiteral("http://127.0.0.1:%1").arg(port));
      tokenField->setText(QStringLiteral("tok"));
      QCoreApplication::sendEvent(&d, &ret);
      pumpUntil([&fresh] { return !fresh.urls().isEmpty(); });
      pumpFor(80);
      check(urlField->text().isEmpty(), "a server that is now in the list clears the URL field");
      check(tokenField->text().isEmpty(), "…and the token field");
    }
  }

  // ── Return connects even with NO focus widget ────────────────────────────────
  // Removing a row destroys the trash button that had the focus, leaving the dialog with
  // none at all — and a Return handler that lives on the fields never sees the key then
  // (after removing a connection, Enter stopped adding one).
  {
    ConnectionManager fresh;
    ConnectDialog d(&fresh);
    d.resize(480, 420);
    d.show();
    pumpFor(50);
    QLineEdit* urlField = nullptr;
    for (QLineEdit* e : d.findChildren<QLineEdit*>())
      if (e->placeholderText() == QLatin1String("http://localhost:8090")) urlField = e;
    check(urlField != nullptr, "finds the URL field");
    if (urlField) {
      urlField->setText(QStringLiteral("http://127.0.0.1:%1").arg(port));
      if (QWidget* f = QApplication::focusWidget()) f->clearFocus();
      check(QApplication::focusWidget() == nullptr, "…with the focus genuinely nowhere");
      QStringList toasts;
      QObject::connect(&d, &ConnectDialog::toast, &d,
                       [&toasts](const QString& text, bool) { toasts << text; });
      QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
      QCoreApplication::sendEvent(&d, &ret);   // where the platform sends it with no focus
      pumpUntil([&fresh] { return !fresh.urls().isEmpty(); });
      pumpFor(60);
      check(fresh.urls().size() == 1, "Return still adds the connection");
      check(toasts.isEmpty(), "…once, with nothing to report");
    }
  }

  // ── The row's Reconnect toast NAMES the server ───────────────────────────────
  // A bare "Reconnected" never said which one signed back in — with more than one saved
  // connection the toast was useless. Browser twin:
  // connectModal.js `Reconnected to ${url}`.
  {
    ConnectionManager fresh;
    QString rerr;
    const QString url = QStringLiteral("http://127.0.0.1:%1").arg(port);
    check(stencil::test::connectNow(fresh, url, QString(), rerr), "connects the server to reconnect");
    ConnectDialog d(&fresh);
    d.resize(480, 420);
    d.show();
    pumpFor(60);
    QStringList toasts;
    QObject::connect(&d, &ConnectDialog::toast, &d,
                     [&toasts](const QString& text, bool) { toasts << text; });
    auto* recon = d.findChild<QPushButton*>(QStringLiteral("rowReconnect"));
    check(recon != nullptr, "finds the row's reconnect button");
    if (recon) {
      recon->click();
      pumpUntil([&toasts] { return !toasts.isEmpty(); });
      check(toasts.size() == 1 && toasts.first() == QStringLiteral("Reconnected to %1").arg(fresh.urls().first()),
            "the toast says WHICH server reconnected");
    }
  }

  // ── FlowLayout's own hint is what it needs ──────────────────────────────────
  // The bar hands `actions` exactly its sizeHint, so a hint one pixel under what the
  // layout really needs makes it wrap EVERY item onto its own line — the connections
  // batch bar's three buttons in a column. Two off-by-ones
  // caused it, in the same direction: `QSize size;` is (-1,-1), so the width sum started
  // short, and the wrap test read `x + w > right()` when an item that ENDS on right()
  // still fits. Pinned together, because either alone still misses by one.
  {
    QWidget host;
    auto* fl = new stencil::gui::FlowLayout(&host, 0, 6, 6);
    fl->setLineSizeHint(true);
    const int widths[] = {114, 108, 111};
    for (int w : widths) {
      auto* b = new QWidget(&host);
      b->setFixedSize(w, 30);
      fl->addWidget(b);
    }
    host.show();
    pumpFor(60);
    const int needed = 114 + 108 + 111 + 6 * 2;   // the items plus the gaps between them
    check(fl->sizeHint().width() == needed,
          qPrintable(QStringLiteral("sizeHint is the one-line width (%1), got %2")
                         .arg(needed).arg(fl->sizeHint().width())));
    // …and at exactly that width the layout really does hold one line.
    const auto lines = [&host] {
      int n = 0, lastY = -1;
      for (QWidget* c : host.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (c->y() != lastY) n++;
        lastY = c->y();
      }
      return n;
    };
    host.setFixedWidth(needed);
    host.layout()->activate();
    pumpFor(20);
    check(lines() == 1, "at its own hint the row holds one line");
    host.setFixedWidth(needed - 1);
    host.layout()->activate();
    pumpFor(20);
    check(lines() > 1, "…and one pixel under it wraps, so the hint is not generous either");
  }

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
