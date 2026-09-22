// MainWindow GUI e2e — the window-wide drop overlay, the save/incognito halves a drag lights,
// and the midline they read. Shared ground (helpers, the loaded window, the motion pins) is in
// MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

namespace {
  // Qt routes a drag to the WINDOW, which picks the child under the point and forwards: a drop
  // sent straight to a widget is discarded, and only this path can be taken by the chat dock.
  void dragTo(MainWindow& win, const QMimeData& mime, const QPoint& at, QEvent::Type type) {
    QDragMoveEvent ev(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier, type);
    QApplication::sendEvent(win.windowHandle(), &ev);
  }

  void dropOn(MainWindow& win, const QMimeData& mime, const QPoint& at) {
    dragTo(win, mime, at, QEvent::DragEnter);   // the enter is what makes the window a drag target
    QDropEvent ev(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(win.windowHandle(), &ev);
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  // Every case runs with the chat dock out, so the canvas viewport is off-centre and a split
  // read from it would land somewhere other than the window's midline.
  void showWithChat(MainWindow& win) {
    win.resize(1200, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.setChatShown(true, /*animate=*/false);
    QTest::qWait(50);
  }

  int viewportMid(MainWindow& win) {
    QWidget* vp = win.scroll->viewport();
    return vp->mapTo(&win, QPoint(0, 0)).x() + vp->width() / 2;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The browser's #global-drop-overlay covers the whole page, so the desktop's covers the whole
  // window: toolbar, canvas, status row and the docked chat panel alike.
  void theZonesSpanTheWholeWindow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    showWithChat(win);
    QVERIFY(win.dropZones && win.chatDock && win.scroll && win.status && win.toolRow());
    QCOMPARE(win.dropZones->parentWidget(), static_cast<QWidget*>(&win));

    win.dropZones->showZones();
    QCOMPARE(win.dropZones->geometry(), win.rect());
    QVERIFY2(win.dropZones->width() > win.scroll->viewport()->width(),
             "the overlay must be the window's size, not the canvas viewport's");
    for (QWidget* under : {static_cast<QWidget*>(win.toolRow()),
                           static_cast<QWidget*>(win.chatDock),
                           static_cast<QWidget*>(win.status)}) {
      const QRect box(under->mapTo(&win, QPoint(0, 0)), under->size());
      QVERIFY2(win.dropZones->geometry().contains(box), qPrintable(under->objectName()));
    }
    // The split is painted down the overlay's own middle, and that is now the window's.
    QCOMPARE(win.dropZones->geometry().x() + win.dropZones->width() / 2, win.width() / 2);
    QVERIFY2(qAbs(viewportMid(win) - win.width() / 2) > 8,
             "the viewport midline must differ from the window's, or nothing is proved");
    win.dropZones->hideZonesNow();
  }

  // The split the user aims at is the one PAINTED, and a docked panel no longer moves it: both
  // the lit half and the drop read the window midline.
  void theLitHalfAndTheDropAgreeWithThePaintedSplit() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    showWithChat(win);
    QVERIFY(win.dropZones && win.scroll);
    const int mid = win.width() / 2;
    // Strictly between the two midlines, so the window's rule and the old viewport one disagree.
    const int band = (mid + viewportMid(win)) / 2;
    QVERIFY2(qAbs(viewportMid(win) - mid) > 8 && band != mid,
             "the docks must shift the viewport midline off the window's");
    const bool bandIsSave = band < mid;

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(guiTestImage())});
    dragTo(win, mime, QPoint(mid - 6, 300), QEvent::DragEnter);
    QVERIFY2(win.dropZones->getActiveLeft(), "just left of the painted split is the SAVE half");
    dragTo(win, mime, QPoint(mid + 6, 300), QEvent::DragMove);
    QVERIFY2(!win.dropZones->getActiveLeft(), "and just right of it is the incognito half");
    dragTo(win, mime, QPoint(band, 300), QEvent::DragMove);
    QCOMPARE(win.dropZones->getActiveLeft(), bandIsSave);

    dropOn(win, mime, QPoint(band, 300));
    QCOMPARE(win.incognito, !bandIsSave);
    dismissModal(QStringLiteral("This window"));   // an image is open now, so the drop asks where
    dropOn(win, mime, QPoint(2 * mid - band, 300));
    QCOMPARE(win.incognito, bandIsSave);
  }

  // The chat dock accepts drops, so Qt makes IT the drag's target and the window is sent no
  // move at all — the lit half must still follow the pointer across it.
  void aDragOverTheChatDockStillMovesTheLitHalf() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    showWithChat(win);
    QVERIFY(win.dropZones && win.chatDock);
    QVERIFY2(win.chatDock->geometry().right() < win.width() / 2,
             "the chat dock must sit left of the window midline for this case to mean anything");

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(guiTestImage())});
    dragTo(win, mime, QPoint(win.width() - 40, 300), QEvent::DragEnter);
    QVERIFY2(!win.dropZones->getActiveLeft(), "the drag started over the incognito half");
    const QPoint overDock = win.chatDock->geometry().center();
    dragTo(win, mime, overDock, QEvent::DragMove);
    QVERIFY2(win.dropZones->getActiveLeft(),
             "the dock swallowed the move and left the zones on the wrong half");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.dropZones.gui.moc"
