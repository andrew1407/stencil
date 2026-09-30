// MainWindow GUI e2e — "Make a copy" (browser core/project/copy/): the three scopes under their
// "-copy" / "-copy(1)" names on an image file of their own, an unsaved incognito copy, what the
// request rules drop, the canvas menu's and the toolbar's entry points, and "Just copy".
#include "../../MainWindow.gui.hpp"
#include "CopyProjectDialog.hpp"
#include "ProjectCopy.hpp"

using stencil::gui::CopyOpen;
using stencil::gui::CopyRequest;
using stencil::gui::Project;
using stencil::gui::ProjectCopy;
namespace scope = stencil::support;

namespace {
  CanvasWidget* loadedWithLine(MainWindow& win) {
    CanvasWidget* canvas = openLoaded(win);
    stencil::core::Line line;
    line.points = {{10, 10}, {60, 40}};
    canvas->commitLines({line});
    return canvas;
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // A friend's member, so the window's parts are in reach.
  static QString copyNow(MainWindow& win, const CopyRequest& req) {
    QString made;
    bool finished = false;
    win.parts.projectCopy.run(req, [&](bool ok, QString id, QString) {
      finished = true;
      made = ok ? id : QString();
    });
    (void)QTest::qWaitFor([&] { return finished; }, 5000);
    return made;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void scopesCarryWhatTheyNameUnderTheirOwnNames() {
    MainWindow win(nullptr, false);
    win.projectList.clear();   // the state dir outlives a run: the names below start from nothing
    CanvasWidget* canvas = loadedWithLine(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage() && !win.activeProjectId.isEmpty(), 5000);
    const QString srcId = win.activeProjectId;
    {
      Project* src = win.findProject(srcId.toStdString());
      src->meta.color = "#123456";
      src->meta.keywords = {"roof"};
    }
    win.saveToActiveProject();
    const Project src = *win.findProject(srcId.toStdString());
    const std::size_t before = win.projectList.size();

    CopyRequest req;
    req.id = srcId;
    req.what = scope::COPY_IMAGE;
    const Project image = *win.findProject(copyNow(win, req).toStdString());
    QCOMPARE(image.meta.name, src.meta.name + "-copy");
    QVERIFY2(image.lines.empty() && image.meta.color.empty(), "image only is the untouched original");
    QVERIFY2(image.imagePath != src.imagePath && QFileInfo::exists(image.imagePath),
             "a copy owns its image file, never the source's path");

    req.what = scope::COPY_LAYOUT;
    const Project layout = *win.findProject(copyNow(win, req).toStdString());
    QCOMPARE(layout.meta.name, src.meta.name + "-copy(1)");
    QCOMPARE(layout.lines.size(), src.lines.size());
    QVERIFY(layout.meta.color.empty());

    req.what = scope::COPY_PROJECT;
    const Project whole = *win.findProject(copyNow(win, req).toStdString());
    QCOMPARE(whole.meta.name, src.meta.name + "-copy(2)");
    QCOMPARE(whole.meta.color, src.meta.color);
    QCOMPARE(whole.meta.keywords, src.meta.keywords);
    QCOMPARE(win.projectList.size(), before + 3);
    QCOMPARE(win.activeProjectId, srcId);   // nothing was asked to open
  }

  void incognitoCopyOpensUnsaved() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = loadedWithLine(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const std::size_t before = win.projectList.size();
    CopyRequest req;
    req.what = scope::COPY_LAYOUT;
    req.open = stencil::gui::COPY_OPEN_HERE;
    req.incognito = true;
    QVERIFY(copyNow(win, req).isEmpty());
    QTRY_VERIFY(win.incognito);
    QVERIFY(win.activeProjectId.isEmpty());
    QCOMPARE(win.projectList.size(), before);
    QTRY_COMPARE(canvas->getLines().size(), std::size_t(1));
  }

  void settleDropsWhatCannotBeHonoured() {
    CopyRequest req;
    req.incognito = true;
    QString note;
    QVERIFY(!ProjectCopy::settle(req, false, &note).incognito);
    QVERIFY(note.contains("must be opened"));
    req.open = stencil::gui::COPY_OPEN_NEW_WINDOW;
    QVERIFY(!ProjectCopy::settle(req, true, &note).incognito);
    QVERIFY(note.contains("server copy"));
    req.local = true;
    QVERIFY(ProjectCopy::settle(req, true, &note).incognito);
    QVERIFY(note.isEmpty());
  }

  void entryPointsOfferTheThreeScopes() {
    MainWindow win(nullptr, false);
    QVERIFY(win.acts.copyProject && !win.acts.copyProject->isEnabled());   // no image yet
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(win.acts.copyProject->isEnabled());
    QToolButton* button = qobject_cast<QToolButton*>(win.buttonForAction(win.acts.copyProject));
    QVERIFY2(button && button->isVisibleTo(&win), "the Image section carries the Make a copy button");

    QStringList rows;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      for (QAction* a : menu->actions())
        if (a->text() == QLatin1String("Make a copy") && a->menu())
          for (QAction* row : a->menu()->actions()) rows << row->text();
      menu->close();
    });
    QTest::mouseClick(win.findChild<QScrollArea*>()->viewport(), Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);
    QCOMPARE(rows, QStringList({"Image only", "Image and layout", "Whole project"}));
  }

  void justCopyLeavesTheEditorWhereItWas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = loadedWithLine(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage() && !win.activeProjectId.isEmpty(), 5000);
    const QString srcId = win.activeProjectId;
    const std::size_t before = win.projectList.size();
    // The confirmation exec()s its own loop, which still runs this poll.
    QTimer answer;
    QObject::connect(&answer, &QTimer::timeout, [&win, &answer] {
      auto* dlg = win.findChild<stencil::gui::CopyProjectDialog*>();
      auto* just = dlg ? dlg->findChild<QPushButton*>(QStringLiteral("copyProjectJust")) : nullptr;
      if (!just || !dlg->isVisible()) return;
      answer.stop();
      just->click();
    });
    answer.start(20);
    QString made;
    CopyRequest req;
    req.what = scope::COPY_LAYOUT;
    win.parts.projectCopy.offer(req, QRect(), [&made](QString id, CopyOpen) { made = id; });
    QTRY_VERIFY_WITH_TIMEOUT(!made.isEmpty(), 5000);
    QCOMPARE(win.projectList.size(), before + 1);
    QCOMPARE(win.activeProjectId, srcId);
    QVERIFY(!win.incognito);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsCopy.gui.moc"
