// MainWindow GUI e2e — the side panel's and the docked chat's resize grips keep under what floats over the
// window: a toolbar popover and a dialog's blurred backdrop (app/chrome/DockChrome.cpp raiseUnderBackdrop).
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/modal/ModalBackdrop.hpp"

#include <QDialog>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A toolbar popover and a dialog's blurred backdrop cover the resize grips: re-placed after either is up
  // (a relayout under the popover), the grips stay under it.
  void resizeGripsStayUnderAModalBackdrop() {
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock->show();
    settleLayout(&win, 120);
    const auto gripsUnder = [&win](QWidget* cover, const char* what) {
      win.parts.dockChrome.positionPanelGrip();
      win.parts.dockChrome.positionChatEdge();
      const QObjectList& order = win.children();   // back to front
      for (QWidget* g : {static_cast<QWidget*>(win.parts.dockChrome.panelGrip), static_cast<QWidget*>(win.parts.dockChrome.chatEdge)})
        if (g && g->isVisible()) QVERIFY2(order.indexOf(g) < order.indexOf(cover), what);
    };
    QWidget popover(&win);   // stands in for MainWindow::execMaybePopover's overlay
    popover.setObjectName(QStringLiteral("popoverOverlay"));
    popover.show();
    popover.raise();
    gripsUnder(&popover, "a grip painted over a toolbar popover");
    QDialog dlg(&win);
    auto* bd = stencil::support::ModalBackdrop::behind(&dlg, &win);
    QVERIFY(bd);
    gripsUnder(bd, "a grip painted over the dialog's backdrop");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.dockGrips.gui.moc"
