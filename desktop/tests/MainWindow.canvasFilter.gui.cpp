// MainWindow GUI e2e — Filters and clearing: the filter applied, clear-all, and what a blank resets.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void filterActionAppliesToCanvas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    // The context-menu filter options are hosted QRadioButtons (an exclusive QButtonGroup) so
    // picking one keeps the menu open; find them by their "filterValue" property and drive them.
    // The radios live inside QWidgetActions' default widgets (setDefaultWidget reparents them out
    // of the window until a menu shows them), so reach them via the actions, not win's children.
    auto filterRadio = [&](const QString& value) -> QRadioButton* {
      for (QWidgetAction* a : win.findChildren<QWidgetAction*>())
        if (QWidget* dw = a->defaultWidget())
          for (QRadioButton* r : dw->findChildren<QRadioButton*>())
            if (r->property("filterValue").toString() == value) return r;
      return nullptr;
    };

    // Normalize to a known baseline via the real "None" radio: applyImageFilter PERSISTS the
    // chosen mode to settings, so a prior run/test can start this canvas non-"none". Drive it
    // rather than assuming the default (order-safe).
    QRadioButton* none = filterRadio("none");
    QVERIFY(none);
    none->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("none"));

    // Check the SHARED filter path (toggling the radio runs the real applyImageFilter, which also
    // syncs the toolbar combo) and lands the mode on the live canvas.
    QRadioButton* bw = filterRadio("bw");
    QVERIFY(bw && bw->isEnabled());
    bw->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("bw"));      // menu/toolbar wiring reached the canvas
    beat();

    // Switching filters is live and mutually exclusive (one button group).
    QRadioButton* sepia = filterRadio("sepia");
    QVERIFY(sepia);
    sepia->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("sepia"));
    QVERIFY(!bw->isChecked());                            // exclusive group cleared the old mode
    beat();

    none->setChecked(true);   // leave the persisted filter clean for other tests/runs
  }

  void clearAllActionEmptiesCanvas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Draw and commit a line so there is something to clear.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start && start->isEnabled());
    start->trigger();
    const int W = canvas->width(), H = canvas->height();
    for (const QPoint& p : { QPoint(W * 0.4, H * 0.4), QPoint(W * 0.6, H * 0.55) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(newLine);
    newLine->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);

    // The destructive "Clear All Lines" action (canvas context menu + Edit menu
    // reuse it) asks first — the browser's styled confirm (drawingApp.js
    // clearAllLines) — and on Confirm wipes every committed and in-progress point.
    QAction* clear = actionByText(&win, "Clear All Lines");
    QVERIFY(clear && clear->isEnabled());
    dismissModal("OK");
    clear->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    QCOMPARE(totalPoints(canvas), 0);   // nothing committed or in-progress remains
    beat();
  }

  // A filter left over from the previous image/session must not repaint a FRESH
  // blank: "make a red page" under a riding 'bw' filter rendered flat gray
  // (Rec. 709 luma of pure red = 54) with no red anywhere. Creation resets the
  // filter to none; a filter applied AFTER creation still works (test above).
  void blankCreationResetsRidingFilter() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.applyImageFilter("bw");
    win.createBlankImage(QColor("#ff0000"), 400, 300);
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("none"));
    QTRY_VERIFY(win.canvas_->graphicsEffect() == nullptr);
    const QImage shot = win.canvas_->grab().toImage();
    const QColor mid = shot.pixelColor(shot.width() / 2, shot.height() / 2);
    QVERIFY2(mid.red() > 200 && mid.green() < 80 && mid.blue() < 80,
             qPrintable(QStringLiteral("blank is %1, not red").arg(mid.name())));
    beat();
  }

  // The bug this locks down: recoloring a blank project's background regenerates the
  // SAME dimensions in place (applyBlankColor → loadFromImage(img, keepZoom=true)) —
  // there is nothing to refit, so the zoom the user had set must survive it (browser
  // parity: drawingApp.js loadImageFromFile's opts.keepZoom / drawingApp-launch tests).
  void recoloringABlankKeepsTheZoom() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.createBlankImage(QColor("#ffffff"), 400, 300);
    win.canvas_->setScale(2.5);
    QCOMPARE(win.canvas_->scale(), 2.5);
    win.applyBlankColor(QColor("#0000ff"));
    QCOMPARE(win.canvas_->scale(), 2.5);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.canvasFilter.gui.moc"
