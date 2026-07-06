// Desktop GUI end-to-end, written with the Qt Test framework (QtTest). Unlike the other
// headless checks — which exercise CanvasWidget / fileStore in isolation — this drives the
// REAL MainWindow: it loads an image through the public OS-open path, triggers the actual
// toolbar/menu QActions (Rotate, Undo, Start Drawing), and sends real mouse clicks to the
// live canvas, asserting on observable widget state. Runs offscreen
// (QT_QPA_PLATFORM=offscreen), so it needs no display; registered with CTest.
#include "mainWindow.hpp"
#include "canvasWidget.hpp"
#include <QtTest>
#include <QAction>
#include <QImage>
#include <QDir>
#include <QApplication>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTimer>
#include <memory>

using stencil::gui::MainWindow;
using stencil::gui::CanvasWidget;

namespace {
  // Find a shared QAction by its visible label (the menu bar, toolbar, and context menu
  // all reuse the same QAction objects, so this reaches the real UI wiring).
  QAction* actionByText(const QWidget* w, const QString& text) {
    for (QAction* a : w->findChildren<QAction*>())
      if (a->text().compare(text, Qt::CaseInsensitive) == 0) return a;
    return nullptr;
  }

  // Total points across all lines (committed + in-progress), for draw/undo assertions.
  int totalPoints(const CanvasWidget* c) {
    int n = 0;
    for (const auto& line : c->allLines()) n += static_cast<int>(line.points.size());
    return n;
  }

  // Optional watch-along pause: set STENCIL_GUI_SLOWMO=<ms> and run headed (no
  // QT_QPA_PLATFORM=offscreen) to actually see each step. No effect in CI (unset → 0).
  void beat() {
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("STENCIL_GUI_SLOWMO", &ok);
    if (ok && ms > 0) QTest::qWait(ms);
  }

  // The quit-confirmation is a modal QMessageBox that blocks the triggering call. Arm this
  // BEFORE triggering Quit: it waits for the box to appear and clicks the button whose label
  // matches (the dialog uses custom "Quit"/"Cancel" buttons, not standard Yes/No roles),
  // letting the otherwise-blocked trigger() return with that answer.
  void dismissQuitDialog(const QString& buttonText) {
    QTimer::singleShot(0, [buttonText]() {
      for (int i = 0; i < 200; ++i) {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
          for (QAbstractButton* b : box->buttons())
            if (QString(b->text()).remove('&').compare(buttonText, Qt::CaseInsensitive) == 0) {
              b->click();
              return;
            }
          return;
        }
        QTest::qWait(5);
      }
    });
  }
}

class MainWindowGuiTest : public QObject {
  Q_OBJECT
  QString png_;

  // Build a shown MainWindow with our test image loaded; returns its live canvas.
  CanvasWidget* openLoaded(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    win.raise();
    win.activateWindow();
    beat();                       // (watch mode) empty editor
    win.openPathFromOS(png_);
    return win.findChild<CanvasWidget*>();
  }

 private slots:
  void initTestCase() {
    // The quit-confirmation test closes its window; keep that from ending the shared
    // QApplication (and the rest of the run) with it.
    qApp->setQuitOnLastWindowClosed(false);
    // A generous album-orientation image (larger than the tiny shared fixture) so the
    // fit-scaled canvas has room for well-separated draw clicks.
    QImage img(240, 160, QImage::Format_RGB32);
    img.fill(Qt::white);
    png_ = QDir::temp().filePath("stencil_gui_e2e_input.png");
    QVERIFY(img.save(png_, "PNG"));
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
    const int r0 = canvas->rotationQuarters();
    // The page crop of our landscape test image is non-square, so a quarter turn
    // produces an observable W↔H swap below (guards the swap assertion's premise).
    QVERIFY2(w0 != h0, "the page crop should be non-square so the rotation swap is observable");

    QAction* right = actionByText(&win, "Rotate Right");
    QAction* left = actionByText(&win, "Rotate Left");
    QVERIFY(right && left);

    beat();
    right->trigger();
    QCOMPARE(canvas->rotationQuarters(), (r0 + 1) % 4);
    // A quarter turn swaps the visible (cropped) dimensions — proof the rotation
    // actually transformed the image, not merely bumped the quarter-turn counter.
    QCOMPARE(canvas->imageWidth(), h0);
    QCOMPARE(canvas->imageHeight(), w0);
    beat();

    left->trigger();                                       // undo the quarter turn
    QCOMPARE(canvas->rotationQuarters(), r0);
    beat();
    QCOMPARE(canvas->imageWidth(), w0);                    // exact state restored
    QCOMPARE(canvas->imageHeight(), h0);
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
    // Each left-click press adds exactly one point: the canvas widget is fixed to
    // the scaled-image size with a zero-offset widget→image mapping, so all three
    // clicks land inside the image (no letterboxing to miss) and none are deduped.
    QCOMPARE(totalPoints(canvas), 3);

    // Commit the line via the "New Line" action — this is what pushes an undo snapshot.
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(newLine);
    newLine->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);   // committed line landed
    QVERIFY(canvas->canUndo());
    QVERIFY(!canvas->canRedo());
    beat();

