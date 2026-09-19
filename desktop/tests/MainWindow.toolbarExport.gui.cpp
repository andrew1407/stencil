// MainWindow GUI e2e — The copy/download variants and the chips that come and go with them.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Copy-to-clipboard ships the RENDERED image: filter plus drawn lines (browser
  // renderExportCanvas parity).
  void copyImageIncludesTheDrawnLines() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    // One fat red line across the middle, straight through the canvas model.
    stencil::core::Line line;
    line.color = "#ff0000";
    line.thickness = 6;
    line.points.push_back({4.0, 20.0});
    line.points.push_back({36.0, 20.0});
    win.canvas_->setLines({line});
    win.dataExport_->copyImageToClipboard();
    const QImage copied = QGuiApplication::clipboard()->image();
    QVERIFY(!copied.isNull());
    // The render ships the default centered crop, so don't pin the size — scan for
    // the red stroke instead: any strongly-red pixel proves the overlay rode along.
    bool sawLine = false;
    for (int y = 0; y < copied.height() && !sawLine; ++y)
      for (int x = 0; x < copied.width() && !sawLine; ++x) {
        const QColor c = copied.pixelColor(x, y);
        if (c.red() > 200 && c.green() < 80 && c.blue() < 80) sawLine = true;
      }
    QVERIFY2(sawLine, "expected the drawn red line in the copied image");
  }
  // Cmd+C and the toolbar Copy button's plain click default to the CURRENT image (tint + lines/points),
  // as in the browser. actCopyImageTint_ ("Filter Only", Ctrl+Alt+C) stays its own fixed variant.
  void copyDefaultIsCurrentImage() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.applyImageFilter(QStringLiteral("custom"));
    win.applyTintColor(QColor(200, 30, 30));   // a strong, easy-to-detect red tint
    // "current" vs "tint" only actually differ by the lines/points overlay — add one
    // so the two variants render to genuinely different images below.
    stencil::core::Line line;
    line.color = "#00ff00";
    line.thickness = 6;
    line.points.push_back({4.0, 20.0});
    line.points.push_back({36.0, 20.0});
    win.canvas_->setLines({line});

    // The default gesture (actCopyImage_, Ctrl+C) copies the CURRENT (full) image.
    win.actCopyImage_->trigger();
    const QImage current = QGuiApplication::clipboard()->image();
    QVERIFY(!current.isNull());
    QCOMPARE(current, win.canvas_->renderToImage(QStringLiteral("current")));

    // "Filter Only" (actCopyImageTint_) copies the filtered image with no overlay instead.
    win.actCopyImageTint_->trigger();
    const QImage tinted = QGuiApplication::clipboard()->image();
    QVERIFY(!tinted.isNull());
    QCOMPARE(tinted, win.canvas_->renderToImage(QStringLiteral("tint")));
    QVERIFY2(current != tinted, "current and tint must actually render differently here");
  }
  // "Filter Only" would render byte-identical to "Original" with no filter applied, so it is hidden, not
  // greyed, until one is — live as the filter is toggled, not on the next unrelated refresh.
  void filterOnlyHiddenWithNoFilterApplied() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.refreshActions();
    QVERIFY2(!win.actCopyImageTint_->isVisible(), "Filter Only shows with no filter applied");
    QVERIFY2(!win.actSaveImageTint_->isVisible(), "Filter Only shows with no filter applied");

    win.applyImageFilter(QStringLiteral("sepia"));
    QVERIFY2(win.actCopyImageTint_->isVisible(), "Filter Only should show once a filter is active");
    QVERIFY2(win.actSaveImageTint_->isVisible(), "Filter Only should show once a filter is active");

    win.applyImageFilter(QStringLiteral("none"));
    QVERIFY2(!win.actCopyImageTint_->isVisible(), "Filter Only should hide again once the filter clears");
    QVERIFY2(!win.actSaveImageTint_->isVisible(), "Filter Only should hide again once the filter clears");
  }
  // "Current"'s OWN row is hidden until there is something to overlay, the same reasoning as Filter
  // Only — a SEPARATE action from the toolbar buttons', which stay visible as a QToolButton mirrors it.
  void currentRowHiddenWithNoLinesButToolbarButtonStays() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.refreshActions();
    QVERIFY2(!win.actCopyImageCurrentRow_->isVisible(), "Current's row shows with nothing drawn");
    QVERIFY2(!win.actSaveImageCurrentRow_->isVisible(), "Current's row shows with nothing drawn");
    QVERIFY2(win.actCopyImage_->isVisible(), "the toolbar's own Copy action must stay visible regardless");
    QVERIFY2(win.actSaveImage_->isVisible(), "the toolbar's own Download action must stay visible regardless");
    QVERIFY(win.actCopyImage_->isEnabled());

    stencil::core::Line line;
    line.color = "#00ff00";
    line.points.push_back({4.0, 20.0});
    line.points.push_back({36.0, 20.0});
    win.canvas_->setLines({line});
    win.refreshActions();
    QVERIFY2(win.actCopyImageCurrentRow_->isVisible(), "Current's row should show once something is drawn");
    QVERIFY2(win.actSaveImageCurrentRow_->isVisible(), "Current's row should show once something is drawn");

    // Clicking the row performs the exact same thing as the toolbar button.
    win.actCopyImageCurrentRow_->trigger();
    const QImage viaRow = QGuiApplication::clipboard()->image();
    win.actCopyImage_->trigger();
    QCOMPARE(QGuiApplication::clipboard()->image(), viaRow);

    win.canvas_->setLines({});
    win.refreshActions();
    QVERIFY2(!win.actCopyImageCurrentRow_->isVisible(), "Current's row should hide again once lines are cleared");
    QVERIFY2(!win.actSaveImageCurrentRow_->isVisible(), "Current's row should hide again once lines are cleared");
    QVERIFY2(win.actCopyImage_->isVisible(), "the toolbar's own Copy action is still untouched");
  }
  // An action going invisible out from under an ALREADY-OPEN menu must take its chip with it, or it
  // floats at its last position over whatever row now sits there. Needs the live poll (installPlacer).
  void filterOnlyChipHidesLiveWhileItsMenuStaysOpen() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.applyImageFilter(QStringLiteral("sepia"));   // Filter Only visible before the popup opens

    QWidget* saveBtn = win.buttonForAction(win.actSaveImage_);
    QVERIFY(saveBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, saveBtn->rect().center(),
                          saveBtn->mapToGlobal(saveBtn->rect().center()));
    QApplication::sendEvent(saveBtn, &ctx);
    QMenu* menu = win.saveImageOptionsMenu_;
    const bool opened = menu && menu->isVisible();

    auto chipOver = [&](QAction* act) -> stencil::gui::TipBody* {
      const QRect r = menu->actionGeometry(act);
      for (QLabel* l : menu->findChildren<QLabel*>())
        if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
          if (!c->isHidden() && c->geometry().intersects(r)) return c;
      return nullptr;
    };
    // Captured into locals and the menu closed BEFORE any assertion: an early QVERIFY2 return must never
    // leave the menu open, or it outlives `win` and crashes on teardown.
    bool chippedWhileActive = false, stillChippedAfter = true;
    if (opened) {
      chippedWhileActive = chipOver(win.actSaveImageTint_) != nullptr;
      // Turn the filter off WHILE the popup stays open — no click, no reopen — and
      // give the live poll (MenuHotkeys.hpp) a moment to catch up.
      win.applyImageFilter(QStringLiteral("none"));
      for (int i = 0; i < 20 && stillChippedAfter; ++i) {
        QTest::qWait(20);
        stillChippedAfter = chipOver(win.actSaveImageTint_) != nullptr;
      }
      menu->close();
    }
    QVERIFY2(opened, "the download options popup never opened");
    QVERIFY2(chippedWhileActive, "Filter Only should be chipped while the filter is active");
    QVERIFY2(!stillChippedAfter, "Filter Only's chip is still floating after the filter cleared");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarExport.gui.moc"
