// MainWindow GUI e2e — Dragging a toolbar icon in fullscreen: the revealed rows stay up while the
// drag is live however far the pointer goes, and fold once it drops; a row folded under a live drag
// still lets the icon drop where it is released. Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "iconDragGui.hpp"
#include "iconDrag.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  // Fullscreen with the tool rows revealed under a pointer warped into the top band.
  static bool revealRows(MainWindow& win) {
    win.parts.view.toggleFullscreen();
    QTest::qWait(120);
    const QPoint top = win.mapToGlobal(QPoint(win.width() / 2, 40));
    QCursor::setPos(top);
    if (QCursor::pos() != top) return false;
    win.parts.view.fsHoverTick();
    return QTest::qWaitFor([&win] { return win.fs.barsShown && !win.parts.view.barsAnim; }, 2000);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theRevealedRowsStayUpUntilTheDrop() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    settleLayout(&win, 150);
    if (!revealRows(win)) QSKIP("the pointer cannot be warped here");
    QWidget* icon = win.buttonForAction(win.acts.settings);
    QVERIFY(icon && icon->isVisible());
    stencil::guitest::liftIcon(icon);
    QVERIFY(stencil::support::anyIconDragActive());
    const QPoint low = win.mapToGlobal(QPoint(win.width() / 2, win.height() - 60));
    QCursor::setPos(low);
    win.parts.view.fsHoverTick();
    QTest::qWait(300);
    QVERIFY2(win.fs.barsShown && icon->isVisible(), "a live drag keeps the rows that hold its icon");
    stencil::guitest::ModalSeen seen;
    stencil::guitest::catchModal(seen);
    stencil::guitest::dropIcon(icon, low);
    QTRY_VERIFY2(seen.seen, "the icon dropped away from the rows opens its dialog");
    QTRY_VERIFY2(!win.fs.barsShown, "after the drop the rows fold away from the pointer");
  }

  void anIconWhoseRowFoldsUnderTheDragStillDrops() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    settleLayout(&win, 150);
    QWidget* icon = win.buttonForAction(win.acts.settings);
    QVERIFY(icon && icon->isVisible());
    stencil::guitest::liftIcon(icon);
    QWidget* row = icon->parentWidget();
    row->hide();
    const QPoint drop = win.scroll->mapToGlobal(QPoint(win.scroll->width() / 3, win.scroll->height() / 3));
    stencil::guitest::ModalSeen seen;
    stencil::guitest::catchModal(seen);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(win.mapFromGlobal(drop)), QPointF(drop),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&win, &release);
    QTRY_VERIFY2(seen.seen, "the release the window took still drops the icon");
    QVERIFY(!stencil::support::iconDragActive(icon));
    row->show();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarDragsFullscreen.gui.moc"
