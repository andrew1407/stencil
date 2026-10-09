// MainWindow GUI e2e — two windows share one project registry and one Settings (SharedState): a
// project created in either survives the other's registry write, and a setting changed in one
// reaches the other and survives its close.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../../src/app/meta/SharedState.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void twoWindowsShareTheRegistryAndSettings() {
    QPointer<MainWindow> a = new MainWindow(nullptr, false);
    a->setAttribute(Qt::WA_DeleteOnClose);
    a->show();
    QVERIFY(QTest::qWaitForWindowExposed(a.data()));
    QPointer<MainWindow> b = new MainWindow(nullptr, false);
    b->setAttribute(Qt::WA_DeleteOnClose);
    b->show();
    QVERIFY(QTest::qWaitForWindowExposed(b.data()));
    QCOMPARE(&a->projectList, &b->projectList);
    QCOMPARE(&a->projectList, &stencil::gui::SharedState::instance().getProjects());

    // A creates a project, then B writes the registry: both rows are in the file.
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const int before = static_cast<int>(a->projectList.size());
    const QString fromA = a->parts.chatAppliers.addImageProjectEntry(img, "from-a");
    QVERIFY(!fromA.isEmpty());
    QCOMPARE(static_cast<int>(b->projectList.size()), before + 1);
    const QString fromB = b->parts.chatAppliers.addImageProjectEntry(img, "from-b");
    QVERIFY(!fromB.isEmpty());
    stencil::gui::fileStore::flushWrites();
    const std::vector<stencil::gui::Project> stored = stencil::gui::fileStore::loadProjects();
    const auto has = [&stored](const QString& id) {
      for (const auto& p : stored)
        if (QString::fromStdString(p.meta.id) == id) return true;
      return false;
    };
    QCOMPARE(static_cast<int>(stored.size()), before + 2);
    QVERIFY2(has(fromA), "B's registry write dropped the project A created");
    QVERIFY2(has(fromB), "B's own project is missing from the file");

    // A setting changed in A reaches B and survives B's close.
    const bool was = a->settings.showPoints;
    a->acts.showPoints->setChecked(!was);
    QCOMPARE(a->settings.showPoints, !was);
    QTRY_COMPARE(b->settings.showPoints, !was);
    QCOMPARE(b->acts.showPoints->isChecked(), !was);
    b->close();
    QTRY_VERIFY(b.isNull());
    QCOMPARE(stencil::gui::fileStore::loadSettings().showPoints, !was);
    a->close();
    QTRY_VERIFY(a.isNull());
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsShared.gui.moc"
