// MainWindow GUI e2e — A rotate or a flip keeps the zoom and the picture point centred in the
// viewport; a picture sitting at its fit refits (browser ZoomPan.viewAnchor / restoreAnchor).
// Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../MainWindowFlight.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static void openWide(MainWindow& win) {
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(400, 200, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());
    win.canvas->applyCrop({0, 0, 400, 200}, false);
    settleLayout(&win, 150);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aZoomedPictureKeepsItsZoomAndCentreThroughATurnAndAFlip() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    openWide(win);
    win.setZoom(4.0);
    // Picture row 100 is its middle, so the turned picture can still centre the point.
    const QSize vp = win.scroll->viewport()->size();
    win.parts.view.scrollTo(240 * 4 - vp.width() / 2, 100 * 4 - vp.height() / 2);
    const auto before = win.parts.view.viewAnchor();
    QVERIFY(!before.fit);
    win.acts.rotateRight->trigger();
    QCOMPARE(win.canvas->getScale(), 4.0);
    auto after = win.parts.view.viewAnchor();
    QVERIFY(std::abs(after.x - (200 - before.y)) <= 1 && std::abs(after.y - before.x) <= 1);
    win.acts.flipImage->trigger();
    QCOMPARE(win.canvas->getScale(), 4.0);
    const auto flipped = win.parts.view.viewAnchor();
    QVERIFY(std::abs(flipped.x - (200 - after.x)) <= 1 && std::abs(flipped.y - after.y) <= 1);
  }

  void aFittedPictureRefitsItsTurnedShape() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    openWide(win);
    win.fitToWindow();
    QVERIFY(win.parts.view.viewAnchor().fit);
    win.acts.rotateLeft->trigger();
    QCOMPARE(win.canvas->getScale(), win.parts.view.fitScale());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.rotateZoom.gui.moc"
