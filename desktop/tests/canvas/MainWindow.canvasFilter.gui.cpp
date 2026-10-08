// MainWindow GUI e2e — Filters and clearing: the filter applied, clear-all, and what a blank resets.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void filterActionAppliesToCanvas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    // The menu's filter options are hosted QRadioButtons in an exclusive QButtonGroup, so
    // picking one keeps the menu open; reach them through the QWidgetActions, by "filterValue".
    auto filterRadio = [&](const QString& value) -> QRadioButton* {
      for (QWidgetAction* a : win.findChildren<QWidgetAction*>())
        if (QWidget* dw = a->defaultWidget())
          for (QRadioButton* r : dw->findChildren<QRadioButton*>())
            if (r->property("filterValue").toString() == value) return r;
      return nullptr;
    };

    // Normalize via the real "None" radio: applyImageFilter PERSISTS the chosen mode to
    // settings, so a prior run can start this canvas non-"none" (order-safe).
    QRadioButton* none = filterRadio("none");
    QVERIFY(none);
    none->setChecked(true);
    QCOMPARE(canvas->getImageFilter(), QString("none"));

    // Check the SHARED filter path (toggling the radio runs the real applyImageFilter, which also
    // syncs the toolbar combo) and lands the mode on the live canvas.
    QRadioButton* bw = filterRadio("bw");
    QVERIFY(bw && bw->isEnabled());
    bw->setChecked(true);
    QCOMPARE(canvas->getImageFilter(), QString("bw"));      // menu/toolbar wiring reached the canvas
    beat();

    // Switching filters is live and mutually exclusive (one button group).
    QRadioButton* sepia = filterRadio("sepia");
    QVERIFY(sepia);
    sepia->setChecked(true);
    QCOMPARE(canvas->getImageFilter(), QString("sepia"));
    QVERIFY(!bw->isChecked());                            // exclusive group cleared the old mode
    beat();

    none->setChecked(true);   // leave the persisted filter clean for other tests/runs
  }

  // A filter pick is one step on the user's own undo stack; undo and redo put the mode and the
  // tint back through the pick's path (toolbar, settings, render) and push nothing themselves.
  void filterPickIsOneUndoStep() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const QString tint0 = win.tools.filterColorValue.name();
    auto shown = [&win] { return win.tools.imageFilter->currentData().toString(); };
    QCOMPARE(win.settings.imageFilter, QStringLiteral("none"));   // a launch never carries one
    QVERIFY(!canvas->canUndo());

    win.tools.imageFilter->setCurrentIndex(win.tools.imageFilter->findData(QStringLiteral("sepia")));
    QCOMPARE(canvas->getImageFilter(), QStringLiteral("sepia"));
    QVERIFY(canvas->canUndo() && win.acts.undo->isEnabled());
    win.applyImageFilter(QStringLiteral("sepia"));   // the same pick again
    win.acts.undo->trigger();
    QCOMPARE(win.settings.imageFilter, QStringLiteral("none"));
    QCOMPARE(shown(), QStringLiteral("none"));
    QCOMPARE(canvas->getImageFilter(), QStringLiteral("none"));
    QVERIFY2(!canvas->canUndo() && canvas->canRedo(), "one pick, one step; the no-op pushed none");
    win.acts.redo->trigger();
    QCOMPARE(shown(), QStringLiteral("sepia"));
    QVERIFY(!canvas->canRedo());

    win.applyImageFilter(QStringLiteral("custom"));
    win.applyTintColor(QColor("#cc2200"));   // the picker's accepted colour
    win.acts.undo->trigger();
    QCOMPARE(win.tools.filterColorValue.name(), tint0);
    QCOMPARE(win.settings.filterColor, tint0);
    QCOMPARE(canvas->getFilterColor().name(), tint0);
    QCOMPARE(shown(), QStringLiteral("custom"));
    win.acts.undo->trigger();
    QCOMPARE(shown(), QStringLiteral("sepia"));
    QVERIFY(win.tools.filterColorBtn->isHidden());
    win.applyImageFilter(QStringLiteral("none"));
  }

  // Alt+Shift+B (hotkeysConfig cycleFilterPrev) steps the filter BACK through the real shortcut map,
  // wrapping, as Alt+B steps it forward; each step is one undo step, as a pick is.
  void previousFilterStepsBackWrapping() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(win.acts.cycleFilterPrev);
    QCOMPARE(win.acts.cycleFilterPrev->shortcut(), QKeySequence("Alt+Shift+B"));
    QCOMPARE(win.keys.actions.value(QStringLiteral("cycleFilterPrev")), win.acts.cycleFilterPrev);   // a rebind applies live
    QCOMPARE(win.keys.order.indexOf(QStringLiteral("cycleFilterPrev")),
             win.keys.order.indexOf(QStringLiteral("cycleFilter")) + 1);
    win.applyImageFilter(QStringLiteral("none"));
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&win));   // a WindowShortcut needs the active window
    // The chord's Alt press over a toolbar icon would peek its window; over the canvas it peeks nothing.
    QCursor::setPos(win.scroll->viewport()->mapToGlobal(win.scroll->viewport()->rect().center()));

    const auto chord = [&win](Qt::KeyboardModifiers mods) {
      QTest::keyClick(win.windowHandle(), Qt::Key_B, mods);
      return win.settings.imageFilter;
    };
    const Qt::KeyboardModifiers back = Qt::AltModifier | Qt::ShiftModifier;
    QCOMPARE(chord(back), QStringLiteral("custom"));
    QCOMPARE(canvas->getImageFilter(), QStringLiteral("custom"));
    QCOMPARE(chord(back), QStringLiteral("contour"));
    QCOMPARE(chord(Qt::AltModifier), QStringLiteral("custom"));
    QCOMPARE(chord(Qt::AltModifier), QStringLiteral("none"));
    QCOMPARE(chord(back), QStringLiteral("custom"));
    win.acts.undo->trigger();
    QCOMPARE(win.settings.imageFilter, QStringLiteral("none"));
    win.acts.undo->trigger();
    QCOMPARE(win.settings.imageFilter, QStringLiteral("custom"));
    win.applyImageFilter(QStringLiteral("none"));
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
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 1);

    // "Clear All Lines" (canvas context menu + Edit menu) asks first — the browser's styled
    // confirm (drawingApp.js clearAllLines) — and on Confirm wipes committed and in-progress.
    QAction* clear = actionByText(&win, "Clear All Lines");
    QVERIFY(clear && clear->isEnabled());
    dismissModal("OK");
    clear->trigger();
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 0);
    QCOMPARE(totalPoints(canvas), 0);   // nothing committed or in-progress remains
    beat();
  }

  // A filter left over from the previous image must not repaint a FRESH blank; creation resets
  // the filter to none, and a filter applied AFTER creation still works (test above).
  void blankCreationResetsRidingFilter() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.applyImageFilter("bw");
    win.parts.sourceOpener.createBlankImage(QColor("#ff0000"), 400, 300);
    QCOMPARE(win.settings.imageFilter, QStringLiteral("none"));
    QTRY_VERIFY(win.canvas->graphicsEffect() == nullptr);
    const QImage shot = win.canvas->grab().toImage();
    const QColor mid = shot.pixelColor(shot.width() / 2, shot.height() / 2);
    QVERIFY2(mid.red() > 200 && mid.green() < 80 && mid.blue() < 80,
             qPrintable(QStringLiteral("blank is %1, not red").arg(mid.name())));
    beat();
  }

  // Recoloring a blank regenerates the SAME dimensions in place (applyBlankColor →
  // loadFromImage(keepZoom=true)), so the zoom the user set must survive.
  void recoloringABlankKeepsTheZoom() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.parts.sourceOpener.createBlankImage(QColor("#ffffff"), 400, 300);
    win.canvas->setScale(2.5);
    QCOMPARE(win.canvas->getScale(), 2.5);
    win.parts.projects.applyBlankColor(QColor("#0000ff"));
    QCOMPARE(win.canvas->getScale(), 2.5);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.canvasFilter.gui.moc"
