// MainWindow GUI e2e — The selected-line bar, the idle hint held until the dust lands, and compare.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The "Selected Line:" bar appears/disappears through dustSelectedLineBarIn/Out
  // (MainWindow.cpp) rather than a plain instant show/hide. Like the other docked
  // surface flights, it declines under the offscreen QPA platform this suite runs
  // under, so this asserts the end state rather than a live flight.
  void selectedLineBarAppearsAndDisappearsWithDust() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY2(!win.selectedLineDock_->isVisible(), "nothing selected yet");

    stencil::core::Line line;
    line.points = {{20, 20}, {80, 80}};
    canvas->setLines({line});

    // Select it: the bar comes up (and, off this platform, dust would gather into it).
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock_->isVisible(), 2000);
    beat();

    // selectedLineBarDustPoint() is plain geometry, so it runs fine offscreen even
    // though the flight it feeds does not.
    QVERIFY2(win.imageInfoBar_ && win.imageInfoBar_->isVisible(), "no image-info row to anchor to");
    const QRect barPicture(win.selectedLineBar_->mapTo(&win, QPoint(0, 0)),
                           win.selectedLineBar_->size());
    const QRect infoRectNow(win.imageInfoBar_->mapTo(&win, QPoint(0, 0)), win.imageInfoBar_->size());
    const int dockTop = win.selectedLineDock_->mapTo(&win, QPoint(0, 0)).y();

    // The x is the BAR's own centre — both bars span the full window width (each its own
    // Qt::TopDockWidgetArea dock), so this already IS the window's centre.
    QVERIFY2(std::abs(barPicture.center().x() - win.width() / 2) < 4,
             "the bar itself is not spanning the full window width — the premise of this test");
    const QPoint openPt = win.selectedLineBarDustPoint(barPicture, /*closing=*/false);
    QCOMPARE(openPt.x(), barPicture.center().x());
    QCOMPARE(openPt.y(), infoRectNow.bottom());   // reflow already ran — read it as-is

    // Closing predicts the row's post-close position (the dock's current top + the row's
    // height) rather than using its live, still-stale bottom — which would overshoot.
    const QPoint closePt = win.selectedLineBarDustPoint(barPicture, /*closing=*/true);
    QCOMPARE(closePt.x(), barPicture.center().x());
    QCOMPARE(closePt.y(), dockTop + infoRectNow.height());
    QVERIFY2(closePt.y() <= barPicture.top() + infoRectNow.height(),
             "the dock's own top must be at or above the bar's content-widget top");
    QVERIFY2(closePt.y() < infoRectNow.bottom(),
             "the closing point used the stale (pre-close) position instead of predicting it");

    // Reselecting a DIFFERENT line while the bar is already open must not disturb it —
    // it just repopulates in place (same as the browser's wasHidden gate).
    stencil::core::Line line2;
    line2.points = {{100, 20}, {160, 80}};
    canvas->setLines({line, line2});
    canvas->selectLineByIndex(1);
    QTRY_VERIFY2(win.selectedLineDock_->isVisible(), "the bar stays up across a re-selection");
    beat();

    // Deselect: the bar goes away (and, off this platform, dust would scatter out of it).
    canvas->deselect();
    QTRY_VERIFY_WITH_TIMEOUT(!win.selectedLineDock_->isVisible(), 2000);
    beat();

    // Re-selecting after a full hide brings it straight back — nothing latched stuck.
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock_->isVisible(), 2000);
    beat();
  }

  // Clearing the image scatters it as dust — and the empty-canvas invitation must NOT
  // appear underneath the falling particles, which reads as the clear happening twice.
  // It is held back for the length of the animation, and cannot be clicked while hidden.
  void clearHoldsTheIdleHintUntilTheDustLands() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(!canvas->idleHintHidden());

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    dismissModal("OK");
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);

    // Image gone, dust falling, invitation still off screen.
    QVERIFY2(canvas->idleHintHidden(), "the blank-image affordance must wait for the dust");
    QSignalSpy asked(canvas, &CanvasWidget::blankImageRequested);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QCOMPARE(asked.count(), 0);   // nothing visible to click, so nothing opens

    // …and it comes back once the animation is over, so the affordance is reachable again.
    // NOT clicked here: accepting it opens the blank-image creator, whose modal loop would
    // hold this test until it timed out (which is exactly what it did).
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->idleHintHidden(),
                             stencil::gui::DisintegrateOverlay::DUST_MS + 1500);
    QCOMPARE(asked.count(), 0);
    beat();
  }

  // A REAL click through the compare combo's own themed popup, not setCompareModeUi():
  // the connect() has to live in buildDrawViewToolbar, after compareCombo_ is built —
  // wired from buildStyleToolbar it was a connect() on a null sender, silently dropped.
  void compareComboClickActuallyChangesTheCanvas() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());

    QVERIFY(win.compareCombo_);
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("none"));
    win.compareCombo_->showPopup();
    QTRY_VERIFY(QApplication::activePopupWidget());
    QWidget* popup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) popup = w;
    QVERIFY2(popup, "the compare combo's themed popup never appeared");
    auto* list = popup->findChild<QListView*>("searchComboList");
    QVERIFY(list);
    // Row 2 = "Split ↔" (vertical) — see the addItem() order in buildDrawViewToolbar.
    const QModelIndex idx = list->model()->index(2, 0);
    QCOMPARE(idx.data(Qt::DisplayRole).toString(), QString::fromUtf8("Split ↔"));
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(idx).center());
    QTRY_COMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("vertical"));
    QCOMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
  }

  // A split compare of a BLANK page shows the blank's own fill on BOTH halves:
  // the original side is the untouched red page, the edit side the red page
  // with the lines — never a gray/neutral placeholder.
  void compareSplitOfBlankKeepsItsFill() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.applyImageFilter("none");   // the filter persists across sessions/tests
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"done\",\"actions\":["
                      "{\"op\":\"blank\",\"color\":\"red\",\"format\":\"a4\"},"
                      "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":200,\"y\":300},"
                      "{\"x\":600,\"y\":300},{\"x\":600,\"y\":800},{\"x\":200,\"y\":800},"
                      "{\"x\":200,\"y\":300}],\"color\":\"#000000\"}]},"
                      "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.5}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("red page with a centred rectangle, compared side by side");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    // The arrival effect hides the canvas briefly; grab only once it is gone.
    QTRY_VERIFY(win.canvas_->graphicsEffect() == nullptr);
    const QImage shot = win.canvas_->grab().toImage();
    // Sample well inside each half, away from the rectangle and the divider.
    const QColor left = shot.pixelColor(
        QPoint(int(shot.width() * 0.10), int(shot.height() * 0.5)));
    const QColor right = shot.pixelColor(
        QPoint(int(shot.width() * 0.90), int(shot.height() * 0.5)));
    const auto isRed = [](const QColor& c) {
      return c.red() > 200 && c.green() < 80 && c.blue() < 80;
    };
    QVERIFY2(isRed(right), qPrintable(QStringLiteral("edit half is %1, not the blank fill")
                                          .arg(right.name())));
    QVERIFY2(isRed(left), qPrintable(QStringLiteral("original half is %1, not the blank fill")
                                         .arg(left.name())));
    // With a filter riding (how a model often colours a page: blank + tint/filter),
    // a BLANK's compare still shows the SAME page colour on both halves — its
    // colour IS the page, so only the lines may differ. Before the fix the
    // original half dropped the filter and went red-vs-gray.
    win.applyImageFilter("bw");
    QTest::qWait(30);
    const QImage shotF = win.canvas_->grab().toImage();
    const QColor leftF = shotF.pixelColor(
        QPoint(int(shotF.width() * 0.10), int(shotF.height() * 0.5)));
    const QColor rightF = shotF.pixelColor(
        QPoint(int(shotF.width() * 0.90), int(shotF.height() * 0.5)));
    QCOMPARE(leftF.name(), rightF.name());
    QVERIFY2(!isRed(leftF), "the filtered blank's original half ignored the filter");
    win.applyImageFilter("none");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.canvasBar.gui.moc"
