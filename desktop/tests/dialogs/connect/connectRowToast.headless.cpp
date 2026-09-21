// The Reconnect toast names its server, and FlowLayout's own hint is what it needs.
#include "connectRowParts.hpp"

namespace connectrow {

  void checkToastAndHint(QTcpServer& server, quint16 port, QWidget* row) {
  // The row's Reconnect toast NAMES the server: with more than one saved connection a bare
  // "Reconnected" says nothing. Browser twin: modal.js `Reconnected to ${url}`.
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

  // FlowLayout's own hint must be what it needs: the bar hands `actions` exactly its sizeHint, so a hint
  // one pixel short wraps every item onto its own line (`QSize size;` is (-1,-1); an item ON right() fits).
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


  }

}  // namespace connectrow
