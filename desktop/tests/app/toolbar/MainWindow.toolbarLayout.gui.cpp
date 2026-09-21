// MainWindow GUI e2e — Geometry: one centre line per section, a centred label, the swatches and ZOOM.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Browser parity: both colour swatches are present and captioned at two levels — an uppercase section
  // header (makeToolSection) plus an inline field label, since two identical swatches need telling apart.
  void toolbarExposesCaptionedLineAndPointColourSwatches() {
    MainWindow win(nullptr, false);

    const auto labelWithText = [&win](const QString& needle) {
      for (QLabel* l : win.findChildren<QLabel*>())
        if (l->text().contains(needle, Qt::CaseInsensitive)) return true;
      return false;
    };
    const auto sectionNamed = [&win](const QString& title) {
      for (QLabel* l : win.findChildren<QLabel*>())
        if (l->objectName() == "sectionLabel" && l->text().compare(title, Qt::CaseInsensitive) == 0)
          return true;
      return false;
    };
    QVERIFY2(sectionNamed("Line"), "the line group needs a section header");
    QVERIFY2(sectionNamed("Point"), "the point group needs a section header");
    QVERIFY2(labelWithText("Color"), "each colour swatch still needs its own field label");
    // The rows carry captions at all — the browser names every toolbar cluster. The
    // filter combo opens EDIT (browser order), so there is no separate Filter section.
    QVERIFY2(!sectionNamed("Filter"), "the filter combo lives in EDIT, like the browser's");
    QVERIFY2(sectionNamed("Edit") && sectionNamed("View"), "the edit/view groups are named");
    QVERIFY2(sectionNamed("Zoom") && sectionNamed("Page") && sectionNamed("Formula")
                 && sectionNamed("Data") && sectionNamed("Settings"),
             "the last row's groups are named, in the browser's order");

    QToolButton* pointSwatch = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->toolTip().contains("Point color")) pointSwatch = b;
    QVERIFY2(pointSwatch, "the toolbar must offer a default point-colour swatch");
    QVERIFY2(!pointSwatch->icon().isNull(), "the swatch paints its current colour");
  }
  // Every control in a toolbar section sits on ONE centre line: the QVBoxLayout used to hand a short
  // section's spare height to its caption (browser parity: one flex row, centred).
  void toolbarSectionControlsShareOneCentreLine() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 200);
    // A section row is the widget whose sibling is the "sectionLabel" caption. The tool run WRAPS
    // (support/WrapRow.hpp), so the baseline is shared per LINE, grouped by where the flow put them.
    QHash<int, QList<QPair<QString, int>>> byRow;
    for (QWidget* rowWidget : win.findChildren<QWidget*>()) {
      QWidget* section = rowWidget->parentWidget();
      if (!section || !section->findChild<QLabel*>("sectionLabel")) continue;
      if (rowWidget->findChild<QLabel*>("sectionLabel")) continue;   // that's the caption itself
      if (!section->parentWidget() || !section->isVisible()) continue;
      const int line = section->mapTo(&win, QPoint(0, 0)).y();
      for (QWidget* c : rowWidget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!c->isVisible() || c->height() <= 0) continue;
        if (!qobject_cast<QToolButton*>(c) && !qobject_cast<QComboBox*>(c)
            && !qobject_cast<QLineEdit*>(c) && !qobject_cast<QCheckBox*>(c)) continue;
        byRow[line] << qMakePair(QString("%1(%2)").arg(c->metaObject()->className(), c->objectName()),
                                 c->mapTo(&win, QPoint(0, c->height() / 2)).y());
      }
    }
    QVERIFY2(!byRow.isEmpty(), "no toolbar sections were found");
    int rowsChecked = 0;
    for (auto it = byRow.constBegin(); it != byRow.constEnd(); ++it) {
      const auto& controls = it.value();
      if (controls.size() < 2) continue;
      ++rowsChecked;
      const int centre = controls.first().second;
      for (const auto& c : controls)
        QVERIFY2(qAbs(c.second - centre) <= 1,
                 qPrintable(QString("%1 sits at y=%2, the row centre is %3")
                                .arg(c.first).arg(c.second).arg(centre)));
    }
    QVERIFY2(rowsChecked >= 2, "expected at least two populated toolbar rows");
  }
  // The labelled Open Image button centres its icon+text: Qt left-aligns a text-beside-icon label inside
  // a hint that reserves more room on the right, so the fix redistributes the padding and this measures it.
  void openImageButtonLabelIsCentred() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.canvas->clearImage();
    win.refreshActions();
    QTRY_VERIFY2(win.openImageBtn->isVisible(), "the labelled button is not on screen");
    settleLayout(&win, 200);
    const QImage im = win.openImageBtn->grab().toImage();
    // Ink is everything that is not the button's own fill (glyph + label are white on it).
    const QColor fill = im.pixelColor(1, im.height() / 2);
    int minx = im.width(), maxx = -1;
    for (int y = 0; y < im.height(); ++y)
      for (int x = 0; x < im.width(); ++x) {
        const QColor c = im.pixelColor(x, y);
        if (qAbs(c.red() - fill.red()) + qAbs(c.green() - fill.green())
            + qAbs(c.blue() - fill.blue()) < 60) continue;
        minx = qMin(minx, x); maxx = qMax(maxx, x);
      }
    QVERIFY2(maxx > minx, "no label ink found on the button");
    const int left = minx, right = im.width() - 1 - maxx;
    QVERIFY2(qAbs(left - right) <= 2,
             qPrintable(QString("icon+text is off-centre: %1px left, %2px right").arg(left).arg(right)));
    // …and the label is the browser's 14px, not the smaller platform default.
    QCOMPARE(win.openImageBtn->font().pixelSize(), 14);
  }
  // Nothing to zoom without an image, so the whole ZOOM cluster is dead until one is loaded — the
  // browser gates zoom-in / zoom-out / zoom-fit / zoom-input on exactly that (updateButtons).
  void zoomClusterNeedsAnImage() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    QVERIFY(win.zoom);
    for (QAction* a : {win.actZoomIn, win.actZoomOut, win.actFit}) {
      QVERIFY(a);
      QVERIFY2(!a->isEnabled(), qPrintable(a->text() + " is live with no image loaded"));
    }
    QVERIFY2(!win.zoom->isEnabled(), "the % field is live with no image loaded");
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.actFit->isEnabled());
    QVERIFY(win.actZoomIn->isEnabled() && win.actZoomOut->isEnabled());
    QVERIFY2(win.zoom->isEnabled(), "the % field stayed dead with an image loaded");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarLayout.gui.moc"
