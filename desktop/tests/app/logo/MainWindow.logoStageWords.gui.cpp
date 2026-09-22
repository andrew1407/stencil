// MainWindow GUI e2e — The logo stage's typed words: a show's name typed into the bare window
// opens it, under the layout the keys are labelled in or any other. Shared ground is in
// MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "LogoStage.hpp"
#include "typedLetter.hpp"

using stencil::gui::LogoStage;
namespace support = stencil::support;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static LogoStage* stageOf(MainWindow& win) { return win.findChild<LogoStage*>("logoStage"); }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Typing a show's name into the bare window opens it, as it does in the browser.
  void typingAShowsNameOpensIt() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.accentColor = "violet";
    LogoStage* stage = stageOf(win);
    QVERIFY(stage);
    qWarning("focus at rest: %s", QApplication::focusWidget()
             ? QApplication::focusWidget()->metaObject()->className() : "none");
    // Through Qt's own delivery — shortcut map, focus widget and all — not straight to the window.
    const auto type = [&win](const QString& word) {
      QWidget* to = QApplication::focusWidget() ? QApplication::focusWidget() : &win;
      QTest::keyClicks(to, word);
    };
    type(QStringLiteral("neonon"));
    QVERIFY2(stage->isOpen(), "the typed word opens its show");
    stage->dismiss();
    QTest::qWait(500);   // the hide plays out, and the stage lets go of the keyboard

    // …and the NEXT word is heard just as well: one show must not deafen the window.
    type(QStringLiteral("makesomesunshine"));
    QVERIFY2(stage->isOpen(), "a second typed word opens its show too");
    QCOMPARE(stage->showName(), QString("makeSomeSunshine"));
    stage->dismiss();
  }

  // Under a Cyrillic layout the keys spell Cyrillic, so the physical key's US letter is heard.
  void aWordTypedUnderAnotherLayoutOpensItsShowToo() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    LogoStage* stage = stageOf(win);
    QVERIFY(stage);
    const QString latin = QStringLiteral("neonon");
    const QString russian = QString::fromUtf8("тущтщт");   // the same keys on the Russian layout
    for (int i = 0; i < latin.size(); ++i) {
      QWidget* to = QApplication::focusWidget() ? QApplication::focusWidget() : &win;
      const quint32 code = support::nativeCodeOfLetter(support::hostKeyPlatform(), latin.at(i));
      QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, code, code, 0,
                      QString(russian.at(i)));
      QApplication::sendEvent(to, &press);
    }
    QVERIFY2(stage->isOpen(), "the word typed under another layout opens its show");
    QCOMPARE(stage->showName(), QString("neonOn"));
    stage->dismiss();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.logoStageWords.gui.moc"
