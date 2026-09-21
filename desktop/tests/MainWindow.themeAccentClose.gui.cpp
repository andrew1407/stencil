// MainWindow GUI e2e — An outside press closing the accent popover, from both routes.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A popover must close on a click OUTSIDE it, from BOTH routes that open the accent picker, and
  // whether Qt delivers that press or not: exec() is app-modal, so the platform drops some presses.
  void accentPopoverClosesOnOutsidePress() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn;
    QVERIFY(logo && win.canvas);
    // With a picture loaded, so a canvas press is an ordinary editing press and not the
    // empty canvas's "create a blank image" invitation (another modal dialog).
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::darkCyan);
    win.canvas->loadFromImage(pic);
    QTRY_VERIFY(win.canvas->hasImage());
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    // A toolbar icon that is not the logo, and NOT one the open popover's box covers: a press on a
    // covered icon is a press ON the window (onOpenBox). The SETTINGS cluster's ℹ sits clear of it.
    QToolButton* other = nullptr;
    for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it)
      if (it.value() == win.actInfo && static_cast<QWidget*>(it.key())->isVisible())
        other = static_cast<QToolButton*>(it.key());
    QVERIFY2(other, "no visible Help button to press outside on");

    enum Route { STICKY, PEEK };
    // Open the picker by `route`, press outside on `target`, and report whether the
    // popover opened and then went.
    const auto outsidePressCloses = [&](Route route, QWidget* target, const char* what) {
      bool opened = false, closed = false, notModal = false;
      QTimer::singleShot(120, &win, [&] {
        opened = win.pop.active &&
                 win.pop.active->objectName() == QLatin1String("accentPopover") &&
                 win.pop.active->isVisible();
        // THE mechanism: the popover must not be MODAL. An application-modal dialog marks this window
        // blockedByModalWindow, and Qt then drops every press aimed at it before any filter runs.
        notModal = !QApplication::activeModalWidget() && win.pop.active &&
                   !win.pop.active->isModal() && win.isEnabled();
        const QPoint local = target->rect().center();
        const QPoint at = target->mapToGlobal(local);
        // A real press, press + release, exactly as the window system delivers it now
        // that the popover no longer blocks this window.
        QMouseEvent press(QEvent::MouseButtonPress, local, at, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QMouseEvent rel(QEvent::MouseButtonRelease, local, at, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &rel);
        closed = !win.pop.active || win.pop.active->isHidden();
        if (win.pop.active && !win.pop.active->isHidden()) win.pop.active->reject();
      });
      if (route == STICKY) {
        QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
        QApplication::sendEvent(logo, &ctx);      // blocks in exec until the timer acts
      } else {
        logo->setAttribute(Qt::WA_UnderMouse, true);
        QTest::keyPress(&win, Qt::Key_Alt);       // ditto, via the peek
        logo->setAttribute(Qt::WA_UnderMouse, false);
        QTest::keyRelease(&win, Qt::Key_Alt);
      }
      QVERIFY2(opened, qPrintable(QString("%1: the popover never opened").arg(what)));
      QVERIFY2(notModal,
               qPrintable(QString("%1: the popover is application-modal — Qt will drop "
                                  "the outside press before any filter sees it").arg(what)));
      QVERIFY2(closed, qPrintable(QString("%1: the popover survived a press outside it")
                                      .arg(what)));
      QVERIFY(!win.pop.active);
      // The closing click is spent: no icon may re-open what it just dismissed.
      QVERIFY2(!win.pop.clickTimer->isActive(),
               qPrintable(QString("%1: the dismissing click armed a re-open").arg(what)));
      QVERIFY2(!win.logoClickTimer->isActive(),
               qPrintable(QString("%1: the dismissing click armed the accent cycle").arg(what)));
      QTest::qWait(320);   // past both deferred-click delays…
      QVERIFY2(!win.pop.active, qPrintable(QString("%1: it came back").arg(what)));
      // …and the nested loop really unwound: the OUTER loop is running our timers again.
      bool alive = false;
      QTimer::singleShot(0, &win, [&alive] { alive = true; });
      QTRY_VERIFY2(alive, qPrintable(QString("%1: the nested event loop leaked").arg(what)));
    };

    outsidePressCloses(STICKY, win.canvas, "sticky + canvas press");
    outsidePressCloses(STICKY, other, "sticky + toolbar press");
    outsidePressCloses(PEEK, win.canvas, "peek + canvas press");
    outsidePressCloses(PEEK, other, "peek + toolbar press");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeAccentClose.gui.moc"
