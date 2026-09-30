// MainWindow GUI e2e — "Make a copy" out of a projects row (browser projects/row/copyItem.js): the
// flyout's scope reaches the dialog's request, and the confirmation stacks on the projects window.
#include "../../MainWindow.gui.hpp"
#include "CopyProjectDialog.hpp"
#include "ProjectCopy.hpp"
#include "ProjectsDialog.hpp"

#include <QListWidget>

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

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A picked scope in the row menu's "Make a copy" flyout reaches the dialog's request, the menu
  // still open around it (a crash once jumped through the scope row's callback here).
  void rowMenuScopeReachesTheRequest() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = loadedWithLine(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage() && !win.activeProjectId.isEmpty(), 5000);
    stencil::gui::ProjectsDialog dlg(win.projectList, 5000);
    dlg.show();
    QTest::qWait(50);
    auto* list = dlg.findChild<QListWidget*>(QStringLiteral("projectsList"));
    QListWidgetItem* row = nullptr;
    for (int i = 0; list && i < list->count() && !row; ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == win.activeProjectId) row = list->item(i);
    QVERIFY(row);
    int picked = -1;
    QString pickedId;
    QObject::connect(&dlg, &stencil::gui::ProjectsDialog::copyRequested, &dlg,
                     [&](const QString& id, const QString&, int s, const QRect&) { pickedId = id; picked = s; });
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      for (QAction* a : menu->actions()) {
        if (a->text() != QLatin1String("Make a copy") || !a->menu()) continue;
        QMenu* sub = a->menu();
        menu->setActiveAction(a);   // hover opens the flyout, as a pointer does
        QTRY_VERIFY(sub->isVisible());
        QTest::qWait(400);          // past the flyout's reveal
        for (QAction* s : sub->actions())
          if (s->text() == QLatin1String("Image only"))
            QTest::mouseClick(sub, Qt::LeftButton, {}, sub->actionGeometry(s).center());
      }
      if (QApplication::activePopupWidget()) menu->close();
    });
    emit list->customContextMenuRequested(list->visualItemRect(row).center());
    QTRY_COMPARE(picked, int(scope::COPY_IMAGE));
    QCOMPARE(pickedId, win.activeProjectId);
  }

  // From a projects row the confirmation stacks on that window, so its flight plays over the list.
  void rowConfirmationStacksOnTheProjectsWindow() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = loadedWithLine(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage() && !win.activeProjectId.isEmpty(), 5000);
    stencil::gui::ProjectsDialog dlg(win.projectList, 5000);
    dlg.show();
    win.parts.projectCopy.wireProjectsList(dlg);
    QWidget* parent = nullptr;
    QTimer answer;
    QObject::connect(&answer, &QTimer::timeout, [&] {
      auto* confirm = dlg.findChild<stencil::gui::CopyProjectDialog*>();
      if (!confirm || !confirm->isVisible()) return;
      answer.stop();
      parent = confirm->parentWidget();
      confirm->findChild<QPushButton*>(QStringLiteral("copyProjectCancel"))->click();
    });
    answer.start(20);
    emit dlg.copyRequested(win.activeProjectId, QString(), scope::COPY_IMAGE, QRect());
    QTRY_VERIFY_WITH_TIMEOUT(parent != nullptr, 5000);
    QCOMPARE(parent, static_cast<QWidget*>(&dlg));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsCopyRow.gui.moc"
