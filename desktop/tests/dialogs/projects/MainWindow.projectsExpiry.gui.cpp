// MainWindow GUI e2e — a project row's expiry: "never expires" for one kept forever, and a Set
// expiration saved from the row's ⋯ menu restating that row in the open list, never on a reopen.
// Shared ground is in projectsHeld.gui.hpp, over MainWindow.gui.hpp.
#include "projectsHeld.gui.hpp"
#include "ExpirationDialog.hpp"
#include <QCheckBox>

namespace {

  QListWidgetItem* idRow(QListWidget* list, const QString& id) {
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
    return nullptr;
  }

  // The row's ⋯ menu, its "Set expiration", and the editor it opens with Keep forever set to `keep`.
  void setKeepForeverViaMenu(QListWidget* list, QListWidgetItem* it, bool keep) {
    list->setCurrentItem(it);
    QTimer::singleShot(0, [keep] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QTimer::singleShot(0, [keep] {
        stencil::gui::ExpirationDialog* dlg = nullptr;
        for (int i = 0; i < 200 && !dlg; ++i) {
          dlg = qobject_cast<stencil::gui::ExpirationDialog*>(QApplication::activeModalWidget());
          if (!dlg) QTest::qWait(10);
        }
        if (!dlg) return;
        for (QCheckBox* box : dlg->findChildren<QCheckBox*>())
          if (box->text().contains(QStringLiteral("Keep forever"))) box->setChecked(keep);
        dlg->accept();
      });
      for (QAction* a : menu->actions())
        if (a->text() == QLatin1String("Set expiration")) { a->trigger(); break; }
      menu->close();
    });
    emit list->customContextMenuRequested(list->visualItemRect(it).center());
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void savedExpirationRestatesTheOpenRow() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    QVERIFY(held::showForProjects(win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.parts.chatAppliers.addImageProjectEntry(img, "expiring");
    QVERIFY(!id.isEmpty());

    QString kept, dated;
    bool sawRow = false;
    QTimer::singleShot(0, [&] {
      const held::ProjectsWindow pw = held::projectsWindow();
      const auto rowText = [&] {
        QListWidgetItem* it = pw.list ? idRow(pw.list, id) : nullptr;
        return it ? it->text() : QString();
      };
      if (!pw.list || !idRow(pw.list, id)) { held::rejectModal(); return; }
      sawRow = true;
      setKeepForeverViaMenu(pw.list, idRow(pw.list, id), true);
      settle([&] { return rowText().contains(QStringLiteral("never expires")); }, 3000);
      kept = rowText();
      setKeepForeverViaMenu(pw.list, idRow(pw.list, id), false);
      settle([&] { return rowText().contains(QStringLiteral("expires in")); }, 3000);
      dated = rowText();
      pw.dlg->reject();
    });
    win.projectHost.flows.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(kept.contains(QStringLiteral("never expires")),
             qPrintable(QStringLiteral("kept forever, the open row reads \"%1\"").arg(kept)));
    QVERIFY2(dated.contains(QStringLiteral("expires in")),
             qPrintable(QStringLiteral("given a date again, the open row reads \"%1\"").arg(dated)));
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsExpiry.gui.moc"
