// MainWindow GUI e2e — Dragging the header mark (app/logo/LogoDrag, browser twin ui/drag/logoDrag.js):
// over a picture a view-only clean view, dropped there the clean view set through the toolbar's own
// controls; an empty canvas, a release anywhere else, back on the mark or after Escape sets nothing,
// and a hold that opened a show is no drag. Shared ground: MainWindow.gui.hpp, app/drag/iconDragGui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../drag/iconDragGui.hpp"
#include "LogoStage.hpp"
#include "dragOverlays.hpp"
#include "iconDrag.hpp"

using stencil::gui::LogoStage;
using stencil::support::DROP_GLOW_NAME;
using stencil::support::iconDragActive;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static QPoint logoAt(MainWindow& win) { return iconCentre(win.tools.logoBtn); }
  static QPoint canvasAt(MainWindow& win) { return iconCentre(win.canvas); }
  // The header row's empty right end: no canvas under it.
  static QPoint headerAt(MainWindow& win) {
    QToolBar* bar = win.tools.headerToolbar;
    return bar->mapToGlobal(QPoint(bar->width() - 12, bar->height() / 2));
  }
  static void carry(MainWindow& win, const QPoint& to) {
    liftIcon(win.tools.logoBtn);
    iconMouse(win.tools.logoBtn, QEvent::MouseMove, to);
  }
  static void release(MainWindow& win, const QPoint& at) { dropIcon(win.tools.logoBtn, at); }
  // A picture wearing everything the clean view takes off: a filter, a line with its points, a split.
  // The line keeps to a corner: over a line the mark restyles it instead (MainWindow.logoDrop).
  static CanvasWidget* dressed(MainWindow& win) {
    CanvasWidget* canvas = openLoaded(win);
    if (!canvas->hasImage()) return canvas;
    stencil::core::Line line;
    line.points = {{4, 4}, {24, 16}, {40, 8}};
    line.color = "#ff0000";
    line.thickness = 4;
    canvas->setLines({line});
    win.applyImageFilter(QStringLiteral("sepia"));
    win.acts.showLines->setChecked(true);
    win.acts.showPoints->setChecked(true);
    win.parts.styleControls.setCompareModeUi(QStringLiteral("vertical"));
    return canvas;
  }
  static bool untouched(MainWindow& win) {
    return win.settings.imageFilter == QLatin1String("sepia") && win.canvas->getImageFilter() == QLatin1String("sepia") &&
           win.tools.showLinesCheck->isChecked() && win.tools.showPointsCheck->isChecked() &&
           win.canvas->getCompareMode() == QLatin1String("vertical") && !win.canvas->getCleanPreview();
  }
  static int glows(MainWindow& win) { return dropGlows(win, DROP_GLOW_NAME); }
  static bool ghostOf(MainWindow& win) {
    for (QWidget* child : win.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
      if (child->isVisible() && child->testAttribute(Qt::WA_TransparentForMouseEvents) &&
          dynamic_cast<stencil::support::DragGhost*>(child) && static_cast<stencil::support::DragGhost*>(child)->faceSize() == win.tools.logoBtn->size())
        return true;
    return false;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void overTheCanvasTheMarkPreviewsACleanViewAndSetsNothing() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = dressed(win);
    QVERIFY(canvas->hasImage());
    const QString accent = win.settings.accentColor;
    carry(win, canvasAt(win));
    QVERIFY2(iconDragActive(win.tools.logoBtn), "past the slop the mark is dragged");
    QVERIFY2(ghostOf(win), "a ghost of the mark follows the pointer");
    QVERIFY2(glows(win) == 1, "the canvas glows as the drop target");
    QVERIFY2(canvas->getCleanPreview(), "over the canvas the picture shows bare");
    QVERIFY2(win.settings.imageFilter == QLatin1String("sepia") && win.tools.showLinesCheck->isChecked() &&
                 win.tools.showPointsCheck->isChecked() && canvas->getCompareMode() == QLatin1String("vertical"),
             "…as a preview: no control moved");
    iconMouse(win.tools.logoBtn, QEvent::MouseMove, headerAt(win));
    QVERIFY2(!canvas->getCleanPreview(), "off the canvas the normal paint is back");
    iconMouse(win.tools.logoBtn, QEvent::MouseMove, canvasAt(win));
    QVERIFY(canvas->getCleanPreview());
    release(win, headerAt(win));
    QVERIFY2(untouched(win), "released off the canvas, nothing changes");
    QVERIFY2(!ghostOf(win) && glows(win) == 0, "…and the ghost and the glow are gone");
    QVERIFY2(!win.tools.logoClickTimer->isActive() && win.settings.accentColor == accent,
             "a drag is no click: the accent never cycles");
  }

  void droppedOnTheCanvasTheCleanViewIsSetThroughTheToolbar() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = dressed(win);
    QVERIFY(canvas->hasImage());
    carry(win, canvasAt(win));
    release(win, canvasAt(win));
    QVERIFY(!canvas->getCleanPreview());
    QCOMPARE(canvas->getImageFilter(), QString("none"));
    QCOMPARE(win.tools.imageFilter->currentData().toString(), QString("none"));
    QVERIFY2(!win.tools.showLinesCheck->isChecked() && !win.tools.showPointsCheck->isChecked(),
             "the view checks follow");
    QCOMPARE(win.tools.compareCombo->currentData().toString(), QString("none"));
    QCOMPARE(canvas->getCompareMode(), QString("none"));
    const Settings stored = stencil::gui::fileStore::loadSettings();
    QVERIFY2(stored.imageFilter == QLatin1String("none") && !stored.showLines && !stored.showPoints,
             "…and so does what is stored");
    // The filter came off as one step on the user's own history.
    win.acts.undo->trigger();
    QCOMPARE(canvas->getImageFilter(), QString("sepia"));
    win.acts.showLines->setChecked(true);
    win.acts.showPoints->setChecked(true);
  }

  // Only what is applied changes: a view already clean is left alone, its filter applier unrun.
  void aDropOnACleanViewRunsNoApplier() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    win.applyImageFilter(QStringLiteral("none"), false);
    win.acts.showLines->setChecked(false);
    win.acts.showPoints->setChecked(true);
    win.filterDirty = false;
    QSignalSpy lines(win.acts.showLines, &QAction::toggled);
    QSignalSpy points(win.acts.showPoints, &QAction::toggled);
    carry(win, canvasAt(win));
    release(win, canvasAt(win));
    QVERIFY2(!win.filterDirty && lines.isEmpty(), "nothing already off is applied again");
    QCOMPARE(points.size(), 1);
    QVERIFY(!win.acts.showPoints->isChecked());
    win.acts.showLines->setChecked(true);
    win.acts.showPoints->setChecked(true);
  }

  // No picture, no clean view: the empty canvas neither glows nor previews, and a drop sets nothing.
  void withoutAPictureTheMarkSetsNothing() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas->hasImage());
    win.acts.showLines->setChecked(true);
    carry(win, canvasAt(win));
    QVERIFY2(iconDragActive(win.tools.logoBtn) && glows(win) == 0 && !win.canvas->getCleanPreview(),
             "the drag runs, with nothing to show on the canvas");
    release(win, canvasAt(win));
    QVERIFY2(win.acts.showLines->isChecked(), "a drop on the empty canvas sets nothing");
  }

  void releasedBackOnTheMarkOrEscapedNothingHappens() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = dressed(win);
    QVERIFY(canvas->hasImage());
    carry(win, canvasAt(win));
    QVERIFY(canvas->getCleanPreview());
    iconMouse(win.tools.logoBtn, QEvent::MouseMove, logoAt(win));
    release(win, logoAt(win));
    QVERIFY2(untouched(win), "released back on the mark, the drag cancels");
    QVERIFY2(!win.tools.logoClickTimer->isActive(), "…and the release clicks nothing");

    carry(win, canvasAt(win));
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&win, &esc);
    QCoreApplication::processEvents();
    QVERIFY2(!iconDragActive(win.tools.logoBtn) && !canvas->getCleanPreview(), "Escape ends the preview");
    release(win, canvasAt(win));
    QVERIFY2(untouched(win), "…and the press stays spent: its release over the canvas sets nothing");
  }

  void aHoldThatOpenedAShowIsNoDragAndADragStopsTheHold() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.accentColor = "violet";
    auto* stage = win.findChild<LogoStage*>("logoStage");
    QVERIFY(stage);
    auto* hold = stage->findChild<QTimer*>("logoHold");
    QVERIFY(hold);

    iconMouse(win.tools.logoBtn, QEvent::MouseButtonPress, logoAt(win));
    QVERIFY(hold->isActive());
    iconMouse(win.tools.logoBtn, QEvent::MouseMove, logoAt(win) + QPoint(0, 30));
    QVERIFY2(iconDragActive(win.tools.logoBtn) && !hold->isActive(), "a drag before the hold fires stops it");
    release(win, headerAt(win));
    QVERIFY(!stage->isOpen());

    iconMouse(win.tools.logoBtn, QEvent::MouseButtonPress, logoAt(win));
    hold->setInterval(1);
    QTest::qWait(40);
    QVERIFY2(stage->isOpen(), "a still hold opens the show");
    iconMouse(win.tools.logoBtn, QEvent::MouseMove, canvasAt(win));
    QVERIFY2(!iconDragActive(win.tools.logoBtn) && !win.canvas->getCleanPreview(),
             "the press that opened a show is no drag");
    release(win, canvasAt(win));
    stage->dismiss();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.logoDrag.gui.moc"