    // The Undo action steps back the history stack, dropping the committed line.
    QAction* undo = actionByText(&win, "Undo");
    QVERIFY(undo);
    undo->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    QVERIFY(canvas->canRedo());          // undo made a redo available
    beat();

    // Redo re-applies it via the real action: the committed line comes back,
    // exactly as the toolbar / Ctrl+Shift+Z would restore it.
    QAction* redo = actionByText(&win, "Redo");
    QVERIFY(redo && redo->isEnabled());
    redo->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);
    QVERIFY(!canvas->canRedo());
    beat();
  }

  void filterActionAppliesToCanvas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    // Normalize to a known baseline via the real "None" action: applyImageFilter
    // PERSISTS the chosen mode to settings, so a prior run/test can start this
    // canvas non-"none". Drive it rather than assuming the default (order-safe).
    QAction* none = actionByText(&win, "None");
    QVERIFY(none);
    none->trigger();
    QCOMPARE(canvas->imageFilter(), QString("none"));

    // Trigger the SHARED filter QAction (the toolbar Style row and the canvas
    // context menu reuse the same object): it runs the real applyImageFilter
    // path and lands the mode on the live canvas.
    QAction* bw = actionByText(&win, "Black && White");
    QVERIFY(bw && bw->isEnabled());
    bw->trigger();
    QCOMPARE(canvas->imageFilter(), QString("bw"));      // menu/toolbar wiring reached the canvas
    beat();

    // Switching filters is live and mutually exclusive (one action group).
    QAction* sepia = actionByText(&win, "Sepia");
    QVERIFY(sepia);
    sepia->trigger();
    QCOMPARE(canvas->imageFilter(), QString("sepia"));
    QVERIFY(!bw->isChecked());                            // exclusive group cleared the old mode
    beat();

    none->trigger();   // leave the persisted filter clean for other tests/runs
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
    // reuse it) wipes every committed and in-progress point — no confirm on the
    // lines action (unlike Projects ▸ Clear All), so it runs straight through.
    QAction* clear = actionByText(&win, "Clear All Lines");
    QVERIFY(clear && clear->isEnabled());
    clear->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    QCOMPARE(totalPoints(canvas), 0);   // nothing committed or in-progress remains
    beat();
  }

  // Quitting pops an "are you sure?" confirmation; answering No cancels the close and
  // leaves the window up (the Quit action, Ctrl+Q, and the title-bar X all go through it).
  void quitDialogCancelKeepsWindowOpen() {
    MainWindow win(nullptr, false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QAction* quit = actionByText(&win, "Quit");
    QVERIFY(quit);
    dismissQuitDialog("Cancel");     // blocks on the modal until the timer clicks Cancel
    quit->trigger();
    QVERIFY(win.isVisible());        // Cancel → close cancelled, window stays open
  }

  // Answering Yes lets the close through and the window disappears.
  void quitDialogConfirmClosesWindow() {
    MainWindow win(nullptr, false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QAction* quit = actionByText(&win, "Quit");
    QVERIFY(quit);
    dismissQuitDialog("Quit");
    quit->trigger();
    QTRY_VERIFY(!win.isVisible());   // Quit → the window closes
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.gui.moc"
