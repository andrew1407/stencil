// A row arriving and leaving: retire-then-finalize, and the compact popover's shape.
#include "connectRowParts.hpp"

namespace connectrow {

  void checkRowMotion(QTcpServer& server, const QString& longUrl, const QString& shortUrl, ConnectionManager& mgr, ConnectDialog& dlg, QListWidget* list, QWidget* row) {
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

  // Compact POPOVER shape (execMaybePopover): the dialog-level minimumWidth is dropped and the box
  // shrinks to sizeHint, so the list's own minimum is what keeps the mini form's URL field wide.
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

  }

}  // namespace connectrow
