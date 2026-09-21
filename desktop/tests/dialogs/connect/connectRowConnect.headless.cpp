// Return connects once, with or without a focus widget, and a refusal clears the fields.
#include "connectRowParts.hpp"

namespace connectrow {

  void checkConnectGestures(QTcpServer& server, quint16 port, QListWidget* list, QWidget* row) {
  // Return in the URL field connects ONCE: a QLineEdit emits returnPressed and then lets the key reach
  // the dialog's DEFAULT button, so wiring both fired doConnect twice.
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

  // A refused credential clears the fields; an unreachable host does not. The refused one still leaves
  // a row (Status::EXPIRED, URL intact, Reconnect on it), so the fields that put it there are done.
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
      // The rule, whichever way this surface treats an unreachable host: the fields clear exactly when the
      // URL ended up in the list, never on a "connection added but the text still sitting there".
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

  // Return connects even with NO focus widget: removing a row destroys the trash button that had the
  // focus, and a Return handler living on the fields never sees the key then.
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

  }

}  // namespace connectrow
