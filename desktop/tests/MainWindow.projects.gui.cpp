// MainWindow GUI e2e — The project shortcuts, incognito's promotion to a real project, and clearing one.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"
#include "../src/support/filterFade.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Two project actions the shared hotkeysConfig.json carries: the trash reads "Remove" and answers
  // Ctrl+Alt+R, and Ctrl+Alt+N opens the toolbar name field for inline editing.
  void projectRemoveAndRenameShortcuts() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    QCOMPARE(clear->shortcut(), QKeySequence("Ctrl+Alt+R"));
    QVERIFY2(clear->toolTip().startsWith("Remove"), qPrintable("trash tooltip: " + clear->toolTip()));

    QAction* rename = actionByText(&win, "Rename Project");
    QVERIFY2(rename, "Rename Project is discoverable as an action, not just a chord");
    QCOMPARE(rename->shortcut(), QKeySequence("Ctrl+Alt+N"));
    // Gated by the name field itself: a fresh window has no project, so nothing to rename.
    QVERIFY(!win.nameBar.field->isEnabled());
    QVERIFY(!rename->isEnabled());
    // An active project makes the name editable (updateProjectTitle), and the action follows.
    win.activeProjectId = QStringLiteral("test-project");
    win.refreshActions();
    QVERIFY(win.nameBar.field->isEnabled());
    QVERIFY2(rename->isEnabled(), "the action did not follow the name field");
    QVERIFY(!win.nameBar.editing);
    rename->trigger();
    QTRY_VERIFY2(win.nameBar.editing, "the chord did not enter inline rename");
    QVERIFY(!win.nameBar.field->isReadOnly());
    QCOMPARE(win.nameBar.field, win.focusWidget());
    win.cancelProjectName();
  }

  // Incognito stops the app writing BY ITSELF; an explicit save is the user, not the app — so `save`
  // promotes the session to a local project instead of dead-ending in "saving is disabled".
  void incognitoPromotesToALocalProjectInsteadOfRefusing() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const int before = static_cast<int>(win.projectList.size());
    win.actIncognito->setChecked(true);
    QTRY_VERIFY(win.incognito);

    // The assistant's pathless save: it promotes rather than failing.
    QString err;
    QVERIFY2(win.chatSaveProject(QString(), QString(), &err),
             qPrintable(QStringLiteral("save refused while incognito: %1").arg(err)));
    QVERIFY2(!win.incognito, "the session left incognito with the save");
    QCOMPARE(static_cast<int>(win.projectList.size()), before + 1);
    QVERIFY2(canvas->hasImage(), "the picture survived the promotion");
    beat();
  }

  // Writing a FILE the user named is the toolbar's Save Image…, not project promotion — so it
  // works while incognito, and the picture stays incognito afterwards.
  void incognitoStillWritesAFileTheUserNamed() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.actIncognito->setChecked(true);
    QTRY_VERIFY(win.incognito);
    const int before = static_cast<int>(win.projectList.size());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.path() + QStringLiteral("/from-incognito.png");
    QString err;
    QVERIFY2(win.chatSaveProject(QStringLiteral("shot"), out, &err),
             qPrintable(QStringLiteral("file save refused: %1").arg(err)));
    QVERIFY2(QFileInfo::exists(out), "the file the user asked for is on disk");
    QVERIFY2(win.incognito, "an export is not a promotion — the session stays incognito");
    QCOMPARE(static_cast<int>(win.projectList.size()), before);
    beat();
  }

  // A folder destination gets the file the model names inside it (the empty-Downloads bug:
  // the echo guard demanded the whole path verbatim, so the destination was silently dropped).
  void aNamedFolderTakesTheFileTheAssistantNames() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString err;
    QVERIFY2(win.chatSaveProject(QStringLiteral("portrait-bw"), dir.path(), &err),
             qPrintable(QStringLiteral("folder save refused: %1").arg(err)));
    const QStringList written = QDir(dir.path()).entryList(QDir::Files);
    QCOMPARE(written.size(), 1);
    QVERIFY2(written.first().endsWith(QStringLiteral(".png")),
             "a folder destination writes the picture, named after the save");
    beat();
  }

  // The trash "Clear Project" action (mirrors the browser #clear-storage button) is visible for a
  // local editor, confirms, and on Yes resets to the empty "Open an image" canvas.
  void clearProjectResetsToBlankEditor() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    QVERIFY(clear->isVisible());   // shown for a local/temporary editor (hidden only for server projects)

    dismissModal("OK");      // blocks on the confirm until the timer answers it
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);   // reset to a blank editor
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 0);
    beat();
  }

  // The ⋯-menu Remove confirms IN-DIALOG (deferred a turn, drag-release safe) and the dialog stays
  // open: No keeps the row, Yes removes the project — the user never loses their place.
  void projectsRemoveConfirmsInDialogAndStaysOpen() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkGreen);
    const QString idA = win.addImageProjectEntry(img, "remove-me");
    const QString idB = win.addImageProjectEntry(img, "keep-me");
    QVERIFY(!idA.isEmpty() && !idB.isEmpty());
    const auto hasProject = [&win](const QString& id) {
      for (const auto& p : win.projectList)
        if (QString::fromStdString(p.meta.id) == id) return true;
      return false;
    };

    bool sawRow = false, openAfterNo = false, keptAfterNo = false;
    bool openAfterYes = false, rowGone = false;
    QTimer::singleShot(0, [&] {
      const auto bailOut = [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
      };
      QDialog* dlg = nullptr;
      QListWidget* list = nullptr;
      for (int i = 0; i < 200 && !list; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg) list = dlg->findChild<QListWidget*>("projectsList");
        if (!list) QTest::qWait(10);
      }
      if (!list) { bailOut(); return; }
      const auto rowFor = [&list](const QString& id) -> QListWidgetItem* {
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
        return nullptr;
      };
      QListWidgetItem* item = rowFor(idA);
      if (!item) { bailOut(); return; }
      sawRow = true;
      list->scrollToItem(item);
      // Trigger the ⋯/right-click menu's Remove on the current row: arm a 0-timer to
      // pick it (the menu's exec() blocks), then pop the menu via the real wiring.
      const auto removeViaMenu = [&list](QListWidgetItem* it) {
        list->setCurrentItem(it);
        QTimer::singleShot(0, [] {
          QMenu* menu = nullptr;
          for (int i = 0; i < 200 && !menu; ++i) {
            menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) QTest::qWait(10);
          }
          if (!menu) return;
          for (QAction* a : menu->actions())
            if (a->text() == QLatin1String("Remove")) { a->trigger(); break; }
          menu->close();
        });
        emit list->customContextMenuRequested(list->visualItemRect(it).center());
      };

      // 1. Answer Cancel: the row, the project, and the dialog all stay.
      removeViaMenu(item);
      dismissModal("Cancel");        // the deferred in-dialog styled confirm
      QTest::qWait(400);
      openAfterNo = dlg->isVisible();
      keptAfterNo = rowFor(idA) != nullptr && hasProject(idA);

      // 2. Same remove, answer Confirm: the project goes, the dialog stays open.
      removeViaMenu(rowFor(idA));
      dismissModal("OK");
      QTest::qWait(400);
      openAfterYes = dlg->isVisible() && !hasProject(idA);
      // The scattered row leaves the list once the dust lands (setProjects repaint).
      settle([&] { return !(rowFor(idA)); }, 3000);
      rowGone = !rowFor(idA) && rowFor(idB) != nullptr;
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(openAfterNo, "answering No closed the projects dialog");
    QVERIFY2(keptAfterNo, "answering No still removed the project");
    QVERIFY2(openAfterYes, "answering Yes closed the dialog (or the project survived)");
    QVERIFY2(rowGone, "the removed row never left the still-open list");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projects.gui.moc"
