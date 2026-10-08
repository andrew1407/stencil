// MainWindow GUI e2e — What a toolbar drag shows on its way (app/drag): a dialog or the chat a drop
// opens forms out of the cursor while a click's still forms out of its icon, and no tip is up while
// an icon is dragged — the one showing as it starts goes, none comes until it ends.
// Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../MainWindowFlight.gui.hpp"
#include "iconDragGui.hpp"
#include "iconDrag.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static void showWindow(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
  }
  static bool near(const QPoint& a, const QPoint& b) { return (a - b).manhattanLength() <= 3; }

  // Closed once its opening flight has formed (or after a beat), so the flight is the open's.
  static void closeModalOnceFlown(const RevealOriginWatcher& watch, bool& seen) {
    auto* poll = new QTimer(qApp);
    poll->setInterval(5);
    QElapsedTimer clock;
    clock.start();
    QObject::connect(poll, &QTimer::timeout, poll, [poll, &watch, &seen, clock] {
      if (clock.elapsed() > 5000) return poll->deleteLater();
      auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (!dlg || (!watch.captured && clock.elapsed() < 1500)) return;
      seen = true;
      poll->deleteLater();
      dlg->reject();
    });
    poll->start();
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aDroppedDialogFormsOutOfTheCursorAndAClickedOneOutOfItsIcon() {
    const auto motion = withMotion();
    if (stencil::support::motionReduced()) QSKIP("motion is reduced on this run");
    MainWindow win(nullptr, /*restoreLast=*/false);
    showWindow(win);
    QWidget* icon = win.buttonForAction(win.acts.shortcuts);
    QVERIFY(icon);
    const QPoint drop = win.scroll->mapToGlobal(QPoint(150, 120));
    RevealOriginWatcher watch;
    qApp->installEventFilter(&watch);
    bool seen = false;
    closeModalOnceFlown(watch, seen);
    dragIcon(icon, drop, drop);
    QTRY_VERIFY(seen);
    QVERIFY2(watch.captured && near(watch.origin, win.mapFromGlobal(drop)), "the dropped dialog formed elsewhere");
    QTRY_VERIFY(!QApplication::activeModalWidget());
    awaitFlights(&win);

    watch.reset();
    seen = false;
    closeModalOnceFlown(watch, seen);
    QTest::mouseClick(icon, Qt::LeftButton);
    QTRY_VERIFY(seen);
    qApp->removeEventFilter(&watch);
    QVERIFY2(watch.captured && near(watch.origin, flightPointOf(icon, &win)), "a clicked dialog left its icon");
    QTRY_VERIFY(!QApplication::activeModalWidget());
    awaitFlights(&win);
  }

  void aChatFloatedByADropFormsOutOfTheCursor() {
    const auto motion = withMotion();
    if (stencil::support::motionReduced()) QSKIP("motion is reduced on this run");
    MainWindow win(nullptr, /*restoreLast=*/false);
    showWindow(win);
    QVERIFY(!win.chatDock->isVisible());
    const QPoint drop = win.scroll->mapToGlobal(QPoint(win.scroll->width() / 2, win.scroll->height() / 2));
    RevealOriginWatcher watch;
    qApp->installEventFilter(&watch);
    dragIcon(win.buttonForAction(win.acts.chat), drop, drop);
    QTRY_VERIFY(watch.captured);
    qApp->removeEventFilter(&watch);
    QVERIFY(win.chatDock->isFloating());
    QVERIFY2(near(watch.origin, win.mapFromGlobal(drop)), "the dropped chat formed elsewhere");
    awaitFlights(&win);
  }

  void noTipIsUpWhileAnIconIsDragged() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    QWidget* canvasTip = win.overlays.tooltip;
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY(canvasTip && tip);
    QWidget* icon = win.buttonForAction(win.acts.zoomIn);
    QWidget* other = win.buttonForAction(win.acts.zoomOut);
    QVERIFY(icon && other && !other->toolTip().isEmpty());
    const auto ask = [other] {
      QHelpEvent help(QEvent::ToolTip, other->rect().center(), iconCentre(other));
      QApplication::sendEvent(other, &help);
    };
    canvasTip->show();
    liftIcon(icon);
    QVERIFY2(!canvasTip->isVisible(), "the canvas tip stayed up as the drag started");
    ask();
    QVERIFY2(!tip->isVisible() || tip->fadingOut(), "a control's tip showed under the drag");
    dropIcon(icon, iconCentre(icon));
    ask();
    QVERIFY2(tip->isVisible() && !tip->fadingOut(), "tips never came back after the drag");
    tip->hideTip();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarDragsFlight.gui.moc"
