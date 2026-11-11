// MainWindow GUI e2e — a click on another toolbar icon while a mini window is up (browser parity):
// the mini window closes and that icon's own window opens, where it once only closed (user report).
// The press goes through the window's own popover judgement (handlePopoverPress) as a real one does:
// offscreen, a synthetic click reaches the icon by another road.
#include "altPeekGui.hpp"

#include <QDialog>
#include <QMouseEvent>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static QToolButton* buttonFor(MainWindow& win, QAction* act) {
    for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it)
      if (it.value() == act && static_cast<QWidget*>(it.key())->isVisible())
        return static_cast<QToolButton*>(it.key());
    return nullptr;
  }

  // Opens `from`'s mini window, clicks `to` while it is up, and returns the window that came up after.
  static QString clickWhileUp(MainWindow& win, QToolButton* from, QToolButton* to) {
    QString opened, popover;
    QPointer<QDialog> mini;   // closing, it is still the active modal for a moment: never the answer
    QTimer::singleShot(700, &win, [&] {
      mini = win.pop.active.data();
      if (mini) popover = mini->windowTitle();
      win.parts.popoverGestures.handlePopoverPress(to, centreOf(to), Qt::LeftButton);
      QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to->rect().center()), QPointF(centreOf(to)),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(to, &release);
    });
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &win, [&] {
      auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (!modal || modal == mini.data() || modal == win.pop.active.data() || !popover.size()) return;
      opened = modal->windowTitle();
      modal->reject();
    });
    poll.start(50);
    emit from->customContextMenuRequested(from->rect().center());   // blocks while the mini window is up
    QTest::qWait(2000);
    poll.stop();
    if (win.pop.active) win.pop.active->done(QDialog::Rejected);
    return popover.isEmpty() ? QStringLiteral("<no mini window>") : opened;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aClickOnAnotherIconOpensItsWindow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode::PARTICLES);
    QToolButton* shortcuts = buttonFor(win, win.acts.shortcuts);
    QToolButton* projects = buttonFor(win, win.acts.projects);   // in another section, clear of the mini window
    QVERIFY2(shortcuts && projects, "no visible Shortcuts / Projects buttons");
    QVERIFY(!QRect(shortcuts->mapToGlobal(QPoint()), shortcuts->size()).intersects(QRect(projects->mapToGlobal(QPoint()), projects->size())));
    const QString opened = clickWhileUp(win, shortcuts, projects);
    QVERIFY2(!opened.isEmpty() && opened != QLatin1String("<no mini window>"),
             "the click on another icon closed the mini window but opened nothing");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.popoverSwitch.gui.moc"
