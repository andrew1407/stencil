// MainWindow GUI e2e — What is NOT an outside press: the logo itself, and presses inside the box or a nested dialog.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // What is NOT an outside press: the logo itself (a sticky popover cycles the accent
  // under it), a press inside the box, and a press in a dialog the popover opened.
  void accentPopoverKeepsOpenInsideAndOnItsLogo() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo && win.canvas_);
    // With a picture loaded, so a canvas press is an ordinary editing press and not the
    // empty canvas's "create a blank image" invitation (another modal dialog).
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::darkCyan);
    win.canvas_->loadFromImage(pic);
    QTRY_VERIFY(win.canvas_->hasImage());
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    // A toolbar icon that is not the logo — and NOT one the open popover's box covers: a
    // press on a covered icon is a press ON the window (the app's own onOpenBox rule), so
    // it is not the "outside press" this case is about. The SETTINGS cluster's ℹ sits at
    // the far end of the last row, clear of a box anchored to the logo.
    QToolButton* other = nullptr;
    for (auto it = win.pop_.buttons.cbegin(); it != win.pop_.buttons.cend(); ++it)
      if (it.value() == win.actInfo_ && static_cast<QWidget*>(it.key())->isVisible())
        other = static_cast<QToolButton*>(it.key());
    QVERIFY2(other, "no visible Help button to press outside on");

    // The logo itself is NOT outside: a left click on it with a STICKY popover up keeps
    // the list open and cycles the accent under it, the ✓ following (browser parity; user
    // report). The peek's own no-op rule is checked in logoAccentPopoverPicksDirectly.
    {
      const QString accentBefore = win.settings_.accentColor;
      const auto& presets = stencil::gui::accentPresets();
      bool opened = false, stayedOpen = false, cycleArmed = false, cycled = false, stillOpen = false, ticked = false;
      QTimer::singleShot(120, &win, [&] {
        QDialog* pop = win.pop_.active.data();
        opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
        const QPoint local = logo->rect().center();
        const QPoint at = logo->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(logo, &press);
        QMouseEvent rel(QEvent::MouseButtonRelease, local, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(logo, &rel);
        stayedOpen = win.pop_.active && !win.pop_.active->isHidden();
        cycleArmed = win.logoClickTimer_->isActive();
        settle([&] { return win.settings_.accentColor != accentBefore; }, 320);
        cycled = win.settings_.accentColor != accentBefore &&
                 std::any_of(presets.begin(), presets.end(),
                             [&](const auto& a) { return a.key == win.settings_.accentColor; });
        stillOpen = win.pop_.active && !win.pop_.active->isHidden();
        if (pop) {
          auto* r = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + win.settings_.accentColor);
          ticked = r && r->property("currentAccent").toBool();
        }
        if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
      });
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
      QApplication::sendEvent(logo, &ctx);      // blocks in exec until the timer acts
      QVERIFY2(opened, "sticky + logo click: the popover never opened");
      QVERIFY2(stayedOpen, "a left press on the logo must not close its sticky popover");
      QVERIFY2(cycleArmed, "the logo click must still arm the accent cycle");
      QVERIFY2(cycled, "the deferred click must cycle the accent under the open popover");
      QVERIFY2(stillOpen, "the popover must survive the accent change");
      QVERIFY2(ticked, "the popover's tick must follow the cycled accent");
      QVERIFY(!win.pop_.active);
      auto restore = win.settings_;
      restore.accentColor = accentBefore;
      win.applySettings(restore, true);
    }

    // …and the other half of the rule: a press INSIDE the popover, or in a NESTED dialog
    // the popover opened (a confirm, a colour picker), leaves it alone — and Escape
    // still closes it.
    bool insideKept = false, nestedKept = false, escapeClosed = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      if (pop) {
        const auto pressAt = [](QWidget* w) {
          const QPoint local = w->rect().center();
          QMouseEvent press(QEvent::MouseButtonPress, local, w->mapToGlobal(local),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(w, &press);
        };
        pressAt(pop);
        insideKept = win.pop_.active && !win.pop_.active->isHidden();
        // A nested dialog on top of the popover: its presses belong to another window.
        auto nested = std::make_unique<QDialog>(pop);
        nested->setObjectName(QStringLiteral("nestedOverPopover"));
        nested->resize(120, 80);
        nested->show();
        QTest::qWait(30);
        pressAt(nested.get());
        nestedKept = win.pop_.active && !win.pop_.active->isHidden() &&
                     win.pop_.active.data() == pop;
        nested->close();
        QTest::qWait(30);
        QTest::keyClick(pop, Qt::Key_Escape);
        escapeClosed = !win.pop_.active || win.pop_.active->isHidden();
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);
    QVERIFY2(insideKept, "a press INSIDE the popover must not dismiss it");
    QVERIFY2(nestedKept, "a press in a NESTED dialog must not dismiss the popover under it");
    QVERIFY2(escapeClosed, "Escape must still close the popover");
    QVERIFY(!win.pop_.active);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeAccentKeep.gui.moc"
