// MainWindow GUI e2e — Leaving fullscreen: the rows, the header and the panel are back before the
// window changes state, so the frame macOS snapshots for its exit transition is the whole editor.
// Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  // macOS paints inside showNormal() for its exit snapshot and keeps that frame: every row is back before it.
  void exitRestoresTheRowsBeforeShowNormal() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.acts.fullscreen->trigger();
    QVERIFY(!win.tools.headerToolbar->isVisible());
    struct StateSpy : QObject {
      MainWindow& w; bool rowsBack = false; bool seen = false;
      explicit StateSpy(MainWindow& w) : w(w) {}
      bool eventFilter(QObject* o, QEvent* e) override {
        if (o == &w && e->type() == QEvent::WindowStateChange && !seen)
          seen = true, rowsBack = w.tools.headerToolbar->isVisible() && w.selPanel->isVisible();
        return false;
      }
    } spy(win);
    win.installEventFilter(&spy);
    win.acts.fullscreen->trigger();
    QTRY_VERIFY(spy.seen);
    QVERIFY2(spy.rowsBack, "showNormal() ran before the header row and the panel were restored");
  }

  // The frame painted inside a narrowing resize (the one macOS animates out of fullscreen) holds the
  // re-wrapped row whole: its toolbar follows in the same turn, not on a later LayoutRequest.
  void narrowingResizeSettlesTheToolbarAtOnce() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1700, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* bar = win.findChild<QToolBar*>("mainToolbar");
    auto* row = win.findChild<QWidget*>("toolWrapRow");
    QVERIFY(bar && row);
    QTRY_VERIFY(bar->height() >= row->height());
    const int wide = row->height();
    win.resize(1000, 760);   // no event loop between the resize and this read, as inside AppKit's snapshot
    QVERIFY2(row->height() > wide, "the narrower row should wrap onto more lines");
    QVERIFY2(bar->height() >= row->height(), "the toolbar kept the wide row's height: the rows draw clipped");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.fullscreenExit.gui.moc"
