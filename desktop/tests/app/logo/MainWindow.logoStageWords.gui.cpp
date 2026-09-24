// MainWindow GUI e2e — The logo stage's typed words: a show's name typed into the bare window
// opens it, under the layout the keys are labelled in or any other. Shared ground is in
// MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "LogoStage.hpp"
#include "typedLetter.hpp"

#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QSpinBox>

using stencil::gui::LogoStage;
namespace support = stencil::support;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static LogoStage* stageOf(MainWindow& win) { return win.findChild<LogoStage*>("logoStage"); }
  static void typeAt(MainWindow& win, const QString& word) {
    QTest::keyClicks(QApplication::focusWidget() ? QApplication::focusWidget() : &win, word);
  }

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

  // Stage shows are exclusive: a word typed at a running show swaps it in, its own word does nothing.
  void aWordTypedDuringAShowReplacesIt() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    LogoStage* stage = stageOf(win);
    typeAt(win, QStringLiteral("firework"));
    QCOMPARE(stage->showName(), QString("firework"));
    const int toasts = int(win.findChildren<QWidget*>(QStringLiteral("toast")).size());
    typeAt(win, QStringLiteral("firework"));
    QVERIFY2(stage->isOpen(), "its own word leaves the show up");
    QCOMPARE(int(win.findChildren<QWidget*>(QStringLiteral("toast")).size()), toasts);
    typeAt(win, QStringLiteral("watershow"));
    QVERIFY2(stage->isOpen(), "another word keeps a stage up…");
    QCOMPARE(stage->showName(), QString("waterShow"));
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(QApplication::focusWidget(), &esc);
    QVERIFY2(!stage->isOpen(), "Escape still ends it");
  }

  // A word typed at an open modal, or in fullscreen, clears the way and opens its show.
  void aTypedWordClosesTheModalAndLeavesFullscreen() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    LogoStage* stage = stageOf(win);
    QDialog dlg(&win);
    auto* ok = new QPushButton(QStringLiteral("OK"), &dlg);
    dlg.setModal(true);
    dlg.show();
    dlg.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&dlg));
    ok->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(ok));
    QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(&dlg));
    QTest::keyClicks(ok, QStringLiteral("neonon"));
    QTRY_VERIFY2(stage->isOpen(), "the word closes the modal and opens its show");
    QVERIFY(!dlg.isVisible());
    QCOMPARE(stage->showName(), QString("neonOn"));
    stage->dismiss();
    QTest::qWait(500);

    win.toggleFullscreen();
    QVERIFY(win.fs.active);
    QVERIFY2(!stage->activateByName("firework"), "a bare activation is still refused");
    typeAt(win, QStringLiteral("firework"));
    QTRY_VERIFY2(stage->isOpen(), "the word leaves fullscreen and opens its show");
    QVERIFY(!win.fs.active);
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

  // A spin box is its line edit's focus proxy, so the container is what has the focus: the keys
  // typed into Thickness are its own, not a word.
  void aWordTypedIntoASpinBoxStaysInIt() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    LogoStage* stage = stageOf(win);
    QVERIFY(stage);
    QVERIFY(win.lineThickness);
    win.lineThickness->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(win.lineThickness));
    QTest::keyClicks(win.lineThickness, QStringLiteral("neonon"));
    QVERIFY2(!stage->isOpen(), "a word typed into a spin box opens nothing");

    // …and so is a combo box's: a closed list jumps to the item the letters spell.
    QVERIFY(win.lineStyle);
    win.lineStyle->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(win.lineStyle));
    QTest::keyClicks(win.lineStyle, QStringLiteral("neonon"));
    QVERIFY2(!stage->isOpen(), "a word typed into a combo box opens nothing");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.logoStageWords.gui.moc"
