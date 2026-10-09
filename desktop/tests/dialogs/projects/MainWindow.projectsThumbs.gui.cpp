// MainWindow GUI e2e — the Projects dialog's local thumbnails: the rows open before their
// pictures, each picture arrives composed at thumbnail scale, and a reopen serves it cached.
// Shared ground is in projectsHeld.gui.hpp, over MainWindow.gui.hpp.
#include "projectsHeld.gui.hpp"

namespace {

  QListWidgetItem* rowOf(QListWidget* list, const QString& id) {
    for (int i = 0; list && i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
    return nullptr;
  }

  QPixmap thumbOf(QListWidgetItem* it) {
    return it ? it->data(Qt::UserRole + 2).value<QPixmap>() : QPixmap();
  }

  QListWidget* openList() {
    for (int i = 0; i < 200; ++i) {
      if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        if (auto* list = dlg->findChild<QListWidget*>("projectsList")) return list;
      QTest::qWait(10);
    }
    return nullptr;
  }

  void closeDialog() {
    if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void thumbnailsArriveLazilyAndReopenCached() {
    MainWindow win(nullptr, false);
    QVERIFY(held::showForProjects(win));
    QImage img(2400, 1600, QImage::Format_RGB32);
    img.fill(Qt::white);
    const QString id = win.parts.chatAppliers.addImageProjectEntry(img, QStringLiteral("thumb-row"));
    QVERIFY(!id.isEmpty());
    stencil::gui::Project* pr = win.findProject(id.toStdString());
    QVERIFY(pr);
    stencil::core::Line line;
    line.points = {{0, 800}, {2400, 800}};
    line.color = "#ff0000";
    line.thickness = 60;
    pr->lines = {line};
    pr->cropRect = stencil::core::CropRect{0, 0, 2400, 1600};
    pr->meta.updatedAt += 1;

    bool placeholderFirst = false;
    QPixmap arrived;
    QTimer::singleShot(0, [&] {
      QListWidget* list = openList();
      placeholderFirst = rowOf(list, id) && thumbOf(rowOf(list, id)).isNull();
      for (int i = 0; i < 500 && thumbOf(rowOf(list, id)).isNull(); ++i) QTest::qWait(10);
      arrived = thumbOf(rowOf(list, id));
      closeDialog();
    });
    win.parts.projects.openProjects();
    QVERIFY2(placeholderFirst, "the row waited for its picture instead of opening at once");
    QVERIFY2(!arrived.isNull(), "the row's picture never arrived");
    QCOMPARE(arrived.size(), QSize(320, 213));
    const QImage shot = arrived.toImage();
    const QColor mid = shot.pixelColor(160, 106);
    QVERIFY2(mid.red() > 200 && mid.green() < 80, "the line was not composed onto the thumbnail");
    QCOMPARE(shot.pixelColor(160, 20), QColor(Qt::white));

    bool cachedAtOnce = false;
    QTimer::singleShot(0, [&] {
      QListWidget* list = openList();
      cachedAtOnce = !thumbOf(rowOf(list, id)).isNull();
      closeDialog();
    });
    win.parts.projects.openProjects();
    QVERIFY2(cachedAtOnce, "an unchanged project was composed again instead of served cached");

    // An edit changes what the picture shows, so the next open composes it afresh.
    pr->lines.clear();
    pr->meta.updatedAt += 1;
    bool staleServed = true;
    QPixmap redone;
    QTimer::singleShot(0, [&] {
      QListWidget* list = openList();
      staleServed = !thumbOf(rowOf(list, id)).isNull();
      for (int i = 0; i < 500 && thumbOf(rowOf(list, id)).isNull(); ++i) QTest::qWait(10);
      redone = thumbOf(rowOf(list, id));
      closeDialog();
    });
    win.parts.projects.openProjects();
    QVERIFY2(!staleServed, "an edited project was served its old picture");
    QVERIFY(!redone.isNull());
    QCOMPARE(redone.toImage().pixelColor(160, 106), QColor(Qt::white));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsThumbs.gui.moc"
