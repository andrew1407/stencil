// MainWindow GUI e2e — The f(x,y) fields: their width, the toggle that reveals them, and the idle commit.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include <cmath>

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

  // The desktop's own pixelToPageCoords. The opened picture is album-shaped, so A4 lies on
  // its side (29.7 x 21 cm) and every name below is measured against the raw point it gives.
  void formulaNamesReachThePageTheImageAndTheOtherAxis() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.canvas->hasImage());
    const int hadPage = win.units.pageSize->currentIndex();
    const QString hadUnits = win.settings.units;
    const int a4 = win.units.pageSize->findData(QStringLiteral("A4"));
    QVERIFY(a4 >= 0);
    win.units.pageSize->setCurrentIndex(a4);
    win.settings.allowFormulas = true;
    win.settings.units = QStringLiteral("cm");

    const double imgW = win.canvas->imageWidth();
    const double imgH = win.canvas->imageHeight();
    QVERIFY(imgW > imgH);
    const double rawX = 29.7 / imgW * 120.0;
    const double rawY = 21.0 / imgH * 80.0;

    const auto at = [&win](const char* fx, const char* fy) {
      win.settings.formulaX = QString::fromLatin1(fx);
      win.settings.formulaY = QString::fromLatin1(fy);
      return win.pageCoords(120, 80);
    };
    const auto near = [](double got, double want) { return std::fabs(got - want) < 1e-9; };

    QVERIFY2(near(at("", "").x, rawX) && near(at("", "").y, rawY), "the raw page point");
    QVERIFY2(near(at("9", "").x, 9.0), "a constant-only formula needs no variable");
    QVERIFY2(near(at("x / y", "PAGE_WIDTH - y").x, rawX / rawY), "f(x) reads y");
    QVERIFY2(near(at("x / y", "PAGE_WIDTH - y").y, 29.7 - rawY), "f(y) reads PAGE_WIDTH");
    QVERIFY2(near(at("IMAGE_WIDTH", "IMAGE_HEIGHT").x, imgW), "the image names are pixels");
    QVERIFY2(near(at("IMAGE_WIDTH", "IMAGE_HEIGHT").y, imgH), "…on both axes");
    QVERIFY2(near(at("PAGE_WIDTH_IN", "").x, 29.7 / 2.54), "_IN converts while showing cm");
    win.settings.units = QStringLiteral("in");
    QVERIFY2(near(at("PAGE_WIDTH", "").x, 29.7 / 2.54), "PAGE_WIDTH follows the display unit");
    win.settings.units = QStringLiteral("cm");
    QVERIFY2(near(at("PAGE_WIDTH", "").x, 29.7), "…and reads cm again");
    QVERIFY2(near(at("PAGE_WIDTHS", "").x, rawX), "an unknown name leaves the raw coordinate");
    win.settings.allowFormulas = false;
    QVERIFY2(near(at("9", "").x, rawX), "formulas off leaves it too");

    win.settings.formulaX.clear();
    win.settings.formulaY.clear();
    win.settings.units = hadUnits;
    if (hadPage >= 0) win.units.pageSize->setCurrentIndex(hadPage);
  }

  // IMAGE_WIDTH / IMAGE_HEIGHT are unsupplied until a picture is open, so the field is
  // flagged and nothing is committed — the coordinate keeps its raw value rather than 0.
  void formulaImageNamesNeedAnImageOpen() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1600, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill && win.formulaX);
    if (!pill->isChecked()) pill->setChecked(true);
    QLabel* err = win.findChild<QLabel*>("formulaError");
    QVERIFY(err);
    win.formulaX->clear();
    QVERIFY(awaitTimer(win.formulaCommitTimer, FORMULA_SETTLE_MS));

    QVERIFY(!win.canvas->hasImage());
    win.formulaX->setText(QStringLiteral("IMAGE_WIDTH"));
    QVERIFY(awaitTimer(win.formulaCommitTimer, FORMULA_SETTLE_MS));
    QVERIFY2(err->isVisible(), "an image name with no image open must be flagged");
    QVERIFY2(win.settings.formulaX.isEmpty(), "…and must not commit");

    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.canvas->hasImage());
    win.validateAndApplyFormulas();
    QVERIFY2(!err->isVisible(), "the same name resolves once a picture is open");
    QCOMPARE(win.settings.formulaX, QStringLiteral("IMAGE_WIDTH"));
    win.settings.allowFormulas = true;
    QVERIFY(std::fabs(win.pageCoords(120, 80).x - win.canvas->imageWidth()) < 1e-9);

    win.formulaX->clear();
    QVERIFY(awaitTimer(win.formulaCommitTimer, FORMULA_SETTLE_MS));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeFormula.gui.moc"
