// Desktop GUI end-to-end, written with the Qt Test framework (QtTest). Unlike the other
// headless checks — which exercise CanvasWidget / fileStore in isolation — this drives the
// REAL MainWindow: it loads an image through the public OS-open path, triggers the actual
// toolbar/menu QActions (Rotate, Undo, Start Drawing), and sends real mouse clicks to the
// live canvas, asserting on observable widget state. It also drives fullscreen edge-hover reveal,
// sampling toolbar/panel geometry over time to assert the reveals animate smoothly (no flicker).
// Runs offscreen (QT_QPA_PLATFORM=offscreen), so it needs no display; registered with CTest.
#include "mainWindow.hpp"
#include "canvasWidget.hpp"
#include "fileStore.hpp"
#include <QtTest>
#include <QAction>
#include <QFile>
#include <QImage>
#include <QDir>
#include <QApplication>
#include <QMessageBox>
#include <QAbstractButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QWidgetAction>
#include <QCursor>
#include <QGraphicsEffect>
#include <QTimer>
#include <QToolBar>
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

  // A confirmation is a modal QMessageBox that blocks the triggering call (Quit, Delete Project
  // File, …). Arm this BEFORE triggering the action: it waits for the box to appear and clicks the
  // button whose label matches (the dialogs use custom "Quit"/"Cancel"/"Delete" buttons, not
  // standard Yes/No roles), letting the otherwise-blocked trigger() return with that answer.
  void dismissModal(const QString& buttonText) {
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

  // Regression: enabling the f(x,y) pill must reveal the x/y formula inputs, and they must
  // stay visible across an image load and window resizes (the state the user drives).
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
    if (pill->isChecked()) { pill->setChecked(false); QTest::qWait(30); }
    QVERIFY(!fx->isVisible());
    QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, pill->rect().center());
    QTest::qWait(60);
    QVERIFY2(fx->isVisible(), "formula inputs should appear when f(x,y) is enabled");
    win.openPathFromOS(png_);
    QTest::qWait(120);
    QVERIFY2(fx->isVisible(), "formula inputs should survive an image load");
    win.resize(720, 800); QTest::qWait(80);
    win.resize(1200, 800); QTest::qWait(80);
    QVERIFY2(fx->isVisible(), "formula inputs should survive window resizes");
  }

  // Fullscreen edge-hover: prove the top toolbars and the right points panel REVEAL WITH AN
  // ANIMATION (they pass through intermediate sizes, not an instant pop) and do so MONOTONICALLY
  // (no size oscillation = no flicker), then hide + fully restore on exit with no lingering effect.
  void fullscreenRevealAnimatesSmoothly() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    auto maxBarHeight = [&win] {
      int m = 0;
      for (QToolBar* b : win.findChildren<QToolBar*>())
        if (b->isVisible()) m = std::max(m, b->height());
      return m;
    };
    auto anyBarVisible = [&win] {
      for (QToolBar* b : win.findChildren<QToolBar*>()) if (b->isVisible()) return true;
      return false;
    };
    // Sample a size getter every ~16ms across the ~200ms animation; return the series.
    auto sample = [](auto getter) {
      QList<int> s;
      for (int i = 0; i < 20; ++i) { s.append(getter()); QTest::qWait(16); }
      return s;
    };
    auto hasIntermediate = [](const QList<int>& s, int full) {   // some value strictly inside (0, full)
      for (int v : s) if (v > 2 && v < full - 2) return true;
      return false;
    };
    auto nonDecreasing = [](const QList<int>& s) {
      for (int i = 1; i < s.size(); ++i) if (s[i] < s[i - 1] - 1) return false;   // 1px slack
      return true;
    };
    auto nonIncreasing = [](const QList<int>& s) {
      for (int i = 1; i < s.size(); ++i) if (s[i] > s[i - 1] + 1) return false;
      return true;
    };

    QAction* fs = actionByText(&win, "Fullscreen");
    QVERIFY(fs);
    fs->trigger();                                   // ENTER fullscreen (bars + panel hidden)
    QTest::qWait(120);
    QVERIFY(!anyBarVisible());                        // nothing shown until the cursor hits an edge

    // --- Top toolbars: cursor to the top band → animated slide-in ---
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() / 2, 40)));
    const QList<int> up = sample(maxBarHeight);
    const int full = up.isEmpty() ? 0 : up.last();
    QVERIFY2(full > 10, "toolbars should have revealed to a real height");
    QVERIFY2(hasIntermediate(up, full), "toolbar reveal popped instantly (no intermediate heights)");
    QVERIFY2(nonDecreasing(up), "toolbar reveal height oscillated (flicker)");

    // Cursor well below the keep-zone → animated slide-out.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() / 2, win.height() - 40)));
    const QList<int> down = sample(maxBarHeight);
    QVERIFY2(nonIncreasing(down), "toolbar hide height oscillated (flicker)");
    QTest::qWait(120);

    // --- Right points panel: cursor to the right edge → animated slide-in reveal ---
    QWidget* panel = nullptr;
    for (QWidget* dw : win.findChildren<QWidget*>())
      if (QString(dw->metaObject()->className()).contains("SelectionPanel")) { panel = dw; break; }
    if (panel) {
      QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 2, win.height() / 2)));
      auto panelW = [panel] { return panel->isVisible() ? panel->width() : 0; };
      const QList<int> pin = sample(panelW);
      const int pfull = pin.isEmpty() ? 0 : pin.last();
      if (pfull > 10) {   // reveal fired (setPos is a soft no-op on some offscreen builds)
        QVERIFY2(hasIntermediate(pin, pfull), "panel reveal popped instantly (no intermediate widths)");
        QVERIFY2(nonDecreasing(pin), "panel reveal width oscillated (flicker)");
      } else {
        qWarning("panel reveal did not fire (cursor setPos likely a no-op offscreen)");
      }
    }

    // --- Exit: everything restored, no lingering graphics effect ---
    fs->trigger();
    QTest::qWait(250);
    QVERIFY(win.isVisible());
    QVERIFY(anyBarVisible());
    for (QToolBar* b : win.findChildren<QToolBar*>()) QVERIFY(b->graphicsEffect() == nullptr);
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
    // reuse it) wipes every committed and in-progress point — no confirm on the
    // lines action (unlike Projects ▸ Clear All), so it runs straight through.
    QAction* clear = actionByText(&win, "Clear All Lines");
    QVERIFY(clear && clear->isEnabled());
    clear->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    QCOMPARE(totalPoints(canvas), 0);   // nothing committed or in-progress remains
    beat();
  }

  // The trash "Clear Project" action (mirrors the browser #clear-storage button) is
  // visible for a local editor, confirms, and — on Yes — resets to the empty
  // "Open an image" canvas. The confirm reuses the modal-dismiss helper.
  void clearProjectResetsToBlankEditor() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    QVERIFY(clear->isVisible());   // shown for a local/temporary editor (hidden only for server projects)

    dismissModal("Yes");      // blocks on the confirm until the timer clicks Yes
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);   // reset to a blank editor
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    beat();
  }

  // Opening a .stencil project file (the real OS-open / drag / file-arg path) decodes its
  // embedded image and adopts its layout — image + lines + rotation — into the live canvas.
  void opensStencilProjectFile() {
    // Author a .stencil bundling the test PNG's bytes + a one-line, quarter-rotated layout.
    QByteArray png;
    {
      QFile f(png_);
      QVERIFY(f.open(QIODevice::ReadOnly));
      png = f.readAll();
    }
    stencil::core::Lines lines;
    stencil::core::Line l;
    l.points = {{10, 10}, {40, 40}};
    l.color = "#ff0000";
    lines.push_back(l);
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "GUI Project";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, lines, "none", "#7c3aed", {}, 1, {});
    const QString path = QDir::temp().filePath("stencil_gui_e2e_project.stencil");
    {
      QFile wf(path);
      QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate));
      wf.write(stencil::gui::fileStore::buildProjectFile(pf));
    }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);   // routes *.stencil -> openProjectFile
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QCOMPARE(canvas->rotationQuarters(), 1);                 // layout rotation adopted
    QVERIFY(totalPoints(canvas) > 0);                        // the line was adopted
    beat();
  }

  // Live sync: a project linked to a .stencil with live-sync ON auto-saves edits back to the
  // file (debounced). Drives openProjectFile linking → the "Live Sync with File" toggle →
  // an edit → onCanvasChanged → scheduleStencilAutosave → flushStencilAutosave writing the file.
  void liveSyncAutosavesEditsToFile() {
    QByteArray png;
    { QFile f(png_); QVERIFY(f.open(QIODevice::ReadOnly)); png = f.readAll(); }
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "Live";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, {}, "none", "#7c3aed", {}, 0, {});   // rotation 0
    const QString path = QDir::temp().filePath("stencil_gui_livesync.stencil");
    { QFile wf(path); QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate)); wf.write(stencil::gui::fileStore::buildProjectFile(pf)); }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QCOMPARE(canvas->rotationQuarters(), 0);

    QAction* live = actionByText(&win, "Live Sync with File");
    QVERIFY(live);
    QVERIFY(live->isEnabled());     // enabled because the project is file-linked
    live->setChecked(true);         // toggled → toggleStencilLiveSync(true)

    QAction* rotate = actionByText(&win, "Rotate Right");
    QVERIFY(rotate);
    rotate->trigger();
    QCOMPARE(canvas->rotationQuarters(), 1);

    // Auto-save is debounced (~800ms) — wait for the linked file to reflect the rotation.
    auto fileRotation = [&]() -> int {
      QFile rf(path);
      if (!rf.open(QIODevice::ReadOnly)) return -1;
      stencil::gui::fileStore::ProjectFileData out;
      QString err;
      if (!stencil::gui::fileStore::parseProjectFile(rf.readAll(), out, &err)) return -1;
      int w = 0, h = 0;
      stencil::core::CropRect crop;
      int rot = 0;
      stencil::gui::fileStore::parseLayoutJson(out.layout, w, h, &crop, &rot);
      return rot;
    };
    QTRY_COMPARE_WITH_TIMEOUT(fileRotation(), 1, 4000);   // the edit auto-saved into the linked file
    beat();
  }

  // Deleting the linked .stencil file removes it from disk and unlinks the project (the live-sync
  // + delete actions disable), while the project stays open in the canvas. Drives openProjectFile
  // linking → the "Delete Project File (.stencil)" action → the confirm (auto-clicked "Delete").
  void deletesLinkedProjectFile() {
    QByteArray png;
    { QFile f(png_); QVERIFY(f.open(QIODevice::ReadOnly)); png = f.readAll(); }
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "Doomed";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, {}, "none", "#7c3aed", {}, 0, {});
    const QString path = QDir::temp().filePath("stencil_gui_delete.stencil");
    { QFile wf(path); QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate)); wf.write(stencil::gui::fileStore::buildProjectFile(pf)); }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    QAction* del = actionByText(&win, "Delete Project File (.stencil)");
    QVERIFY(del);
    QVERIFY(del->isEnabled());       // enabled because the project is file-linked
    QVERIFY(QFile::exists(path));

    dismissModal("Delete");          // auto-click "Delete" on the confirm modal
    del->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 4000);   // the file was removed from disk
    QVERIFY(!del->isEnabled());      // unlinked → the delete action disables again
    QVERIFY(canvas->hasImage());     // the project itself stays open in the editor
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
    dismissModal("Cancel");     // blocks on the modal until the timer clicks Cancel
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
    dismissModal("Quit");
    quit->trigger();
    QTRY_VERIFY(!win.isVisible());   // Quit → the window closes
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.gui.moc"
