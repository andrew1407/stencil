// MainWindow GUI e2e — Per-project pan/zoom, the gated danger action, and what a session restore carries.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/theme/filterFade.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Pan/zoom persistence (browser parity: storage.js's debounced scroll/zoom save + "Saved" toast): a
  // zoom change and a scrollbar drag each debounce into ONE save, and reopening restores that view.
  void panZoomPersistsPerProjectWithSavedToast() {
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId.isEmpty());
    const QString projectId = win.activeProjectId;

    // Zoom in past the viewport so the canvas grows a scrollable range: otherwise the scrollbars have
    // nowhere to move and the pan half of this test proves nothing.
    win.setZoom(5.0);
    win.scroll->horizontalScrollBar()->setValue(30);
    win.scroll->verticalScrollBar()->setValue(20);
    QTest::qWait(600);   // past the 400ms debounce

    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr);
      QCOMPARE(pr->zoomScale, 5.0);
      QCOMPARE(pr->scrollLeft, 30);
      QCOMPARE(pr->scrollTop, 20);
    }
    // The debounced save flashes the same toast every other save does.
    {
      bool sawSaved = false;
      for (QLabel* l : win.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly))
        if (l->property("stencilToastText").toString() == "Saved") sawSaved = true;
      QVERIFY2(sawSaved, "no \"Saved\" toast after the debounced pan/zoom save");
    }

    // Knock the live canvas to a different zoom WITHOUT setZoom (which would reschedule and overwrite
    // the save just verified), simulating an editor sitting elsewhere as this project reopens.
    canvas->setScale(1.0);
    QVERIFY(win.loadProjectIntoCanvas(projectId, /*animate=*/false));
    QCOMPARE(canvas->getScale(), 5.0);
    // The scroll half restores a turn later (QTimer::singleShot(0, …)), so it is the one
    // to wait on — the scale is already back by the time loadProjectIntoCanvas returns.
    QTRY_COMPARE(win.scroll->horizontalScrollBar()->value(), 30);
    QCOMPARE(win.scroll->verticalScrollBar()->value(), 20);

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("OK");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

  void clearProjectActionIsDangerAndGated() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QAction* clear = win.actClearProject;
    QVERIFY(clear);
    // With an image loaded it is live…
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());   // the bare-QImage adoption path
    win.refreshActions();
    QVERIFY2(clear->isEnabled(), "Clear Project is dead with an image loaded");
    // …and dead once there is nothing left to clear (an empty canvas, no project).
    win.canvas->clearImage();
    win.activeProjectId.clear();
    win.refreshActions();
    QVERIFY2(!clear->isEnabled(), "Clear Project stays live on an empty editor");
    // The ACTION's glyph is the ordinary menu tone, never danger red: menus paint icons muted (browser
    // .ctx-icon), and the red belongs to the filled toolbar button.
    const QImage glyph = clear->icon().pixmap(16, 16).toImage();
    QVERIFY(!glyph.isNull());
    const QColor danger = stencil::gui::themePalette(false).danger;
    const QColor dangerDark = stencil::gui::themePalette(true).danger;
    bool tinted = false;
    for (int y = 0; y < glyph.height() && !tinted; ++y)
      for (int x = 0; x < glyph.width(); ++x) {
        const QColor c = glyph.pixelColor(x, y);
        if (c.alpha() < 40) continue;
        const auto near = [&c](const QColor& d) {
          return qAbs(c.red() - d.red()) < 45 && qAbs(c.green() - d.green()) < 45
              && qAbs(c.blue() - d.blue()) < 45;
        };
        if (near(danger) || near(dangerDark)) { tinted = true; break; }
      }
    QVERIFY2(!tinted, "the Clear Project trash is still painted in the danger colour");
  }

  // A restored session must keep its PROJECT identity: without the binding a relaunch showed the
  // project's pixels while activeProjectId was empty, orphaning the image when it was deleted.
  void sessionRoundTripsTheActiveProjectBinding() {
    MainWindow win(nullptr, false);
    win.resize(1200, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkMagenta);
    win.loadImageWithLayout(img, QJsonObject());
    win.createLocalProject(QStringLiteral("session-bind"), /*announce=*/false);
    QVERIFY(!win.activeProjectId.isEmpty());
    win.saveSessionNow();
    const auto sess = stencil::gui::fileStore::loadSession();
    QVERIFY(sess.has_value());
    QCOMPARE(sess->activeProjectId, win.activeProjectId);
    // …and the restore path re-binds it (project still exists in the list).
    MainWindow win2(nullptr, true);
    QCOMPARE(win2.activeProjectId, win.activeProjectId);
  }

  // Deliberate NON-round-trip: the image filter/tint and the compare split view must NOT carry into a
  // freshly reopened app, unlike everything else a session restores.
  void sessionRestoreDoesNotCarryOverTheFilterOrTint() {
    MainWindow win(nullptr, false);
    win.resize(1200, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // a REAL file path — restoreSession needs one to reload from
    QTRY_VERIFY(win.canvas->hasImage());
    win.applyImageFilter(QStringLiteral("custom"));
    win.applyTintColor(QColor(200, 40, 40));
    QCOMPARE(win.settings.imageFilter, QStringLiteral("custom"));
    win.saveSessionNow();

    MainWindow win2(nullptr, true);
    QVERIFY(win2.canvas && win2.canvas->hasImage());   // the rest of the session DID restore
    QCOMPARE(win2.settings.imageFilter, QStringLiteral("none"));
    QCOMPARE(win2.canvas->getImageFilter(), QStringLiteral("none"));
    if (win2.imageFilter) QCOMPARE(win2.imageFilter->currentData().toString(), QStringLiteral("none"));
    if (win2.filterColorBtn) QVERIFY(!win2.filterColorBtn->isVisible());
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsView.gui.moc"
