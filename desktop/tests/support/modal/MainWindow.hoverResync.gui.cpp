// MainWindow GUI e2e — Hover after a modal loop: the widget a still pointer rests on is entered
// and its cursor put back on the window once a dialog or a popover closes over it. Shared ground
// is in MainWindow.gui.hpp; the resync itself is support/modal/hoverResync.cpp.
#include "../../MainWindow.gui.hpp"
#include "hoverResync.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  // Counts the cursor the window is handed: macOS repaints the pointer only on a change.
  struct CursorChanges : QObject {
    int seen = 0;
    bool eventFilter(QObject*, QEvent* e) override {
      if (e->type() == QEvent::CursorChange) ++seen;
      return false;
    }
  };

  // A modal over the window, closed by Escape as a user would; exec() returns once it is down.
  static void runModal(MainWindow& win, const std::function<void()>& beforeClose = {}) {
    QDialog dlg(&win);
    dlg.resize(240, 160);
    QTimer::singleShot(40, &dlg, [&dlg, beforeClose] {
      if (beforeClose) beforeClose();
      QTest::keyClick(&dlg, Qt::Key_Escape);
    });
    dlg.exec();
  }

  static void showWindow(MainWindow& win) {
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Qt never entered the logo (no platform Enter came); the resync does, so its hover plays.
  void theResyncEntersTheWidgetQtLeftUnhovered() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    showWindow(win);
    QToolButton* logo = win.logoBtn;
    QCursor::setPos(logo->mapToGlobal(logo->rect().center()));
    QVERIFY2(!logo->underMouse(), "no platform Enter was sent, so nothing is hovered yet");
    stencil::support::resyncHover(&win);
    QVERIFY2(logo->underMouse(), "the logo under the pointer was never entered");
    QVERIFY2(win.logoFx->property("fxActive").toBool(), "and its hover never started");
  }

  // The shape a closed dialog left is replaced by the plain spot's, and handed over afresh even
  // where Qt thinks the window already wears it — the screen may still show the dialog's.
  void aClosedModalHandsTheCursorBackAfresh() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    showWindow(win);
    QWidget* plain = win.imageSizeInfo;   // the size line: text, nothing to press
    QVERIFY(plain && plain->isVisible());
    QCOMPARE(plain->cursor().shape(), Qt::ArrowCursor);
    QCursor::setPos(plain->mapToGlobal(plain->rect().center()));
    win.windowHandle()->setCursor(Qt::PointingHandCursor);
    CursorChanges changes;
    win.windowHandle()->installEventFilter(&changes);
    runModal(win, [&changes] { changes.seen = 0; });   // only what the close hands over
    QTRY_VERIFY2(changes.seen >= 2, "the close never re-applied the cursor");
    QCOMPARE(win.windowHandle()->cursor().shape(), Qt::ArrowCursor);
  }

  // The in-window popover: Escape takes it down, and the pointer's cursor is handed over afresh.
  void aClosedPopoverHandsTheCursorBackAfresh() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    showWindow(win);
    QToolButton* icon = nullptr;
    for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it)
      if (it.value() == win.actSettings) icon = static_cast<QToolButton*>(it.key());
    QVERIFY(icon);
    QWidget* plain = win.imageSizeInfo;
    CursorChanges changes;
    bool opened = false;
    QTimer::singleShot(40, &win, [&] {
      opened = win.pop.active;
      QCursor::setPos(plain->mapToGlobal(plain->rect().center()));
      win.windowHandle()->installEventFilter(&changes);
      if (win.pop.active) QTest::keyClick(win.pop.active, Qt::Key_Escape);
    });
    win.pop.anchor = icon;
    win.actSettings->trigger();
    QVERIFY2(opened, "the icon's popover never opened");
    QTRY_VERIFY2(changes.seen >= 2, "the close never re-applied the cursor");
    QCOMPARE(win.windowHandle()->cursor().shape(), Qt::ArrowCursor);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.hoverResync.gui.moc"
