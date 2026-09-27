// MainWindow GUI e2e — The canvas hover tooltip hits what the browser's does (ui/tip/tooltipHover.js):
// HIT.pointRadiusPx and HIT.lineRadiusPx over the zoom, hitTest.js's point order, the topmost line.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // What the tooltip shows for an image-space spot: its target key ("point:x:y", "line:i:…"), or
  // empty when it stays hidden. `immediate` is the modifier refresh, so no reveal delay to wait out.
  static QString hitAt(MainWindow& win, double ix, double iy) {
    win.parts.hoverTip.hideHoverTooltip();
    win.parts.hoverTip.onHoverDetail(ix, iy, QPoint(10, 10), Qt::NoModifier, /*immediate=*/true);
    return win.overlays.tooltip->isVisible() ? win.parts.hoverTip.hoverShownKey : QString();
  }

  static stencil::core::Line lineOf(std::vector<stencil::core::Point> points) {
    stencil::core::Line line;
    line.points = std::move(points);
    return line;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void hoverTooltipTakesTheBrowsersRadii() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(400, 300, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.settings.tooltipEnabled = true;   // independent of the machine's saved settings
    for (const double zoom : {1.0, 2.0}) {
      win.canvas->setLines({lineOf({{100, 150}, {300, 150}})});
      win.setZoom(zoom);
      QCOMPARE(win.canvas->getScale(), zoom);
      const double px = 1.0 / zoom;   // one screen px, in image px
      const QByteArray at = QByteArray("zoom ") + QByteArray::number(zoom);
      // A point inside HIT.pointRadiusPx (10 screen px) is labelled; past it the pointer is only on
      // its line, whose point hits pad the stroke radius by 4 image px (hitTest.js findLineAt).
      QVERIFY2(hitAt(win, 100, 150 + 9 * px) == QStringLiteral("point:100:150"), at);
      const QString past = hitAt(win, 100, 150 + 11 * px);
      QVERIFY2(!past.startsWith(QStringLiteral("point:")) && past.startsWith(QStringLiteral("line:0:")),
               qPrintable(at + " 11 px from the point: " + past));
      // A stroke inside HIT.lineRadiusPx (8 screen px) is labelled, away from both of its points.
      QVERIFY2(hitAt(win, 200, 150 + 7 * px).startsWith(QStringLiteral("line:0:")), at);
      QVERIFY2(hitAt(win, 200, 150 + 9 * px).isEmpty(), at);

      // Two strokes under the pointer: the TOPMOST (last drawn) answers.
      win.canvas->setLines({lineOf({{100, 150}, {300, 150}}), lineOf({{100, 152}, {300, 152}})});
      QVERIFY2(hitAt(win, 200, 151).startsWith(QStringLiteral("line:1:")), at);
      // Two points in reach: the first line drawn answers, not the nearer point (hitTest.js order).
      win.canvas->setLines({lineOf({{100, 150}, {300, 150}}), lineOf({{100 + 4 * px, 150}, {100, 250}})});
      QVERIFY2(hitAt(win, 100 + 3 * px, 150) == QStringLiteral("point:100:150"), at);
    }
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.hoverTip.gui.moc"
