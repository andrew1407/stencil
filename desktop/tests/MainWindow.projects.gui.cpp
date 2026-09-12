// MainWindow GUI e2e — Projects: the dialog's rows and gestures, the project file on disk,
// live sync and what a session restores.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Two project actions the shared hotkeysConfig.json now carries: the trash reads
  // "Remove" and answers Ctrl+Alt+R, and Ctrl+Alt+N opens the toolbar name field for
  // inline editing — the keyboard route to the ✎ the browser grew at the same time.
  // (shareImage is in that config too, and this app answers it wherever the OS shares —
  // see shareButtonOnlyWhereTheOsShares.)
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
    QVERIFY(!win.nameBar_.field->isEnabled());
    QVERIFY(!rename->isEnabled());
    // An active project makes the name editable (updateProjectTitle), and the action follows.
    win.activeProjectId_ = QStringLiteral("test-project");
    win.refreshActions();
    QVERIFY(win.nameBar_.field->isEnabled());
    QVERIFY2(rename->isEnabled(), "the action did not follow the name field");
    QVERIFY(!win.nameBar_.editing);
    rename->trigger();
    QTRY_VERIFY2(win.nameBar_.editing, "the chord did not enter inline rename");
    QVERIFY(!win.nameBar_.field->isReadOnly());
    QCOMPARE(win.nameBar_.field, win.focusWidget());
    win.cancelProjectName();
  }

  // An incognito session could be published to a SERVER but never kept locally, so the
  // assistant's `save` — and "make this a normal project" — dead-ended in "incognito mode —
  // saving is disabled", stranding the picture in a session that could not be saved at all.
  // Incognito stops the app writing BY ITSELF; an explicit save is the user, not the app.
  void incognitoPromotesToALocalProjectInsteadOfRefusing() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const int before = static_cast<int>(win.projectList_.size());
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY(win.incognito_);

    // The assistant's pathless save: it promotes rather than failing.
    QString err;
    QVERIFY2(win.chatSaveProject(QString(), QString(), &err),
             qPrintable(QStringLiteral("save refused while incognito: %1").arg(err)));
    QVERIFY2(!win.incognito_, "the session left incognito with the save");
    QCOMPARE(static_cast<int>(win.projectList_.size()), before + 1);
    QVERIFY2(canvas->hasImage(), "the picture survived the promotion");
    beat();
  }

  // Writing a FILE the user named is the toolbar's Save Image…, not project promotion — so it
  // works while incognito, and the picture stays incognito afterwards.
  void incognitoStillWritesAFileTheUserNamed() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY(win.incognito_);
    const int before = static_cast<int>(win.projectList_.size());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.path() + QStringLiteral("/from-incognito.png");
    QString err;
    QVERIFY2(win.chatSaveProject(QStringLiteral("shot"), out, &err),
             qPrintable(QStringLiteral("file save refused: %1").arg(err)));
    QVERIFY2(QFileInfo::exists(out), "the file the user asked for is on disk");
    QVERIFY2(win.incognito_, "an export is not a promotion — the session stays incognito");
    QCOMPARE(static_cast<int>(win.projectList_.size()), before);
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

  // The trash "Clear Project" action (mirrors the browser #clear-storage button) is
  // visible for a local editor, confirms, and — on Yes — resets to the empty
  // "Open an image" canvas. The confirm reuses the modal-dismiss helper.
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
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    beat();
  }

  // Opening a .stencil project file (the real OS-open / drag / file-arg path) decodes its
  // embedded image and adopts its layout — image + lines + rotation — into the live canvas.
  void opensStencilProjectFile() {
    // Author a .stencil bundling the test PNG's bytes + a one-line, quarter-rotated layout.
    QByteArray png;
    {
      QFile f(guiTestImage());
      QVERIFY(f.open(QIODevice::ReadOnly));
      png = f.readAll();
    }
    stencil::core::Lines lines;
    stencil::core::Line l;
    l.points = {{10, 10}, {40, 40}};
    l.color = "#ff0000";
    lines.push_back(l);
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "GUI Project";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, lines, "none", "#7c3aed", {}, 1, {});
    const QString path = QDir::temp().filePath("stencil_e2e_project.stencil");
    {
      QFile wf(path);
      QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate));
      wf.write(stencil::gui::fileStore::buildProjectFile(pf));
    }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);   // routes *.stencil -> openProjectFile
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QCOMPARE(canvas->rotationQuarters(), 1);                 // layout rotation adopted
    QVERIFY(totalPoints(canvas) > 0);                        // the line was adopted
    beat();
  }

  // Live sync: a project linked to a .stencil with live-sync ON auto-saves edits back to the
  // file (debounced). Drives openProjectFile linking → the "Live Sync with File" toggle →
  // an edit → onCanvasChanged → scheduleStencilAutosave → flushStencilAutosave writing the file.
  void liveSyncAutosavesEditsToFile() {
    QByteArray png;
    { QFile f(guiTestImage()); QVERIFY(f.open(QIODevice::ReadOnly)); png = f.readAll(); }
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "Live";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, {}, "none", "#7c3aed", {}, 0, {});   // rotation 0
    const QString path = QDir::temp().filePath("stencil_livesync.stencil");
    { QFile wf(path); QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate)); wf.write(stencil::gui::fileStore::buildProjectFile(pf)); }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QCOMPARE(canvas->rotationQuarters(), 0);

    QAction* live = actionByText(&win, "Live Sync with File");
    QVERIFY(live);
    QVERIFY(live->isEnabled());     // enabled because the project is file-linked
    live->setChecked(true);         // toggled → toggleStencilLiveSync(true)

    QAction* rotate = actionByText(&win, "Rotate Right");
    QVERIFY(rotate);
    rotate->trigger();
    QCOMPARE(canvas->rotationQuarters(), 1);

    // Auto-save is debounced (~800ms) — wait for the linked file to reflect the rotation.
    auto fileRotation = [&]() -> int {
      QFile rf(path);
      if (!rf.open(QIODevice::ReadOnly)) return -1;
      stencil::gui::fileStore::ProjectFileData out;
      QString err;
      if (!stencil::gui::fileStore::parseProjectFile(rf.readAll(), out, &err)) return -1;
      int w = 0, h = 0;
      stencil::core::CropRect crop;
      int rot = 0;
      stencil::gui::fileStore::parseLayoutJson(out.layout, w, h, &crop, &rot);
      return rot;
    };
    QTRY_COMPARE_WITH_TIMEOUT(fileRotation(), 1, 4000);   // the edit auto-saved into the linked file
    beat();
  }

  // Deleting the linked .stencil file removes it from disk and unlinks the project (the live-sync
  // + delete actions disable), while the project stays open in the canvas. Drives openProjectFile
  // linking → the "Delete Project File (.stencil)" action → the confirm (auto-clicked "Delete").
  void deletesLinkedProjectFile() {
    QByteArray png;
    { QFile f(guiTestImage()); QVERIFY(f.open(QIODevice::ReadOnly)); png = f.readAll(); }
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "Doomed";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, {}, "none", "#7c3aed", {}, 0, {});
    const QString path = QDir::temp().filePath("stencil_delete.stencil");
    { QFile wf(path); QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate)); wf.write(stencil::gui::fileStore::buildProjectFile(pf)); }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    QAction* del = actionByText(&win, "Delete Project File (.stencil)");
    QVERIFY(del);
    QVERIFY(del->isEnabled());       // enabled because the project is file-linked
    QVERIFY(QFile::exists(path));

    dismissModal("Delete");          // auto-click "Delete" on the confirm modal
    del->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 4000);   // the file was removed from disk
    QVERIFY(!del->isEnabled());      // unlinked → the delete action disables again
    QVERIFY(canvas->hasImage());     // the project itself stays open in the editor
    beat();
  }

  // Pan/zoom persistence (browser parity: storage.js's own debounced scroll/zoom save +
  // "Saved" toast). A zoom change and a scrollbar drag each debounce into ONE save, the
  // active project's record picks up the new view, and reopening that project restores it
  // instead of the plain fit-to-window.
  void panZoomPersistsPerProjectWithSavedToast() {
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId_.isEmpty());
    const QString projectId = win.activeProjectId_;

    // Zoom in past the viewport so the canvas actually grows a scrollable range —
    // otherwise the scrollbars have nowhere to move and the pan half of this test proves
    // nothing.
    win.setZoom(5.0);
    win.scroll_->horizontalScrollBar()->setValue(30);
    win.scroll_->verticalScrollBar()->setValue(20);
    QTest::qWait(600);   // past the 400ms debounce

    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr);
      QCOMPARE(pr->zoomScale, 5.0);
      QCOMPARE(pr->scrollLeft, 30);
      QCOMPARE(pr->scrollTop, 20);
    }
    // The debounced save flashes the same toast every other save does.
    {
      bool sawSaved = false;
      for (QLabel* l : win.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly))
        if (l->property("stencilToastText").toString() == "Saved") sawSaved = true;
      QVERIFY2(sawSaved, "no \"Saved\" toast after the debounced pan/zoom save");
    }

    // Knock the live canvas to a different zoom WITHOUT going through setZoom (which would
    // reschedule — and overwrite — the very save just verified above), simulating "the
    // editor is sitting somewhere else" right before this project reopens.
    canvas->setScale(1.0);
    QVERIFY(win.loadProjectIntoCanvas(projectId, /*animate=*/false));
    QCOMPARE(canvas->scale(), 5.0);
    // The scroll half restores a turn later (QTimer::singleShot(0, …)), so it is the one
    // to wait on — the scale is already back by the time loadProjectIntoCanvas returns.
    QTRY_COMPARE(win.scroll_->horizontalScrollBar()->value(), 30);
    QCOMPARE(win.scroll_->verticalScrollBar()->value(), 20);

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("OK");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

  void clearProjectActionIsDangerAndGated() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QAction* clear = win.actClearProject_;
    QVERIFY(clear);
    // With an image loaded it is live…
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());   // the bare-QImage adoption path
    win.refreshActions();
    QVERIFY2(clear->isEnabled(), "Clear Project is dead with an image loaded");
    // …and dead once there is nothing left to clear (an empty canvas, no project).
    win.canvas_->clearImage();
    win.activeProjectId_.clear();
    win.refreshActions();
    QVERIFY2(!clear->isEnabled(), "Clear Project stays live on an empty editor");
    // The ACTION's glyph is the ordinary menu tone, never danger red: menus paint
    // icons muted (browser .ctx-icon) and the red belongs to the filled toolbar
    // button, which dangerToolButtonsAreFilledRed covers.
    const QImage glyph = clear->icon().pixmap(16, 16).toImage();
    QVERIFY(!glyph.isNull());
    const QColor danger = stencil::gui::themePalette(false).danger;
    const QColor dangerDark = stencil::gui::themePalette(true).danger;
    bool tinted = false;
    for (int y = 0; y < glyph.height() && !tinted; ++y)
      for (int x = 0; x < glyph.width(); ++x) {
        const QColor c = glyph.pixelColor(x, y);
        if (c.alpha() < 40) continue;
        const auto near = [&c](const QColor& d) {
          return qAbs(c.red() - d.red()) < 45 && qAbs(c.green() - d.green()) < 45
              && qAbs(c.blue() - d.blue()) < 45;
        };
        if (near(danger) || near(dangerDark)) { tinted = true; break; }
      }
    QVERIFY2(!tinted, "the Clear Project trash is still painted in the danger colour");
  }

  // Opening a project from the list is gesture-mapped (browser parity):
  //   single click          → confirm, then open in the CURRENT window
  //   double click          → open immediately, NO confirmation
  //   Ctrl/⌘ + single click → confirm, then open in a NEW window
  //   Ctrl/⌘ + double click → new window immediately, NO confirmation
  // The crux is that the single-click open is deferred by doubleClickInterval()
  // and cancelled by the double click, so the confirmation never flashes.
  void projectsListOpenGestures() {
    // Offscreen only (which is how ctest runs this suite). Driving a MODAL
    // dialog with synthetic clicks needs the window server to have activated
    // it; on a real desktop the clicks go nowhere and the flow deadlocks on the
    // still-open modal. Same class of limitation as the fullscreen edge-hover
    // test, which is likewise offscreen-only.
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // A project to click on.
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::magenta);
    const QString id = win.addImageProjectEntry(img, "gesture-target");
    QVERIFY(!id.isEmpty());

    auto mainWindowCount = [] {
      int n = 0;
      for (QWidget* w : QApplication::topLevelWidgets())
        if (qobject_cast<MainWindow*>(w) && w->isVisible()) ++n;
      return n;
    };
    // Watches for the styled in-dialog open-confirm (modalChrome confirmModal —
    // it sits OVER the still-open projects dialog now) and answers it. Matched by
    // objectName: the projects dialog itself is a modal QDialog too.
    struct BoxWatch {
      bool seen = false;
      bool accept = true;
    };
    auto startWatch = [](BoxWatch* w) {
      auto* t = new QTimer;
      t->setInterval(5);
      QObject::connect(t, &QTimer::timeout, t, [w] {
        QWidget* m = QApplication::activeModalWidget();
        if (!m || m->objectName() != QLatin1String("stencilConfirmModal")) return;
        w->seen = true;
        const QLatin1String want = w->accept ? QLatin1String("Open") : QLatin1String("Cancel");
        for (QPushButton* b : m->findChildren<QPushButton*>())
          if (b->text() == want) { b->click(); return; }
      });
      t->start();
      return t;
    };
    // Perform a gesture on the first project row inside the (modal) dialog.
    auto gesture = [&win, &id](bool doubleClick, Qt::KeyboardModifiers mods) {
      QTimer::singleShot(0, [doubleClick, mods, id] {
        // Always leave a way out: if anything below fails to accept the modal,
        // reject it so the blocking openProjects() can return.
        const auto bailOut = [] {
          if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            d->reject();
        };
        QListWidget* list = nullptr;
        QWidget* opener = nullptr;   // the projects dialog: what an opening gesture leaves
        for (int i = 0; i < 200 && !list; ++i) {
          if (auto* dlg = qobject_cast<QDialog*>(opener = QApplication::activeModalWidget()))
            list = dlg->findChild<QListWidget*>("projectsList");
          if (!list) QTest::qWait(10);
        }
        if (!list) { bailOut(); return; }
        // OUR project's row specifically — the store may hold hundreds, and a
        // stale one could fail to load and mask the result.
        QListWidgetItem* item = nullptr;
        for (int i = 0; i < list->count() && !item; ++i)
          if (list->item(i)->data(Qt::UserRole).toString() == id) item = list->item(i);
        if (!item) { bailOut(); return; }
        list->scrollToItem(item);
        settleLayout(list, 30);
        // Aim right of the icon/kebab strips, at the row's text.
        const QRect r = list->visualItemRect(item);
        const QPoint hit(r.left() + r.width() / 2, r.center().y());
        if (doubleClick) {
          // Synthesised directly rather than via QTest::mouseDClick: that helper
          // waits internally between the events, and the dialog accepting
          // mid-sequence leaves it stuck.
          QWidget* vp = list->viewport();
          const QPointF gp = vp->mapToGlobal(hit);
          const auto send = [&](QEvent::Type t) {
            QMouseEvent e(t, QPointF(hit), gp, Qt::LeftButton,
                          t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          mods);
            QApplication::sendEvent(vp, &e);
          };
          send(QEvent::MouseButtonPress);
          send(QEvent::MouseButtonRelease);
          send(QEvent::MouseButtonDblClick);
          send(QEvent::MouseButtonRelease);
        } else {
          QTest::mouseClick(list->viewport(), Qt::LeftButton, mods, hit);
        }
        // Outlast the deferred open so "no dialog" is really observed, but stop early
        // once the gesture HAS opened something.
        settle([opener] { return QApplication::activeModalWidget() != opener; },
               QApplication::doubleClickInterval() + 250);
        bailOut();  // gesture did not open anything → don't hang the test
      });
      win.openProjects();
    };

    const int baseWindows = mainWindowCount();

    // ── 1. single click → confirmation, then opens HERE ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY2(w.seen, "single click did not ask for confirmation");
      QTRY_COMPARE(mainWindowCount(), baseWindows);  // same window
      QVERIFY(win.canvas_->hasImage());
    }

    // ── 2. single click, confirmation DECLINED → nothing opens ──
    win.canvas_->clearImage();
    {
      BoxWatch w;
      w.accept = false;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY(w.seen);
      QVERIFY2(!win.canvas_->hasImage(), "declining the confirmation still opened it");
      QCOMPARE(mainWindowCount(), baseWindows);
    }

    // ── 3. double click → NO confirmation, opens HERE ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(true, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY2(!w.seen, "double click still raised a confirmation dialog");
      QTRY_VERIFY(win.canvas_->hasImage());
      QCOMPARE(mainWindowCount(), baseWindows);
    }

    // ── 4. ⌘ + single click → confirmation, then a NEW window ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::ControlModifier);
      t->stop();
      delete t;
      QVERIFY2(w.seen, "Ctrl/⌘ + single click did not ask for confirmation");
      QTRY_COMPARE(mainWindowCount(), baseWindows + 1);
    }

    // ── 5. ⌘ + double click → NO confirmation, another NEW window ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(true, Qt::ControlModifier);
      t->stop();
      delete t;
      QVERIFY2(!w.seen, "Ctrl/⌘ + double click still raised a confirmation dialog");
      QTRY_COMPARE(mainWindowCount(), baseWindows + 2);
    }

    // Tidy up the windows this test opened.
    for (QWidget* wgt : QApplication::topLevelWidgets())
      if (qobject_cast<MainWindow*>(wgt) && wgt != &win) wgt->close();
    QTRY_COMPARE(mainWindowCount(), baseWindows);
    beat();
  }

  // Removing project rows plays the scatter over an EMPTY slot. The overlay animates a
  // SNAPSHOT, and the list kept painting the ORIGINAL row underneath it — so the removal
  // was never actually seen. Pins the fixed sequence: the real row blanks the instant the
  // removal starts, its slot stays open while the dust falls, and the item leaves the
  // list only once the scatter has played (browser parity: leaveThenRemove +
  // beginRemoval in projectsModal.js). Driven through Clear All — the removal path that
  // keeps the dialog open while the animation runs.
  void projectRemovalBlanksTheRowAndHoldsItsSlot() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "doomed-row");
    QVERIFY(!id.isEmpty());

    bool sawRow = false, blankedAtOnce = false, slotHeld = false, goneAfter = false;
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
      QListWidgetItem* item = nullptr;
      for (int i = 0; i < list->count() && !item; ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) item = list->item(i);
      if (!item) { bailOut(); return; }
      sawRow = true;
      list->scrollToItem(item);
      settleLayout(list, 30);
      const QRect r = list->visualItemRect(item);
      const int rowsBefore = list->count();
      const QImage before = list->viewport()->grab(r).toImage();

      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!clearBtn) { bailOut(); return; }
      // click() is synchronous (like trigger()): dismissModal's 0-timer must first fire
      // INSIDE the confirm's nested loop, not during a QTest::mouseClick event pump —
      // there its qWait poll gets buried under the confirm's loop and deadlocks.
      dismissModal("OK");   // the in-dialog styled confirm
      clearBtn->click();

      // The row is still IN the list (slot held open, same height) but paints as blank.
      const QImage after = list->viewport()->grab(r).toImage();
      slotHeld = list->count() == rowsBefore && list->visualItemRect(item).height() == r.height();
      bool uniform = !after.isNull();
      const QRgb base = uniform ? after.pixel(1, 1) : 0;
      for (int y = 0; y < after.height() && uniform; ++y)
        for (int x = 0; x < after.width() && uniform; ++x)
          if (after.pixel(x, y) != base) uniform = false;
      blankedAtOnce = uniform && after != before;

      // …and the item is gone once the scatter has played out.
      const auto rowPresent = [&] {
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole).toString() == id) return true;
        return false;
      };
      settle([&] { return !(rowPresent()); }, 3000);
      goneAfter = !rowPresent();
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(blankedAtOnce, "the original row kept painting under the scatter");
    QVERIFY2(slotHeld, "the row's slot collapsed before the scatter finished");
    QVERIFY2(goneAfter, "the doomed row never left the list");
    beat();
  }

  // Nothing open here, so the pinned "Temporary (unsaved)" row is listed above the saved
  // projects — and the batch bar (it hosts Select all) is up because there are rows to
  // select. Removing every project takes both away, and the row underneath must GLIDE up
  // into the space, not be dropped into it: the strip used to lose its height the frame
  // its last control was hidden, and the layout's own spacing went in one more frame
  // after that ("it should smoothly move"). Pins the whole close as a
  // continuous slide: no single frame moves the row more than a few pixels.
  void closingTheBatchBarGlidesTheRowsUp() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    const auto motion = withMotion();   // the slide below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    QVERIFY(!win.addImageProjectEntry(img, "one").isEmpty());
    QVERIFY(!win.addImageProjectEntry(img, "two").isEmpty());

    bool sawPinned = false, barWasUp = false;
    int biggestStep = 0, travelled = 0;
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
      QWidget* selectAll = dlg->findChild<QPushButton*>("projectsSelectAll");
      QWidget* bar = selectAll ? selectAll->parentWidget() : nullptr;
      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!bar || !clearBtn) { bailOut(); return; }
      // The pinned row's top edge, in screen coordinates — what the eye follows.
      const auto pinnedTop = [&] {
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole + 11).toBool())
            return list->viewport()->mapToGlobal(list->visualItemRect(list->item(i)).topLeft()).y();
        return -1;
      };
      sawPinned = pinnedTop() >= 0;
      barWasUp = bar->isVisible() && bar->height() > 0;
      if (!sawPinned || !barWasUp) { bailOut(); return; }
      const int from = pinnedTop();
      dismissModal("OK");
      clearBtn->click();
      int last = from;
      for (int i = 0; i < 70 && bar->isVisible(); ++i) {
        QTest::qWait(16);
        const int now = pinnedTop();
        if (now < 0) continue;   // mid-rebuild
        biggestStep = std::max(biggestStep, std::abs(now - last));
        last = now;
      }
      travelled = from - last;
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawPinned, "the pinned row was not listed with the saved projects");
    QVERIFY2(barWasUp, "the batch bar was not up over the selectable rows");
    QVERIFY2(travelled > 20, QString("the rows never moved up (%1px)").arg(travelled).toLatin1());
    QVERIFY2(biggestStep <= 20,
             QString("the bar's close dropped the rows %1px in one frame — not a glide")
                 .arg(biggestStep).toLatin1());
    beat();
  }

  // Removing the OPEN project empties the list — and what stands there then is the pinned
  // "Temporary (unsaved)" row, never "No projects yet": the removal reset this window to a
  // blank unsaved editor (eraseLocalProject → resetToBlankEditor), exactly the state the
  // browser's list pins that row for. The window was asked once, at open time, so the row
  // never came and the emptied list read "No projects yet". Driven through
  // Clear All, the removal path that keeps the dialog up.
  void removingTheOpenProjectPinsTheTemporaryRow() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    const auto motion = withMotion();   // the arrival below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkMagenta);
    const QString id = win.addImageProjectEntry(img, "the-only-one");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));   // …and it is this window's OPEN project

    bool sawRow = false, tempPinned = false, noPlaceholder = true, landedWhereItArrived = false;
    bool arrivedVeiled = false, cloudInFlight = false, landedWhole = false;
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
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) sawRow = true;
      if (!sawRow) { bailOut(); return; }

      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!clearBtn) { bailOut(); return; }
      dismissModal("OK");   // the in-dialog styled confirm (see the note above)
      clearBtn->click();

      // Once the dust has landed the list holds the pinned row and nothing else.
      const auto pinned = [&] {
        return list->count() == 1 && list->item(0)->data(Qt::UserRole + 11).toBool();
      };
      settle([&] { return pinned(); }, 3000);
      tempPinned = pinned() && list->item(0)->text() == QStringLiteral("Temporary (unsaved)");
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text() == QStringLiteral("No projects yet")) noPlaceholder = false;
      // …and it ARRIVES: veiled behind its own motes with the filter's light cloud in
      // flight, never the removal's scatter. The rebuild answering a removal finds the
      // list EMPTY, and reading that as the opening build skipped the arrival outright.
      arrivedVeiled = list->item(0)->data(Qt::UserRole + 43).toDouble() == 0.0;
      cloudInFlight = !dlg->findChildren<QWidget*>("stencilFilterDust").isEmpty();
      for (int i = 0; i < 200 && list->item(0)->data(Qt::UserRole + 43).toDouble() < 1.0; ++i)
        QTest::qWait(10);
      landedWhole = list->item(0)->data(Qt::UserRole + 43).toDouble() >= 1.0;

      // …and it arrives WHERE IT BELONGS. The list's own top moves with the batch bar
      // above it, and answering the removal in two repaints showed that bar again for the
      // stale row: the pinned row appeared a bar's height too low and jumped up a beat
      // later. Its screen position at arrival must be its final one.
      if (tempPinned) {
        const auto rowTop = [&] {
          return list->viewport()->mapToGlobal(list->visualItemRect(list->item(0)).topLeft()).y();
        };
        const int atArrival = rowTop();
        for (int i = 0; i < 60; ++i) QTest::qWait(10);   // past the bar's out-flight
        landedWhereItArrived = rowTop() == atArrival;
      }
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(tempPinned, "the emptied list never pinned the window's \"Temporary (unsaved)\" row");
    QVERIFY2(noPlaceholder, "the emptied list still read \"No projects yet\"");
    QVERIFY2(arrivedVeiled, "the pinned row was simply there — not veiled behind its own motes");
    QVERIFY2(cloudInFlight, "no arrival cloud played for the row the removal revealed");
    QVERIFY2(landedWhole, "the arriving row never came out from behind its veil");
    QVERIFY2(landedWhereItArrived, "the pinned row appeared off its final place and jumped");
    beat();
  }

  // Closing the dialog mid-scatter must not bring removed rows back: the close flight
  // photographs the dialog as it hides, and it used to fly the OPEN-time snapshot —
  // rows just cleared reappeared in the shrinking ghost. Pins the fix: on done() the
  // doomed rows are finalized (gone from the list at once, scatters stopped), and the
  // ghost's pixmap shows the row's slot as empty background, not the row.
  void closingProjectsDialogFinalizesRetiredRows() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    // ctest runs this suite with STENCIL_NO_ANIM=1, which turns the reveal/close
    // flights off entirely — but the close flight's ghost IS what this test pins.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "doomed-close-row");
    QVERIFY(!id.isEmpty());

    bool sawRow = false, finalized = false, ghostSeen = false, ghostClean = false;
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
      QListWidgetItem* item = nullptr;
      for (int i = 0; i < list->count() && !item; ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) item = list->item(i);
      if (!item) { bailOut(); return; }
      sawRow = true;
      list->scrollToItem(item);
      // Let the OPEN flight (dust or ghost) land and delete itself, so the one found
      // below is unambiguously the close flight's. Bounded wait for a loaded machine.
      QTest::qWait(50);
      const auto openFlightLive = [&] { return surfaceFlight(&win) || modalGhost(&win); };
      settle([&] { return !(openFlightLive()); }, 2500);
      if (openFlightLive()) { bailOut(); return; }
      // Where the row sits, in DIALOG coordinates — the ghost photographs the dialog.
      const QRect rowInDlg =
          QRect(list->viewport()->mapTo(dlg, list->visualItemRect(item).topLeft()),
                list->visualItemRect(item).size()).adjusted(4, 4, -4, -4);

      QPushButton* clearBtn = nullptr;
      QPushButton* closeBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>()) {
        if (b->text().startsWith("Clear All")) clearBtn = b;
        if (b->text() == "Close") closeBtn = b;
      }
      if (!clearBtn || !closeBtn) { bailOut(); return; }
      dismissModal("OK");
      clearBtn->click();     // rows doomed, scatter playing
      closeBtn->click();     // …and the dialog closed IMMEDIATELY, mid-scatter

      // Finalized on done(): the doomed row left the list at once, no DUST_MS wait.
      finalized = true;
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) finalized = false;

      // The close flight's SNAPSHOT must show the slot as bare background — the stale
      // open-time picture (or a barely-started scatter) would still paint the row.
      // Checked either way (dust or ghost), same as the reveal tests.
      QPixmap shot;
      if (auto* fx = surfaceFlight(&win)) { ghostSeen = true; shot = fx->snapshot(); }
      else if (auto* g = modalGhost(&win)) { ghostSeen = true; shot = g->pixmap(); }
      if (!shot.isNull()) {
        const qreal dpr = shot.devicePixelRatio();
        const QImage gi = shot.toImage();
        const QRect strip(int(rowInDlg.x() * dpr), int(rowInDlg.y() * dpr),
                          int(rowInDlg.width() * dpr), int(rowInDlg.height() * dpr));
        if (gi.rect().contains(strip)) {
          const QRgb base = gi.pixel(strip.center());   // bare list background
          int off = 0;
          for (int y = strip.top(); y <= strip.bottom(); ++y)
            for (int x = strip.left(); x <= strip.right(); ++x)
              if (gi.pixel(x, y) != base) ++off;
          // A hair of frame anti-aliasing may cross the strip; a painted row (icon,
          // text, badges) is orders of magnitude more than 1% of it.
          ghostClean = off < strip.width() * strip.height() / 100;
        }
      }
      bailOut();   // belt and braces — Close already rejected the dialog
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(finalized, "closing mid-scatter left the doomed row in the list");
    QVERIFY2(ghostSeen, "the close flight's ghost was not found");
    QVERIFY2(ghostClean, "the close ghost still painted the removed row");
    beat();
  }

  // Regression: Remove used to CLOSE the projects dialog before its confirm (the box was
  // shown by MainWindow after exec() returned) and left it closed — the user lost their
  // place. Now the ⋯-menu Remove confirms in-dialog (deferred a turn, drag-release safe):
  // No keeps the row and the dialog; Yes removes the project while the dialog stays open.
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
      for (const auto& p : win.projectList_)
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

  // §2.1: an image index the turn cannot satisfy costs that ACTION a warning,
  // never the rest of the plan; a save with nothing on the canvas is skipped
  // the same way instead of failing the turn.
  // A restored session must keep its PROJECT identity: without the binding, a
  // relaunch showed the project's pixels while activeProjectId_ was empty, so
  // deleting that project later left its image orphaned on the canvas.
  void sessionRoundTripsTheActiveProjectBinding() {
    MainWindow win(nullptr, false);
    win.resize(1200, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkMagenta);
    win.loadImageWithLayout(img, QJsonObject());
    win.createLocalProject(QStringLiteral("session-bind"), /*announce=*/false);
    QVERIFY(!win.activeProjectId_.isEmpty());
    win.saveSessionNow();
    const auto sess = stencil::gui::fileStore::loadSession();
    QVERIFY(sess.has_value());
    QCOMPARE(sess->activeProjectId, win.activeProjectId_);
    // …and the restore path re-binds it (project still exists in the list).
    MainWindow win2(nullptr, true);
    QCOMPARE(win2.activeProjectId_, win.activeProjectId_);
  }

  // Deliberate NON-round-trip: the image filter/tint (and the compare split view,
  // which was never persisted to begin with) must NOT carry over into a freshly
  // reopened desktop app — unlike everything else a session restores
  // (image, lines, page size, scale, crop, rotation, draw mode), which still does.
  void sessionRestoreDoesNotCarryOverTheFilterOrTint() {
    MainWindow win(nullptr, false);
    win.resize(1200, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // a REAL file path — restoreSession needs one to reload from
    QTRY_VERIFY(win.canvas_->hasImage());
    win.applyImageFilter(QStringLiteral("custom"));
    win.applyTintColor(QColor(200, 40, 40));
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("custom"));
    win.saveSessionNow();

    MainWindow win2(nullptr, true);
    QVERIFY(win2.canvas_ && win2.canvas_->hasImage());   // the rest of the session DID restore
    QCOMPARE(win2.settings_.imageFilter, QStringLiteral("none"));
    QCOMPARE(win2.canvas_->imageFilter(), QStringLiteral("none"));
    if (win2.imageFilter_) QCOMPARE(win2.imageFilter_->currentData().toString(), QStringLiteral("none"));
    if (win2.filterColorBtn_) QVERIFY(!win2.filterColorBtn_->isVisible());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projects.gui.moc"
