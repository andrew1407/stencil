// MainWindow GUI e2e — the compact "mini window" a dialog icon opens (MainWindow::
// execMaybePopover). It is the SAME QDialog the menus open as a window, reparented into the
// popover overlay as a plain Qt::Widget child — so the modal shell's header drag, which moves
// a real window, must not move this one: its move() would read the drag's global points as
// parent-relative and throw the panel out of the overlay it is anchored to.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

#include <QContextMenuEvent>
#include <QMouseEvent>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Right-click on a dialog icon is the popover gesture (MainWindowToolbarSections
  // wirePopover). The open blocks in a nested loop, so the drag runs from a timer inside it.
  void compactPopoverStaysPutWhenItsHeaderIsDragged() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QToolButton* icon = nullptr;
    for (auto it = win.pop_.buttons.cbegin(); it != win.pop_.buttons.cend(); ++it) {
      auto* b = static_cast<QToolButton*>(it.key());
      if (b->isVisible() && it.value()->isEnabled()) { icon = b; break; }
    }
    QVERIFY2(icon, "the tool rows must carry at least one popover-wired dialog icon");

    bool sawPopover = false, wasWindow = true;
    QRect before, after;
    QTimer::singleShot(250, &win, [&] {
      QDialog* dlg = win.pop_.active;
      QWidget* overlay = win.pop_.overlay;
      if (!dlg || !overlay) { win.dismissPopover(); return; }
      sawPopover = true;
      wasWindow = dlg->isWindow();

      QWidget* header = dlg->findChild<QWidget*>(QStringLiteral("modalHeader"));
      if (!header) { win.dismissPopover(); return; }
      before = dlg->geometry();   // the DIALOG is what move() shifts, inside its overlay

      // A real drag of the header: press on it, travel, release.
      const QPoint grab = header->mapToGlobal(QPoint(30, 12));
      const QPointF local(30, 12);
      QMouseEvent press(QEvent::MouseButtonPress, local, QPointF(grab), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(header, &press);
      for (const QPoint step : {QPoint(30, 20), QPoint(90, 70), QPoint(160, 120)}) {
        QMouseEvent move(QEvent::MouseMove, local, QPointF(grab + step), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(header, &move);
      }
      QMouseEvent release(QEvent::MouseButtonRelease, local, QPointF(grab + QPoint(160, 120)),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(header, &release);

      after = dlg->geometry();
      win.dismissPopover();
    });

    QContextMenuEvent ctx(QContextMenuEvent::Mouse, icon->rect().center(),
                          icon->mapToGlobal(icon->rect().center()));
    QApplication::sendEvent(icon, &ctx);
    settle([&] { return sawPopover && !win.pop_.active; }, 4000);

    QVERIFY2(sawPopover, "right-click on a dialog icon must open the compact popover");
    QVERIFY2(!wasWindow, "the popover is a child of the overlay, never its own window");
    QCOMPARE(after, before);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.popover.gui.moc"
