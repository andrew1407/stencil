// MainWindow GUI e2e — Glyphs on the paste dialog's buttons, and the image arrival that assembles or is skipped.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void layoutPasteDialogButtonsCarryGlyphs() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // An existing line is what raises the question at all.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start);
    start->trigger();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(canvas->width() * 0.3, canvas->height() * 0.3));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(canvas->width() * 0.6, canvas->height() * 0.5));
    QTRY_VERIFY(!canvas->allLines().empty());

    QApplication::clipboard()->setText(
        QStringLiteral(R"({"lines":[{"points":[{"x":5,"y":5},{"x":9,"y":9}]}]})"));

    // Inspect the modal while it blocks the trigger, then back out of it. The prompt
    // is the chrome-styled askAlt (modalChrome confirmModalChoice), not a QMessageBox.
    QMap<QString, bool> hasGlyph;
    QTimer::singleShot(0, [&hasGlyph]() {
      for (int i = 0; i < 200; ++i) {
        QWidget* m = QApplication::activeModalWidget();
        if (m && m->objectName() == QLatin1String("stencilConfirmModal")) {
          for (QPushButton* b : m->findChildren<QPushButton*>())
            hasGlyph.insert(QString(b->text()).remove('&'), !b->icon().isNull());
          for (QPushButton* b : m->findChildren<QPushButton*>())
            if (QString(b->text()).remove('&').compare("Cancel", Qt::CaseInsensitive) == 0) {
              b->click();
              return;
            }
          if (auto* d = qobject_cast<QDialog*>(m)) d->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    QAction* paste = actionByText(&win, "Paste Layout JSON");
    QVERIFY(paste);
    paste->trigger();

    QVERIFY2(hasGlyph.value("Combine", false), "Combine must show a glyph, not a bare word");
    QVERIFY2(hasGlyph.value("Replace", false), "Replace must show a glyph");
    QVERIFY2(hasGlyph.value("Cancel", false), "Cancel must show a glyph");
    // The glyph names themselves must resolve — a renamed one degrades to a null QIcon,
    // which is exactly the "button has no icon" the assertions above would then catch,
    // but this says WHICH name broke.
    QVERIFY2(stencil::gui::hasIcon("layers"), "Combine's glyph");
    QVERIFY2(stencil::gui::hasIcon("swap"), "Replace's glyph");
    beat();
  }

  // Every way a picture lands on the canvas ASSEMBLES out of dust (Sweep::GATHER) instead of
  // appearing all at once, with the real canvas held back until the motes land: a fresh open
  // (the OS-open / drop path — a hand-built QDropEvent never routes through Qt's drag
  // session), a created BLANK (it used to pop into place while a dropped one animated), and
  // a REOPEN of a saved project, the everyday route that once had no arrival at all.
  void imageArrivalAssemblesOnEveryRoute() {
    const auto motion = withMotion();
    const char* DUST = stencil::gui::DisintegrateOverlay::OBJECT_NAME;
    enum Route { FRESH_OPEN, CREATED_BLANK, REOPENED_PROJECT };
    for (const Route route : {FRESH_OPEN, CREATED_BLANK, REOPENED_PROJECT}) {
      const char* name = route == FRESH_OPEN ? "fresh open"
                         : route == CREATED_BLANK ? "created blank" : "reopened project";
      MainWindow win(nullptr, false);
      win.resize(1000, 760);
      win.show();
      QVERIFY2(QTest::qWaitForWindowExposed(&win), name);
      CanvasWidget* canvas = win.findChild<CanvasWidget*>();
      QVERIFY2(canvas, name);
      if (route == REOPENED_PROJECT) {
        // Let the OPEN's own arrival finish, so what we see next belongs to the reopen.
        win.openPathFromOS(guiTestImage());
        QTRY_VERIFY2_WITH_TIMEOUT(canvas->hasImage(), name, 5000);
        QTRY_VERIFY2_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr, name,
                                  stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
      }
      QVERIFY2(!win.findChild<QWidget*>(DUST), name);   // nothing flying before the route runs
      switch (route) {
        case FRESH_OPEN:
          win.openPathFromOS(guiTestImage());
          break;
        case CREATED_BLANK:
          win.createBlankImageFromDialog(QColor("#3366cc"), 320, 240);
          break;
        case REOPENED_PROJECT:
          QVERIFY2(!win.activeProjectId_.isEmpty(), "the loaded image was adopted as a project");
          QVERIFY2(win.loadProjectIntoCanvas(win.activeProjectId_), name);
          break;
      }
      QTRY_VERIFY2_WITH_TIMEOUT(canvas->hasImage(), name, 5000);
      // The dust layer exists and the canvas is behind it (opacity effect at 0), so the
      // picture is the motes, not a canvas that popped in under them.
      QTRY_VERIFY2_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) != nullptr, name, 3000);
      if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(canvas->graphicsEffect()))
        QVERIFY2(fx->opacity() < 0.01, "the real canvas waits behind the motes");
      // Both are gone when it lands, leaving no effect on a canvas that repaints per stroke.
      QTRY_VERIFY2_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr, name,
                                stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
      QTRY_VERIFY2_WITH_TIMEOUT(canvas->graphicsEffect() == nullptr, name, 2000);
    }
    beat();
  }

  // …and the two shapes that must NOT flourish. A REBIND is not an arrival: the same
  // picture is already on screen (a move-to-local relinks the open editor). Under reduced
  // motion the image is simply THERE — the bug pinned there is not the missing dust but the
  // opacity effect, which used to stay on at 0 and leave the canvas blank for 900 ms.
  void arrivalIsSkippedWhenNothingIsArriving() {
    const char* DUST = stencil::gui::DisintegrateOverlay::OBJECT_NAME;
    for (const bool reduced : {false, true}) {
      const char* name = reduced ? "reduced motion" : "rebind";
      const auto motion = motionPinned(!reduced);
      MainWindow win(nullptr, false);
      CanvasWidget* canvas = openLoaded(win);
      QTRY_VERIFY2_WITH_TIMEOUT(canvas->hasImage(), name, 5000);
      if (reduced) {
        QVERIFY2(!win.findChild<QWidget*>(DUST), "no dust under reduced motion");
      } else {
        // The open's own arrival has to land first, or the rebind inherits its dust.
        QTRY_VERIFY2_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr, name,
                                  stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
        QVERIFY2(win.loadProjectIntoCanvas(win.activeProjectId_, /*animate=*/false), name);
        settle([&] { return win.findChild<QWidget*>(DUST) != nullptr; }, 150);
        QVERIFY2(!win.findChild<QWidget*>(DUST), "a rebind is not an image appearing");
      }
      QVERIFY2(!canvas->graphicsEffect(), "the end state, immediately: a visible canvas");
      if (!reduced) continue;

      // The clear counterpart lands on its end state too — no scatter, and the empty-canvas
      // invitation is back at once instead of waiting out an animation that never ran.
      QAction* clear = actionByText(&win, "Clear Project");
      QVERIFY(clear);
      dismissModal("OK");
      clear->trigger();
      QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
      QVERIFY2(!win.findChild<QWidget*>(DUST), "no dust on the clear either");
      QVERIFY2(!canvas->idleHintHidden(), "the invitation is not held back by a missing animation");
    }
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motionGlyphs.gui.moc"
