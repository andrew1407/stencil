// MainWindow GUI e2e — opening or closing the docked chat slides the toolbar under a still pointer, so the
// chat icon that was pressed loses the pointer without a Leave: the slide's end un-hovers it, and leaves it
// hovered while the pointer really is over it (support/modal/hoverResync.cpp dropStaleHover).
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/modal/hoverResync.hpp"

#include <QCursor>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void chatIconLetsGoOfAPointerItLost() {
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock->hide();
    settleLayout(&win, 120);
    QWidget* icon = win.buttonForAction(win.acts.chat);
    QVERIFY(icon && icon->isVisible());
    const QPoint away = icon->mapToGlobal(QPoint(-400, 300));
    QCursor::setPos(away);
    icon->setAttribute(Qt::WA_UnderMouse, true);   // the press left it hovered
    win.parts.dockChrome.setChatShown(true, /*animate=*/false);
    QVERIFY2(!icon->underMouse(), "the chat icon kept the hover of a pointer that is not over it");

    icon->setAttribute(Qt::WA_UnderMouse, true);
    win.parts.dockChrome.setChatShown(false, /*animate=*/false);
    QVERIFY2(!icon->underMouse(), "closing leaves it hovered too");

    if (QCursor::pos() != away) QSKIP("this platform cannot place the pointer");
    const QPoint over = icon->mapToGlobal(icon->rect().center());
    QCursor::setPos(over);
    icon->setAttribute(Qt::WA_UnderMouse, true);
    stencil::support::dropStaleHover(icon);
    QVERIFY2(icon->underMouse(), "a pointer really over the icon keeps its hover");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatHover.gui.moc"
