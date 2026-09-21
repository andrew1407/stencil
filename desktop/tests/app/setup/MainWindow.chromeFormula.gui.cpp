// MainWindow GUI e2e — The f(x,y) fields: their width, the toggle that reveals them, and the idle commit.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

// Comfortably past the f(x,y) idle-commit delay (MainWindow.cpp FORMULA_COMMIT_MS), so a
// "stopped typing" wait can't race the timer on a loaded machine.
constexpr int FORMULA_SETTLE_MS = 1600;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The f(x,y) pair is as wide as the browser's (#formula-x / #formula-y, 180px inline) and keeps
  // that width on a narrow window, where the tool run WRAPS (support/WrapRow.hpp) instead.
  void formulaFieldsTakeTheBrowsersWidth() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill && win.formulaX && win.formulaY);
    if (!pill->isChecked()) pill->setChecked(true);
    QTRY_VERIFY(win.formulaX->isVisible());
    // The reveal is ANIMATED, so every width here is settled with a QTRY — a plain
    // compare beside the first one reads the pair mid-slide on a slow machine.
    QTRY_COMPARE(win.formulaX->width(), 180);
    QTRY_COMPARE(win.formulaY->width(), 180);
    // …side by side on the browser's 6px gap, not spread out over the row's leftover width.
    QTRY_COMPARE(win.formulaY->x() - (win.formulaX->x() + win.formulaX->width()), 6);
    // A row with no slack wraps around them: the pair keeps the browser's width and stays
    // on the toolbar, down to the narrowest window the layout allows.
    for (const int w : {1100, 920, 850}) {
      win.resize(w, 800);
      QTRY_COMPARE(win.formulaX->width(), 180);
      QCOMPARE(win.formulaY->width(), 180);
      QVERIFY2(win.formulaX->isVisible(),
               qPrintable(QString("the formula fields hid at %1px").arg(w)));
    }
  }

  void formulaToggleRevealsInputs() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill);
    QLineEdit* fx = nullptr;
    for (auto* e : win.findChildren<QLineEdit*>())
      if (e->placeholderText().startsWith("x(x)")) fx = e;
    QVERIFY(fx);
    // The pill starts wherever the LAST run left it (settings.json persists allowFormulas) and its
    // reveal/hide is animated, so every wait here is a QTRY.
    if (pill->isChecked()) pill->setChecked(false);
    QTRY_VERIFY(!fx->isVisible());
    QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, pill->rect().center());
    QTRY_VERIFY2(fx->isVisible(), "formula inputs should appear when f(x,y) is enabled");
    win.openPathFromOS(guiTestImage());
    QTest::qWait(120);
    QVERIFY2(fx->isVisible(), "formula inputs should survive an image load");
    win.resize(720, 800); settleLayout(&win, 80);
    win.resize(1200, 800); settleLayout(&win, 80);
    QVERIFY2(fx->isVisible(), "formula inputs should survive window resizes");
  }

  // The f(x,y) pair commits when typing SETTLES, not per keystroke: an expression still being
  // written is neither judged nor applied. Mirrors browser settingsController.wireFormulaInputs.
  void formulaCommitsOnIdlePauseNotPerKeystroke() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    // Wide enough that the inline formula inputs stay on the row rather than folding
    // into the toolbar's ≫ extension — this test is about the debounce, not the layout.
    win.resize(1600, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill);
    if (!pill->isChecked()) {
      QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, pill->rect().center());
      QTest::qWait(60);
    }
    QLineEdit* fx = nullptr;
    for (auto* e : win.findChildren<QLineEdit*>())
      if (e->placeholderText().startsWith("x(x)")) fx = e;
    QVERIFY(fx);
    // By object name, not by its text: the label is icon-only (the words live on its
    // tooltip, as in the browser), so matching on "invalid" found nothing.
    QLabel* err = win.findChild<QLabel*>("formulaError");
    QVERIFY(err);
    // A window restores the persisted formulas, so start from a known-empty field and let
    // that clear settle (leaving no pending commit to race the assertions below).
    fx->clear();
    QVERIFY(awaitTimer(win.formulaCommitTimer, FORMULA_SETTLE_MS));
    QVERIFY(!err->isVisible());

    fx->setFocus();
    QTest::keyClicks(fx, "(x+", Qt::NoModifier, 40);   // half-written, a key at a time
    QVERIFY2(!err->isVisible(), "an expression still being typed must not be flagged");
    QVERIFY2(win.formulaCommitTimer->isActive()
                 && awaitTimer(win.formulaCommitTimer, FORMULA_SETTLE_MS),
             "typing then pausing armed no commit");
    QVERIFY2(err->isVisible(), "a settled, unparseable expression IS flagged");

    QTest::keyClicks(fx, "1)", Qt::NoModifier, 40);    // finish it: valid again
    QCOMPARE(fx->text(), QStringLiteral("(x+1)"));
    QVERIFY2(!err->isVisible(), "fixing the expression clears the error indicator on the spot");
    QVERIFY(awaitTimer(win.formulaCommitTimer, FORMULA_SETTLE_MS));
    QVERIFY(!err->isVisible());

    // Leave the persisted formula as we found it — the settings are shared across tests.
    fx->clear();
    QVERIFY(awaitTimer(win.formulaCommitTimer, FORMULA_SETTLE_MS));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeFormula.gui.moc"
