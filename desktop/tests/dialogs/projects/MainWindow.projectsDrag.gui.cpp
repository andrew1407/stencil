// MainWindow GUI e2e — A held project row brings the ⋯ beside the Projects window's title: held
// there, the row's own menu pops up, hovering lights an item, and the item released on runs.
// Browser twin: tests/ui/projects/list/dragReorder.test.js.
#include "projectsHeld.gui.hpp"
#include "../../../src/dialogs/projects/row/projectsRowChrome.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Resting on Rename in the menu the ⋯ popped up only lights it; released there it runs on the held
  // row once the drag loop is gone, and that release reorders nothing and opens no zone.
  void heldRowRunsTheMenuItemItIsReleasedOn() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog drags need the offscreen platform");
    MainWindow win(nullptr, false);
    QVERIFY(held::showForProjects(win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString idA = win.parts.chatAppliers.addImageProjectEntry(img, "held-alpha");
    const QString idB = win.parts.chatAppliers.addImageProjectEntry(img, "held-beta");
    QVERIFY(!idA.isEmpty() && !idB.isEmpty());
    const QString sortWas = stencil::gui::g_projectsSortMode;

    bool hidden = false, formed = false, menuUp = false, lit = false, waited = false, folded = false;
    bool inert = false, left = false, renaming = false;
    QTimer::singleShot(0, [&] {
      ProjectsWindow w = projectsWindow();
      if (!w.list || !w.more) {
        if (w.dlg) w.dlg->reject();
        return;
      }
      hidden = !w.more->isVisible();
      w.list->setCurrentItem(rowFor(w.list, idA));
      w.list->onDragStart();
      formed = w.more->isVisible();
      holdAt(w.more->mapToGlobal(w.more->rect().center()));
      QMenu* menu = shownMenuWith(w.dlg, QStringLiteral("Rename"));
      menuUp = menu != nullptr;
      QAction* rename = nullptr;
      if (menu)
        for (QAction* a : menu->actions())
          if (a->text() == QLatin1String("Rename")) rename = a;
      if (rename) {
        holdAt(menu->mapToGlobal(menu->actionGeometry(rename).center()), 60);
        lit = menu->activeAction() == rename;
        QTest::qWait(700);
        waited = menu->isVisible() && !w.dlg->findChild<QWidget*>(QStringLiteral("projectsRenameBox"));
      }
      w.list->onDragEnd();
      folded = menu && !menu->isVisible();
      w.list->onReorder(w.list->row(rowFor(w.list, idA)), w.list->count() - 1);
      w.list->onDragOut(w.list->row(rowFor(w.list, idA)));
      inert = stencil::gui::g_projectsSortMode == sortWas;
      left = !w.more->isVisible();
      settle([&] {
        auto* box = w.dlg->findChild<QWidget*>(QStringLiteral("projectsRenameBox"));
        return box && box->isVisible();
      }, 2000);
      auto* box = w.dlg->findChild<QWidget*>(QStringLiteral("projectsRenameBox"));
      renaming = box && box->isVisible();
      w.dlg->reject();
    });
    win.parts.projects.openProjects();
    QVERIFY2(hidden, "the ⋯ is only there while a row is held");
    QVERIFY2(formed, "picking a row up brought the ⋯");
    QVERIFY2(menuUp, "held over the ⋯, the row's own menu popped up");
    QVERIFY2(lit, "the item under the held row is lit as a hover lights it");
    QVERIFY2(waited, "resting on it runs nothing, however long");
    QVERIFY2(folded, "the release took it, and the menu folded");
    QVERIFY2(inert, "the release reordered nothing");
    QVERIFY2(left, "the ⋯ left with the drag");
    QVERIFY2(renaming, "Rename ran on the held row once the drag let go");
  }

  // A row lit under the held row plays the row menu's hover shimmer, as a pointer's hover does.
  void heldRowHoverPlaysTheRowShimmer() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog drags need the offscreen platform");
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    QVERIFY(held::showForProjects(win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkYellow);
    const QString id = win.parts.chatAppliers.addImageProjectEntry(img, "shimmer-held");
    QVERIFY(!id.isEmpty());

    bool overlay = false, swept = false;
    QTimer::singleShot(0, [&] {
      ProjectsWindow w = projectsWindow();
      if (!w.list || !w.more) {
        if (w.dlg) w.dlg->reject();
        return;
      }
      w.list->setCurrentItem(rowFor(w.list, id));
      w.list->onDragStart();
      holdAt(w.more->mapToGlobal(w.more->rect().center()), 600);
      QMenu* menu = shownMenuWith(w.dlg, QStringLiteral("Rename"));
      QWidget* sheen = menu ? menu->findChild<QWidget*>(QStringLiteral("menuShimmerOverlay")) : nullptr;
      overlay = sheen != nullptr;
      if (sheen) {
        QAction* color = nullptr;
        QAction* rename = nullptr;
        for (QAction* a : menu->actions()) {
          if (a->text() == QLatin1String("Set color")) color = a;
          if (a->text() == QLatin1String("Rename")) rename = a;
        }
        holdAt(menu->mapToGlobal(menu->actionGeometry(color).center()), 900);
        holdAt(menu->mapToGlobal(menu->actionGeometry(rename).center()), 0);
        for (int i = 0; i < 60 && !swept; ++i) {
          const qreal p = sheen->property("sweepProgress").toReal();
          swept = p > 0.0 && p < 1.0;
          QTest::qWait(5);
        }
      }
      holdAt(w.dlg->mapToGlobal(QPoint(w.dlg->width() / 2, w.dlg->height() - 20)));
      w.list->onDragEnd();
      w.dlg->reject();
    });
    win.parts.projects.openProjects();
    QVERIFY2(overlay, "the held menu carries the row menu's shimmer");
    QVERIFY2(swept, "the row lit under the drag sweeps it, as a hover does");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsDrag.gui.moc"
