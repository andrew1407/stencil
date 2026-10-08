// MainWindow GUI e2e — Closing the project this window holds: the Close Project action (Alt+Shift+W)
// and the open project dropped on the Projects window's Close, each asked about first.
// Browser twins: tests/ui/bindings/keys/closeProject.test.js, tests/ui/projects/list/dragClose.test.js.
#include "projectsHeld.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // The window's list is private to it; this suite is its friend.
  static bool hasProject(const MainWindow& win, const QString& id) {
    for (const auto& p : win.projectList)
      if (QString::fromStdString(p.meta.id) == id) return true;
    return false;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The shared hotkeysConfig.json closeProject: a Project-menu action right after Clear Project, listed
  // there in the Shortcuts window too, and with nothing open it only says so.
  void closeProjectIsAProjectMenuActionOnAltShiftW() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QAction* close = win.acts.closeProject;
    QVERIFY(close);
    QCOMPARE(close->text(), QStringLiteral("Close Project"));
    QCOMPARE(close->shortcut(), QKeySequence(QStringLiteral("Alt+Shift+W")));
    QCOMPARE(win.keys.actions.value(QStringLiteral("closeProject")), close);
    QCOMPARE(win.keys.order.indexOf(QStringLiteral("closeProject")),
             win.keys.order.indexOf(QStringLiteral("clearProject")) + 1);
    QCOMPARE(win.keys.labels.value(QStringLiteral("closeProject")), QStringLiteral("Close Current Project"));
    QMenu* project = nullptr;
    for (QAction* a : win.menuBar()->actions())
      if (a->menu() && a->text().remove(QLatin1Char('&')) == QLatin1String("Project")) project = a->menu();
    QVERIFY(project);
    QCOMPARE(project->actions().indexOf(close), project->actions().indexOf(win.acts.clearProject) + 1);

    QVERIFY(win.activeProjectId.isEmpty());
    Asked unexpected;
    QPointer<QTimer> refuse = answerClose(QStringLiteral("Cancel"), &unexpected);
    close->trigger();
    delete refuse.data();
    QVERIFY2(unexpected.title.isEmpty(), "nothing open, nothing asked");
    QLabel* toast = win.notify->toasts()->lastToast();
    QVERIFY2(toast && toast->text().contains(QStringLiteral("No project is open")), "…only said");
  }

  // The action asks first — not as a danger, naming the project and that it stays saved — and Close
  // leaves an empty editor with the project still in Projects.
  void closeProjectAsksThenLeavesAnEmptyEditor() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const QString id = win.activeProjectId;
    QVERIFY2(!id.isEmpty(), "the opened picture became this window's project");

    answerClose(QStringLiteral("Cancel"));
    win.acts.closeProject->trigger();
    QCOMPARE(win.activeProjectId, id);
    QVERIFY2(canvas->hasImage(), "Cancel closed nothing");

    Asked asked;
    answerClose(QStringLiteral("Close"), &asked);
    win.acts.closeProject->trigger();
    QCOMPARE(asked.title, QStringLiteral("Close project?"));
    QVERIFY2(asked.message.contains(QStringLiteral("stays saved in Projects")), qPrintable(asked.message));
    QVERIFY2(!asked.danger, "closing removes nothing, so it is no danger");
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    QVERIFY(win.activeProjectId.isEmpty());
    QVERIFY2(hasProject(win, id), "the project stays in Projects");
  }

  // Only the project this window holds arms Close: another row dropped there does nothing, the open
  // one is closed here at once, unasked, with its notice, and the Projects window stays up.
  void openProjectDroppedOnCloseClosesItHere() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog drags need the offscreen platform");
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const QString open = win.activeProjectId;
    QVERIFY(!open.isEmpty());
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkMagenta);
    const QString other = win.parts.chatAppliers.addImageProjectEntry(img, "not-open-here");
    QVERIFY(!other.isEmpty());

    bool otherInert = false, closed = false, stayed = false, noticed = false;
    Asked asked;
    QTimer::singleShot(0, [&] {
      ProjectsWindow w = projectsWindow();
      if (!w.list || !w.pill) {
        if (w.dlg) w.dlg->reject();
        return;
      }
      const QPoint onPill = w.pill->mapToGlobal(w.pill->rect().center());
      Asked unexpected;
      QPointer<QTimer> refuse = answerClose(QStringLiteral("Cancel"), &unexpected);
      w.list->setCurrentItem(rowFor(w.list, other));
      w.list->onDragStart();
      holdAt(onPill);
      w.list->onDragEnd();
      w.list->onDragOut(w.list->currentRow());
      QTest::qWait(150);
      delete refuse.data();
      otherInert = unexpected.title.isEmpty() && win.activeProjectId == open && w.dlg->isVisible();

      w.list->setCurrentItem(rowFor(w.list, open));
      w.list->onDragStart();
      holdAt(onPill);
      QPointer<QTimer> refuseToo = answerClose(QStringLiteral("Cancel"), &asked);
      w.list->onDragEnd();
      w.list->onDragOut(w.list->currentRow());
      settle([&] { return win.activeProjectId.isEmpty(); }, 3000);
      delete refuseToo.data();
      closed = win.activeProjectId.isEmpty() && hasProject(win, open);
      stayed = w.dlg->isVisible();
      QLabel* toast = win.notify->toasts()->lastToast();
      noticed = toast && toast->text().contains(QStringLiteral("Closed"));
      w.dlg->reject();
    });
    win.parts.projects.openProjects();
    QVERIFY2(otherInert, "a project not open here leaves Close inert");
    QVERIFY2(asked.title.isEmpty(), "the drop asks nothing");
    QVERIFY2(closed, "the open project closed here and stays in Projects");
    QVERIFY2(noticed, "…with the Closed notice");
    QVERIFY2(stayed, "the Projects window stayed up");
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsClose.gui.moc"
