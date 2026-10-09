// MainWindow GUI e2e — the Projects window's row-menu Description and Keywords edits: a local row's
// text lands in the window's registry and its file, a server row's goes out as a version-guarded
// PUT, and a server that refuses it is a toast, not silence.
// Shared ground is in projectsHeld.gui.hpp, over MainWindow.gui.hpp.
#include "projectsHeld.gui.hpp"
#include "../../support/recordingSink.hpp"
#include "../../support/connectNow.hpp"
#include "../../support/mockRest.hpp"
#include "DescriptionDialog.hpp"
#include <QPlainTextEdit>

namespace {

  QListWidgetItem* rowFor(QListWidget* list, const QString& id, const QString& server) {
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == id &&
          list->item(i)->data(Qt::UserRole + 1).toString() == server)
        return list->item(i);
    return nullptr;
  }

  // The row's ⋯ menu, its "Add description", the editor it opens, filled and saved: three nested
  // loops, each answered by a timer armed before the one that opens it.
  void editDescriptionViaMenu(QListWidget* list, QListWidgetItem* it, const QString& text) {
    list->setCurrentItem(it);
    QTimer::singleShot(0, [text] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QTimer::singleShot(0, [text] {
        stencil::gui::DescriptionDialog* dlg = nullptr;
        for (int i = 0; i < 200 && !dlg; ++i) {
          dlg = qobject_cast<stencil::gui::DescriptionDialog*>(QApplication::activeModalWidget());
          if (!dlg) QTest::qWait(10);
        }
        if (!dlg) return;
        dlg->findChild<QPlainTextEdit*>()->setPlainText(text);
        dlg->accept();
      });
      for (QAction* a : menu->actions())
        if (a->text() == QLatin1String("Add description")) { a->trigger(); break; }
      menu->close();
    });
    emit list->customContextMenuRequested(list->visualItemRect(it).center());
  }

  QListWidget* projectsList(QDialog*& dlg) {
    QListWidget* list = nullptr;
    for (int i = 0; i < 200 && !list; ++i) {
      dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (dlg) list = dlg->findChild<QListWidget*>("projectsList");
      if (!list) QTest::qWait(10);
    }
    return list;
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void localRowDescriptionLandsInTheRegistry() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    QVERIFY(held::showForProjects(win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkGreen);
    const QString id = win.parts.chatAppliers.addImageProjectEntry(img, "describe-me");
    QVERIFY(!id.isEmpty());
    const auto description = [&win, id] {
      const stencil::gui::Project* pr = win.findProject(id.toStdString());
      return pr ? QString::fromStdString(pr->meta.description) : QString();
    };

    bool sawRow = false, landed = false;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      QListWidget* list = projectsList(dlg);
      QListWidgetItem* item = list ? rowFor(list, id, QString()) : nullptr;
      if (!item) { if (dlg) dlg->reject(); return; }
      sawRow = true;
      editDescriptionViaMenu(list, item, QStringLiteral("Shared words"));
      settle([&] { return description() == QStringLiteral("Shared words"); }, 3000);
      landed = description() == QStringLiteral("Shared words");
      dlg->reject();
    });
    win.parts.projects.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(landed, "the row menu's description never reached the window's registry");
    stencil::gui::fileStore::flushWrites();
    for (const auto& p : stencil::gui::fileStore::loadProjects())
      if (QString::fromStdString(p.meta.id) == id)
        QCOMPARE(QString::fromStdString(p.meta.description), QStringLiteral("Shared words"));
    beat();
  }

  void serverRowDescriptionIsAGuardedPutAndAFailureToasts() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    mock.projects[QStringLiteral("p1")].name = QStringLiteral("Remote");
    {   // the listing shows only projects with a picture
      QImage img(40, 30, QImage::Format_RGB32);
      img.fill(Qt::white);
      QBuffer buf(&mock.projects[QStringLiteral("p1")].original);
      buf.open(QIODevice::WriteOnly);
      img.save(&buf, "PNG");
    }
    MainWindow win(nullptr, false);
    QVERIFY(held::showForProjects(win));
    auto owned = std::make_unique<stencil::test::RecordingSink>();
    stencil::test::RecordingSink* notices = owned.get();
    win.notify->setSystemSink(std::move(owned));
    win.notify->setChannel(stencil::gui::NotifyChannel::SYSTEM);
    QString err;
    QVERIFY2(stencil::test::connectNow(*win.ensureConnections(), mock.url(), QString(), err), qPrintable(err));

    bool sawRow = false, saved = false, failed = false;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      QListWidget* list = projectsList(dlg);
      QListWidgetItem* item = nullptr;
      for (int i = 0; list && i < 300 && !item; ++i) {
        item = rowFor(list, QStringLiteral("p1"), mock.url());
        if (!item) QTest::qWait(10);
      }
      if (!item) { if (dlg) dlg->reject(); return; }
      sawRow = true;
      editDescriptionViaMenu(list, item, QStringLiteral("From here"));
      settle([&] { return mock.projects[QStringLiteral("p1")].description == QStringLiteral("From here"); }, 3000);
      saved = mock.puts == 1 && mock.projects[QStringLiteral("p1")].description == QStringLiteral("From here");
      // The server refuses the next one: the user hears it.
      mock.putStatus = 500;
      item = rowFor(list, QStringLiteral("p1"), mock.url());
      if (item) editDescriptionViaMenu(list, item, QStringLiteral("Refused"));
      settle([&] { return !notices->shown.filter(QStringLiteral("Description update failed")).isEmpty(); }, 3000);
      failed = !notices->shown.filter(QStringLiteral("Description update failed")).isEmpty();
      dlg->reject();
    });
    win.parts.projects.openProjects();
    QVERIFY2(sawRow, "the server row never appeared in the dialog");
    QVERIFY2(saved, "the description never reached the server through a guarded PUT");
    QVERIFY2(failed, "a refused PUT raised no toast");
    QCOMPARE(mock.projects[QStringLiteral("p1")].description, QStringLiteral("From here"));
    beat();
  }

  // The row menu's keywordsRequested lands in the same window flow as the description's.
  void keywordsGoThroughTheWindowLocallyAndAsAGuardedPut() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    mock.projects[QStringLiteral("p1")].name = QStringLiteral("Remote");
    MainWindow win(nullptr, false);
    QVERIFY(held::showForProjects(win));
    auto owned = std::make_unique<stencil::test::RecordingSink>();
    stencil::test::RecordingSink* notices = owned.get();
    win.notify->setSystemSink(std::move(owned));
    win.notify->setChannel(stencil::gui::NotifyChannel::SYSTEM);

    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkGreen);
    const QString id = win.parts.chatAppliers.addImageProjectEntry(img, "tag-me");
    QVERIFY(!id.isEmpty());
    win.parts.projects.setProjectKeywordsById(id, QString(), {QStringLiteral("alpha"), QStringLiteral("beta")});
    const stencil::gui::Project* pr = win.findProject(id.toStdString());
    QVERIFY(pr && pr->meta.keywords == std::vector<std::string>({"alpha", "beta"}));

    QString err;
    QVERIFY2(stencil::test::connectNow(*win.ensureConnections(), mock.url(), QString(), err), qPrintable(err));
    win.parts.projects.setProjectKeywordsById(QStringLiteral("p1"), mock.url(), {QStringLiteral("shared")});
    QTRY_COMPARE(mock.projects[QStringLiteral("p1")].keywords, QStringList{QStringLiteral("shared")});
    QCOMPARE(mock.puts, 1);
    mock.putStatus = 500;
    win.parts.projects.setProjectKeywordsById(QStringLiteral("p1"), mock.url(), {QStringLiteral("refused")});
    QTRY_VERIFY(!notices->shown.filter(QStringLiteral("Keywords update failed")).isEmpty());
    QCOMPARE(mock.projects[QStringLiteral("p1")].keywords, QStringList{QStringLiteral("shared")});
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsMeta.gui.moc"
