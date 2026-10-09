// MainWindow GUI e2e — Dragging one toolbar colour chip onto another (support/drag/colorDrag, browser
// twin ui/drag/colorDrag.js): the target takes the colour through its own pick path, the point chip
// keeping it as its own, and a drop off every swatch changes nothing. Shared ground:
// MainWindow.gui.hpp, app/drag/iconDragGui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../drag/iconDragGui.hpp"
#include "iconDrag.hpp"

using stencil::support::DROP_GLOW_NAME;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static void carry(QWidget* from, QWidget* onto) { dragIcon(from, iconCentre(onto), iconCentre(onto)); }
  // Narrow enough that the wrapped tool rows sit on the offscreen screen, where widgetAt finds them.
  static CanvasWidget* opened(MainWindow& win) {
    CanvasWidget* canvas = openLoaded(win);
    win.resize(780, 560);
    win.applyImageFilter(QStringLiteral("custom"));
    win.applyTintColor(QColor("#123456"));
    QApplication::processEvents();
    return canvas;
  }
  static bool reachable(QWidget* chip) { return QApplication::widgetAt(iconCentre(chip)) == chip; }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theTintChipTakesTheLineColourAsItsPick() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(opened(win)->hasImage());
    QVERIFY(reachable(win.tools.filterColorBtn));
    const QColor line = win.tools.lineColorValue;
    QVERIFY(win.tools.filterColorValue != line);
    liftIcon(win.tools.lineColorBtn);
    QVERIFY2(dropGlows(win, DROP_GLOW_NAME) >= 2, "the other chips glow while the colour is carried");
    dropIcon(win.tools.lineColorBtn, iconCentre(win.tools.filterColorBtn));
    QVERIFY2(win.tools.filterColorValue == line && QColor(win.settings.filterColor) == line,
             "the tint took the line colour and keeps it");
    QVERIFY2(win.canvas->getFilterColor() == line, "…and the picture is tinted with it");
    QCOMPARE(dropGlows(win, DROP_GLOW_NAME), 0);
  }

  void thePointChipTakesAColourAsItsOwn() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(opened(win)->hasImage());
    QVERIFY(reachable(win.tools.pointColorBtn));
    const QColor tint = win.tools.filterColorValue;
    carry(win.tools.filterColorBtn, win.tools.pointColorBtn);
    QVERIFY2(QColor(win.settings.defaultPointColor) == tint, "the points took the tint as their own colour");
    carry(win.tools.lineColorBtn, win.tools.pointColorBtn);
    QVERIFY2(QColor(win.settings.defaultPointColor) == win.tools.lineColorValue,
             "the line's colour dropped on them is theirs to keep, as picking it is");
  }

  void aDropOffEverySwatchChangesNothing() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(opened(win)->hasImage());
    QVERIFY(reachable(win.tools.pointColorBtn) && reachable(win.tools.lineColorBtn));
    const QColor line = win.tools.lineColorValue;
    const QString point = win.settings.defaultPointColor;
    carry(win.tools.lineColorBtn, win.canvas);
    dragIcon(win.tools.lineColorBtn, iconCentre(win.tools.pointColorBtn), iconCentre(win.tools.lineColorBtn));
    QVERIFY2(win.tools.lineColorValue == line && win.settings.defaultPointColor == point,
             "dropped on the canvas or carried back home, nothing takes a colour");
    QCOMPARE(dropGlows(win, DROP_GLOW_NAME), 0);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.colorDrag.gui.moc"
