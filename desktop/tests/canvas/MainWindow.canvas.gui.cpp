// MainWindow GUI e2e — Loading and clearing an image, the rotate round trip and its undo, drawing
// then undoing, and points pressed past the edge clamping to it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void clearingImageBringsBackTheIdleAffordance() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = win.canvas;
    QVERIFY(canvas);
    const QSize idle = canvas->size();

    QImage big(1600, 2200, QImage::Format_ARGB32);
    big.fill(Qt::darkCyan);
    canvas->loadFromImage(big);
    QTRY_VERIFY(canvas->hasImage());
    QVERIFY2(canvas->height() > win.scroll->viewport()->height(),
             "the fixture must be taller than the viewport, or this proves nothing");

    canvas->clearImage();
    QTRY_VERIFY(!canvas->hasImage());
    QCOMPARE(canvas->size(), idle);
    QVERIFY2(canvas->width() <= win.scroll->viewport()->width()
                 && canvas->height() <= win.scroll->viewport()->height(),
             "the cleared canvas must fit its viewport, or the idle hint is scrolled away");
  }

  void loadsImageAndEnablesActions() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);  // load settles on the event loop
    QVERIFY(canvas->imageWidth() > 0);
    QVERIFY(canvas->imageHeight() > 0);

    QAction* rotate = actionByText(&win, "Rotate Right");
    QVERIFY(rotate);
    QVERIFY(rotate->isEnabled());            // an image makes the transform actions live
    QVERIFY(!win.windowTitle().isEmpty());
  }

  void rotateActionsRoundTrip() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    const int w0 = canvas->imageWidth(), h0 = canvas->imageHeight();
    const int r0 = canvas->getRotationQuarters();
    // The page crop of our landscape test image is non-square, so a quarter turn
    // produces an observable W↔H swap below (guards the swap assertion's premise).
    QVERIFY2(w0 != h0, "the page crop should be non-square so the rotation swap is observable");

    QAction* right = actionByText(&win, "Rotate Right");
    QAction* left = actionByText(&win, "Rotate Left");
    QVERIFY(right && left);

    beat();
    right->trigger();
    QCOMPARE(canvas->getRotationQuarters(), (r0 + 1) % 4);
    // A quarter turn swaps the visible (cropped) dimensions — proof the rotation
    // actually transformed the image, not merely bumped the quarter-turn counter.
    QCOMPARE(canvas->imageWidth(), h0);
    QCOMPARE(canvas->imageHeight(), w0);
    beat();

    left->trigger();                                       // undo the quarter turn
    QCOMPARE(canvas->getRotationQuarters(), r0);
    beat();
    QCOMPARE(canvas->imageWidth(), w0);                    // exact state restored
    QCOMPARE(canvas->imageHeight(), h0);
  }

  // A quarter turn is an undo step (the browser's editorMemento): Undo turns the view back, Redo
  // turns it again, through the real actions.
  void rotateThenUndoRedo() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const int w0 = canvas->imageWidth(), h0 = canvas->imageHeight();
    const int r0 = canvas->getRotationQuarters();

    QAction* right = actionByText(&win, "Rotate Right");
    QAction* undo = actionByText(&win, "Undo");
    QAction* redo = actionByText(&win, "Redo");
    QVERIFY(right && undo && redo);
    right->trigger();
    QCOMPARE(canvas->getRotationQuarters(), (r0 + 1) % 4);
    QVERIFY(canvas->canUndo() && undo->isEnabled());
    beat();

    undo->trigger();
    QCOMPARE(canvas->getRotationQuarters(), r0);
    QCOMPARE(canvas->imageWidth(), w0);
    QCOMPARE(canvas->imageHeight(), h0);
    QVERIFY(redo->isEnabled());
    beat();

    redo->trigger();
    QCOMPARE(canvas->getRotationQuarters(), (r0 + 1) % 4);
    QCOMPARE(canvas->imageWidth(), h0);
    beat();
  }

  void drawWithMouseThenUndo() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Enter drawing mode via the real "Start Drawing" action, then click three
    // well-separated points on the canvas — the same left-click path the app uses.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start && start->isEnabled());
    start->trigger();

    const int W = canvas->width(), H = canvas->height();
    for (const QPoint& p : { QPoint(W * 0.35, H * 0.35), QPoint(W * 0.6, H * 0.45), QPoint(W * 0.45, H * 0.65) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    // Each left-click press adds exactly one point: the canvas is fixed to the scaled-image
    // size with a zero-offset widget→image mapping, so all three clicks land inside it.
    QCOMPARE(totalPoints(canvas), 3);

    // Commit the line via the "New Line" action — this is what pushes an undo snapshot.
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(newLine);
    newLine->trigger();
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 1);   // committed line landed
    QVERIFY(canvas->canUndo());
    QVERIFY(!canvas->canRedo());
    beat();

    // The Undo action steps back the history stack, dropping the committed line.
    QAction* undo = actionByText(&win, "Undo");
    QVERIFY(undo);
    undo->trigger();
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 0);
    QVERIFY(canvas->canRedo());          // undo made a redo available
    beat();

    // Redo re-applies it via the real action: the committed line comes back,
    // exactly as the toolbar / Ctrl+Shift+Z would restore it.
    QAction* redo = actionByText(&win, "Redo");
    QVERIFY(redo && redo->isEnabled());
    redo->trigger();
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 1);
    QVERIFY(!canvas->canRedo());
    beat();
  }

  // A press just past the picture's edge lands on it: no -1 px, no point beyond the last one.
  void pointsPastTheEdgeClampToIt() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    actionByText(&win, "Start Drawing")->trigger();
    const int W = canvas->width(), H = canvas->height();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(-3, -4));
    beat();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(W + 5, H + 6));
    beat();
    actionByText(&win, "New Line")->trigger();
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 1);
    const auto& pts = canvas->getLines()[0].points;
    QCOMPARE(static_cast<int>(pts.size()), 2);
    QCOMPARE(pts[0].x, 0.0);
    QCOMPARE(pts[0].y, 0.0);
    QCOMPARE(pts[1].x, static_cast<double>(canvas->imageWidth()));
    QCOMPARE(pts[1].y, static_cast<double>(canvas->imageHeight()));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.canvas.gui.moc"
