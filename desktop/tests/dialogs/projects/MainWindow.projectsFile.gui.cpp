// MainWindow GUI e2e — The .stencil file: opening it, autosaving edits into it, and deleting the linked one.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/theme/filterFade.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

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
    QCOMPARE(canvas->getRotationQuarters(), 1);                 // layout rotation adopted
    QVERIFY(totalPoints(canvas) > 0);                        // the line was adopted
    beat();
  }

  // Live sync: a project linked to a .stencil with live-sync ON auto-saves edits back to the file,
  // debounced — openProjectFile linking → the toggle → an edit → scheduleStencilAutosave → flush.
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
    QCOMPARE(canvas->getRotationQuarters(), 0);

    QAction* live = actionByText(&win, "Live Sync with File");
    QVERIFY(live);
    QVERIFY(live->isEnabled());     // enabled because the project is file-linked
    live->setChecked(true);         // toggled → toggleStencilLiveSync(true)

    QAction* rotate = actionByText(&win, "Rotate Right");
    QVERIFY(rotate);
    rotate->trigger();
    QCOMPARE(canvas->getRotationQuarters(), 1);

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

  // Deleting the linked .stencil removes it from disk and unlinks the project (the live-sync and
  // delete actions disable), while the project stays open in the canvas.
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

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsFile.gui.moc"
