// MainWindow GUI e2e — Losing the keyboard closes the popover, a nested dialog does not, and it plays once.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Losing the keyboard closes it too, a nested dialog taking that focus does not, and the
  // whole lifecycle plays once — one open, one close, nothing re-shown.
  void accentPopoverClosesOnFocusLossAndPlaysOnce() {
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

    // Losing the KEYBOARD closes it too — the path that survives when the window system
    // The popover lives in this window, so an app-focus loss is what ends it (a click
    // elsewhere in the window is the press rule's job). The exemption: a NESTED dialog
    // the popover opened took that focus for us.
    bool deactClosedSticky = false, deactKeptWithNested = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      if (pop) {
        auto nested = std::make_unique<QDialog>(pop);
        nested->resize(120, 80);
        nested->show();
        QTest::qWait(30);
        QEvent deact1(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact1);   // …handing focus to OUR nested window
        deactKeptWithNested = win.pop_.active && !win.pop_.active->isHidden();
        nested->close();
        nested.reset();
        QTest::qWait(30);
        QEvent deact2(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact2);   // …now the app really lost focus
        deactClosedSticky = !win.pop_.active || win.pop_.active->isHidden();
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QContextMenuEvent ctx2(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx2);
    QVERIFY2(deactKeptWithNested,
             "a nested window's activation must not close the popover under it");
    QVERIFY2(deactClosedSticky, "losing focus must close even a STICKY popover");
    QVERIFY(!win.pop_.active);

    // One open, one close, both animated, nothing re-shown. The popover is a CHILD
    // widget of the main window, so its grow/shrink are ordinary in-window animations.
    // (Animations are off for the suite; this case needs them.)
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] {
      if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    });
    // The lifecycle as it happens, in order: the popover's own show/hide, the particle
    // dust flights execMaybePopover plays over the hosting overlay, and any ghost the
    // reveal machinery might fly instead — there must be none.
    struct Trace : QObject {
      QStringList seq;
      QSet<QObject*> dialogs;
      QElapsedTimer clock;
      qint64 pressedAt = -1, hidAt = -1;
      // DisintegrateOverlay flights actually launched, before/after the outside press.
      int dustOpening = 0, dustClosing = 0;
      bool dismissed = false;
      bool eventFilter(QObject* o, QEvent* e) override {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && w->objectName() == QLatin1String("accentPopover")) {
          if (e->type() == QEvent::Show) { seq << QStringLiteral("show"); dialogs.insert(o); }
          if (e->type() == QEvent::Hide) {
            seq << QStringLiteral("hide");
            hidAt = clock.isValid() ? clock.elapsed() : -1;
          }
        }
        if (w && w->objectName() == QLatin1String(stencil::gui::DisintegrateOverlay::OBJECT_NAME)
            && e->type() == QEvent::Show) {
          if (dismissed) ++dustClosing; else ++dustOpening;
        }
        if (w && w->objectName() == QLatin1String("stencilModalGhost") &&
            e->type() == QEvent::Show)
          seq << QStringLiteral("ghost");
        return false;
      }
    } trace;
    trace.clock.start();
    qApp->installEventFilter(&trace);
    const auto removeTrace = qScopeGuard([&] { qApp->removeEventFilter(&trace); });

    // The logo is not an outside target any more (the block above), so the lifecycle is
    // traced on the canvas and a toolbar icon.
    for (QWidget* target : {static_cast<QWidget*>(win.canvas_), static_cast<QWidget*>(other)}) {
      trace.seq.clear();
      trace.dialogs.clear();
      trace.pressedAt = trace.hidAt = -1;
      trace.dustOpening = trace.dustClosing = 0;
      trace.dismissed = false;
      bool wasTopLevel = true, hadOverlay = false;
      QTimer::singleShot(400, &win, [&] {   // …once the open animation has landed
        const QPoint local = target->rect().center();
        const QPoint at = target->mapToGlobal(local);
        trace.pressedAt = trace.clock.elapsed();
        // THE property: no window of its own. That is what made three animated closes
        // invisible, and what the in-window overlay fixes.
        if (win.pop_.active) wasTopLevel = win.pop_.active->isWindow();
        hadOverlay = win.pop_.overlay && win.pop_.overlay->isVisible();
        trace.dismissed = true;
        QMouseEvent pr(QEvent::MouseButtonPress, local, at, Qt::LeftButton, Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(target, &pr);
        QMouseEvent rl(QEvent::MouseButtonRelease, local, at, Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(target, &rl);
      });
      QContextMenuEvent open(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
      QApplication::sendEvent(logo, &open);
      QTest::qWait(700);   // past both deferred-click delays and the closing flight
      const QString what = QStringLiteral("dismiss on %1").arg(target->objectName().isEmpty()
                                                                   ? target->metaObject()->className()
                                                                   : target->objectName());
      const QString seq = trace.seq.join(QLatin1Char(','));
      QCOMPARE(trace.dialogs.size(), 1);   // ONE popover instance, never a second
      QVERIFY2(trace.seq.count(QStringLiteral("show")) == 1,
               qPrintable(QString("%1: shown %2x — something re-showed it (%3)")
                              .arg(what).arg(trace.seq.count(QStringLiteral("show"))).arg(seq)));
      QVERIFY2(trace.seq.count(QStringLiteral("hide")) == 1,
               qPrintable(QString("%1: hidden %2x (%3)")
                              .arg(what).arg(trace.seq.count(QStringLiteral("hide"))).arg(seq)));
      // No ghost at all: the popover animates itself now, and a ghost is the thing that
      // could be revealed from under a window and read as a re-open.
      QVERIFY2(!seq.contains(QLatin1String("ghost")),
               qPrintable(QString("%1: a ghost was flown for an in-window popover (%2)")
                              .arg(what, seq)));
      QVERIFY2(seq == QLatin1String("show,hide"),
               qPrintable(QString("%1: unexpected lifecycle — got %2").arg(what, seq)));
      // Never a show AFTER a hide: nothing is re-shown, by construction.
      QVERIFY2(!seq.contains(QLatin1String("hide,show")),
               qPrintable(QString("%1: the popover was re-shown after closing (%2)")
                              .arg(what, seq)));
      // …and it is a CHILD WIDGET, hosted by an in-window overlay.
      QVERIFY2(!wasTopLevel, qPrintable(QString("%1: the popover is still a top-level "
                                                "window — nothing will animate it").arg(what)));
      QVERIFY2(hadOverlay, qPrintable(QString("%1: no in-window overlay hosted it").arg(what)));
      // Both edges actually flew the particle dust — the popover no longer grows/shrinks
      // its own box.
      QVERIFY2(trace.dustOpening > 0,
               qPrintable(QString("%1: no dust played on open").arg(what)));
      QVERIFY2(trace.dustClosing > 0,
               qPrintable(QString("%1: no dust played on close").arg(what)));
      QVERIFY(!win.pop_.active);
    }
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeAccentFocus.gui.moc"
