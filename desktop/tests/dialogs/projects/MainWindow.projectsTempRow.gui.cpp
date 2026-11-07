// MainWindow GUI e2e — the projects list's own unsaved row ("Temporary (unsaved)") has nothing to switch to: a click
// on it closes the list, as the browser's does (browser projects/row/localRow.js), and it wears the pointer.
#include "../../MainWindow.gui.hpp"
#include "ProjectsDialog.hpp"

#include <QListWidget>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void clickingTheTemporaryRowClosesTheList() {
    MainWindow win(nullptr, false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::ProjectsDialog dlg(win.projectHost.projectList, 5000);
    dlg.setProjects(win.projectHost.projectList, /*temporary=*/true, /*incognito=*/false);
    dlg.show();
    QTest::qWait(50);
    auto* list = dlg.findChild<QListWidget*>(QStringLiteral("projectsList"));
    QVERIFY(list && list->count() > 0);
    QListWidgetItem* temp = list->item(0);
    QCOMPARE(temp->text(), QStringLiteral("Temporary (unsaved)"));
    int result = -1;
    QObject::connect(&dlg, &QDialog::finished, &dlg, [&](int r) { result = r; });
    const QRect at = list->visualItemRect(temp);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, at.center());
    QTRY_COMPARE(result, int(QDialog::Rejected));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsTempRow.gui.moc"
