// MainWindow GUI e2e — Where a reveal flies from (the anchor, the triggered icon, the colour picker), and
// that one out of a floating chat flies above it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // pickColorAnimated is the getColor drop-in behind every colour swatch: the same modal contract plus
  // the revealDialog flight out of the anchor icon, with the animation re-enabled for the origin.
  void pickColorAnimatedMatchesGetColorAndFliesFromItsAnchor() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    QToolButton* icon = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->isVisible() && b->property("toolSection").isValid()) { icon = b; break; }
    QVERIFY2(icon, "no visible toolbar icon to anchor on");
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    // exec() blocks, so a 0-timer drives the modal: the watcher catches the flight's origin the instant
    // it starts (QColorDialog is past the dust size ceiling, so this is the ghost).
    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    QTimer::singleShot(0, [&] {
      for (int i = 0; i < 200; ++i) {
        if (auto* dlg = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
          // revealDialog's own 0-timer (registered after this one) needs a turn to fire and start the OPEN
          // flight before accept() closes the dialog and starts the close flight instead.
          QTest::qWait(50);
          dlg->setCurrentColor(QColor("#12ab34"));
          dlg->accept();
          return;
        }
        QTest::qWait(5);
      }
    });
    const QColor picked =
        stencil::support::pickColorAnimated(QColor("#ffffff"), &win, "Test colour", icon);
    QCOMPARE(picked, QColor("#12ab34"));
    const QPoint want = flightPointOf(icon, &win);
    QVERIFY2(watcher.origin == want,
             qPrintable(QString("the picker's flight starts at %1, the anchor icon is at %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(want))));
    // Cancel path: reject → invalid colour, exactly the getColor contract call sites rely on.
    QTimer::singleShot(0, [] {
      for (int i = 0; i < 200; ++i) {
        if (auto* dlg = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
          dlg->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    const QColor cancelled =
        stencil::support::pickColorAnimated(QColor("#ffffff"), &win, "Test colour", icon);
    QVERIFY2(!cancelled.isValid(), "cancel must return an invalid QColor");
  }

  // …and the anchor that feeds it follows whatever was triggered last, including actions
  // with no icon of their own — those must CLEAR it, not leave the previous icon in place.
  void revealAnchorFollowsTheTriggeredIcon() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(60, 40, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());   // rotate needs an image to be enabled
    settleLayout(&win, 150);
    QAction* first = win.acts.rotateLeft;
    QAction* second = win.acts.rotateRight;
    QVERIFY(first && second);
    QWidget* b1 = win.buttonForAction(first);
    QWidget* b2 = win.buttonForAction(second);
    QVERIFY2(b1 && b2 && b1 != b2, "expected a distinct toolbar button per action");
    first->trigger();
    QCOMPARE(win.pop.dialogAnchor.data(), b1);
    second->trigger();
    QCOMPARE(win.pop.dialogAnchor.data(), b2);
    // An action with no toolbar icon of its own clears the anchor instead of inheriting the last icon —
    // the "it flew out of the wrong button" case. It has no slot either, so nothing opens.
    auto* iconless = new QAction("Menu-only command", &win);
    win.addAction(iconless);
    win.parts.actionsBuilder.bindRevealAnchors();     // idempotent — binds whatever is not bound yet
    win.pop.dialogAnchor = b2;
    iconless->trigger();
    QVERIFY2(!win.pop.dialogAnchor, "an icon-less action left the previous icon as the origin");
  }

  // End-to-end: open a dialog the way a user does and read where its flight STARTS — an icon-backed
  // command flies out of its icon, a menu-only one out of the menu row that was clicked.
  void openingADialogFliesFromWhatWasClicked() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });

    // The watcher catches the flight's origin the instant it starts, whichever mechanism plays it: a
    // ghost's geometry is what a QPropertyAnimation is tweening, so reading it after Show is mid-flight.
    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    // Let the dialog and its flight fully appear, then close it so trigger() returns.
    const auto closeSoon = [&] {
      QTimer::singleShot(140, &win, [&] {
        if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
      });
    };

    // ── an icon-backed dialog ──
    QWidget* icon = win.buttonForAction(win.acts.projects);
    QVERIFY2(icon && icon->isVisible(), "the Projects icon is not on the toolbar");
    watcher.reset();
    closeSoon();
    win.acts.projects->trigger();
    settle([&] { return watcher.captured; }, 50);
    {
      const QPoint want = flightPointOf(icon, &win);
      QVERIFY2(watcher.origin == want,
               qPrintable(QString("icon case: flight starts at %1, icon at %2")
                              .arg(QDebug::toString(watcher.origin), QDebug::toString(want))));
    }

    // A menu-only dialog: no icon, so the clicked ROW is the origin. Every real dialog action carries a
    // toolbar icon now, so this synthetic one is bound and wired through the same execMaybePopover path.
    QMenu* help = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>())
      if (m->actions().contains(win.acts.info)) { help = m; break; }
    QVERIFY2(help, "the Help menu was not found");
    auto* act = new QAction("Test Menu-Only Dialog", &win);
    help->addAction(act);
    win.parts.actionsBuilder.bindRevealAnchor(act);
    connect(act, &QAction::triggered, &win, [&win, act] {
      QDialog dlg(&win);
      dlg.resize(300, 200);
      win.execMaybePopover(dlg, act);
    });
    QVERIFY2(!win.buttonForAction(act), "the synthetic action must have no toolbar icon");
    help->popup(win.mapToGlobal(QPoint(60, 40)));
    QVERIFY(QTest::qWaitForWindowExposed(help));
    const QRect row = help->actionGeometry(act);
    QTest::mouseMove(help, row.center());
    QTest::qWait(30);
    help->close();
    watcher.reset();
    closeSoon();
    act->trigger();
    settle([&] { return watcher.captured; }, 50);
    const QRect rowInWin(win.mapFromGlobal(help->mapToGlobal(row.topLeft())), row.size());
    QVERIFY2(watcher.origin == rowInWin.center(),
             qPrintable(QString("menu case: flight starts at %1, row at %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(rowInWin))));
  }

  // The compact chat's "…" ▸ Settings: the chat floats above the window, so a cloud left in the
  // window's own layer passed under it. Both flights ride a tooltip-level window, above a tool window.
  void aCloudOutOfTheFloatingChatFliesAboveIt() {
    using stencil::gui::DisintegrateOverlay;
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(780, 740);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openChatCompact(win.buttonForAction(win.acts.chat));
    QTRY_VERIFY(win.chatCompactShowing());
    QTest::qWait(900);   // past the float's own arrival
    QVERIFY(win.chatDock->isWindow() && win.chatDock->windowType() == Qt::Tool);
    QWidget* more = win.chatDock->moreButton();
    const QPoint origin = more->mapToGlobal(more->rect().center());
    const QList<QWidget*> earlier = win.findChildren<QWidget*>(QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME));
    const auto cloud = [&](bool gathering) -> DisintegrateOverlay* {
      DisintegrateOverlay* found = nullptr;
      for (QWidget* w : win.findChildren<QWidget*>(QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME))) {
        auto* fx = static_cast<DisintegrateOverlay*>(w);
        if (!earlier.contains(w) && fx->surfacePicture().isValid() && fx->gathering() == gathering) found = fx;
      }
      return found;
    };
    const auto verdict = [&](DisintegrateOverlay* fx, const char* which) {
      if (!fx) return QString("%1: no cloud played").arg(which);
      if (!fx->isWindow() || fx->windowType() != Qt::ToolTip)
        return QString("%1: the cloud is a layer of the window, beneath the floating chat").arg(which);
      if (!fx->geometry().contains(origin)) return QString("%1: the cloud does not reach the \"…\"").arg(which);
      return QString();
    };
    QString opened = "the dialog never opened", closed;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      settle([&] { return (dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) != nullptr; }, 3000);
      if (!dlg) return;
      settle([&] { return cloud(true) != nullptr; }, 500);
      opened = verdict(cloud(true), "arrival");
      dlg->reject();
      closed = verdict(cloud(false), "departure");
    });
    win.chatDock->moreRows.settings->trigger();
    QVERIFY2(opened.isEmpty(), qPrintable(opened));
    QVERIFY2(closed.isEmpty(), qPrintable(closed));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motionReveal.gui.moc"
