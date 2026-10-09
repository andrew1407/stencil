// MainWindow GUI e2e — the bubble's role style: the #chatCard* rule reaches a card named after
// its entrance laid it out, and the painted tail follows a theme switch with the bubble's fill.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The #chatCardUser rule reaches the card: its fill is not the transcript's backdrop and its
  // top corner is rounded. A card styled before it was named renders as a bare square frame.
  void chatBubbleTakesItsRoleStyle() {
    const auto still = withoutMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.acts.chat->setChecked(true);
    openTranscript(win);
    win.chatDock->appendUser(QStringLiteral("Frame the S"), {});
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    QTRY_VERIFY(card->width() > 40);
    const QImage shot = win.chatDock->grab().toImage();
    const QPoint origin = card->mapTo(win.chatDock, QPoint(0, 0));
    const QColor backdrop = shot.pixelColor(origin.x() - 4, origin.y() + card->height() / 2);
    const QColor fill = shot.pixelColor(origin.x() + card->width() / 2, origin.y() + 3);
    const QColor corner = shot.pixelColor(origin.x() + 1, origin.y() + 1);
    QVERIFY2(fill != backdrop, qPrintable(QStringLiteral("user bubble fill %1 is the backdrop")
                                              .arg(fill.name())));
    QVERIFY2(corner == backdrop, qPrintable(QStringLiteral("user bubble corner %1 is square (backdrop %2)")
                                                .arg(corner.name(), backdrop.name())));
    beat();
  }

  // The tail is painted, not styled: after a light → dark switch its fill is the bubble's new fill.
  void chatBubbleTailFollowsTheTheme() {
    const auto still = withoutMotion();
    MainWindow win(nullptr, false);
    win.settings.themeMode = QStringLiteral("light");
    win.applyTheme();
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.acts.chat->setChecked(true);
    openTranscript(win);
    win.chatDock->appendUser(QStringLiteral("Frame the S"), {});
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    QTRY_VERIFY(card->width() > 40);
    win.settings.themeMode = QStringLiteral("dark");
    win.applyTheme();
    auto* tail = qobject_cast<QWidget*>(card->property("chatTail").value<QObject*>());
    QVERIFY(tail);
    QTRY_VERIFY(tail->isVisible());
    // One grab of the dock: the card's translucent fill is only its true colour over the backdrop.
    const QImage shot = win.chatDock->grab().toImage();
    const QPoint at = card->mapTo(win.chatDock, QPoint(card->width() / 2, 3));
    const QColor fill = shot.pixelColor(at), backdrop = shot.pixelColor(at.x(), at.y() - 6);
    const QRect r(tail->mapTo(win.chatDock, QPoint(0, 0)), tail->size());
    QHash<QRgb, int> counts;
    for (int y = r.top(); y <= r.bottom(); ++y)
      for (int x = r.left(); x <= r.right(); ++x)
        if (shot.pixelColor(x, y) != backdrop) ++counts[shot.pixel(x, y)];
    QRgb most = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
      if (!most || it.value() > counts.value(most)) most = it.key();
    QVERIFY2(QColor(most) == fill, qPrintable(QStringLiteral("tail fill %1 != bubble fill %2")
                                                  .arg(QColor(most).name(), fill.name())));
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatCardsStyle.gui.moc"
