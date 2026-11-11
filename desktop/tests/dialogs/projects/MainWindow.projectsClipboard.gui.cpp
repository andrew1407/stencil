// MainWindow GUI e2e — a projects row's "Copy ›" and "Copy image ›" (app/project/copy/ProjectCopyShare.cpp):
// a stored project that is not open is drawn off-screen in the variant picked — cropped, filtered by
// this window, its lines only in "Current" — and its layout and whole project go out as text.
#include "../../MainWindow.gui.hpp"
#include "ProjectCopy.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace share = stencil::support;
using stencil::gui::CopyRequest;
using stencil::gui::Project;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // A stored row beside the open one: the shared white picture, cropped to 100x60, one red line across it.
  static QString storeRow(MainWindow& win) {
    Project pr;
    pr.meta.id = "share-row";
    pr.meta.name = "Shared";
    pr.imagePath = guiTestImage();
    pr.cropRect = {20, 10, 100, 60};
    stencil::core::Line line;
    line.points = {{10, 30}, {90, 30}};
    line.color = "#ff0000";
    line.thickness = 8;
    pr.lines = {line};
    win.projectHost.projectList.push_back(pr);
    return QStringLiteral("share-row");
  }

  static QImage copiedImage(MainWindow& win, const QString& id, share::ShareItem item) {
    QGuiApplication::clipboard()->clear();
    CopyRequest at;
    at.id = id;
    win.projectHost.copy.copyToClipboard(at, item);
    QImage img;
    for (int i = 0; i < 500 && img.isNull(); ++i) {
      img = QGuiApplication::clipboard()->image();
      if (img.isNull()) QTest::qWait(10);
    }
    return img;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aRowThatIsNotOpenIsDrawnInTheVariantPicked() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage() && !win.projectHost.activeProjectId.isEmpty(), 5000);
    const QString id = storeRow(win);
    QVERIFY(win.projectHost.activeProjectId != id);
    win.settings.imageFilter = QStringLiteral("invert");

    const QImage current = copiedImage(win, id, share::ShareItem::IMAGE_CURRENT);
    QCOMPARE(current.size(), QSize(100, 60));
    QVERIFY2(current.pixelColor(50, 30).red() > 200 && current.pixelColor(50, 30).green() < 60, "the line is drawn");
    QVERIFY2(current.pixelColor(50, 5).lightness() < 40, "under it the white picture is inverted");

    const QImage noLines = copiedImage(win, id, share::ShareItem::IMAGE_TINT);
    QCOMPARE(noLines.size(), QSize(100, 60));
    QVERIFY2(noLines.pixelColor(50, 30).lightness() < 40, "no line, the filter still on");

    const QImage original = copiedImage(win, id, share::ShareItem::IMAGE_ORIGINAL);
    QCOMPARE(original.size(), QSize(100, 60));
    QVERIFY2(original.pixelColor(50, 30).lightness() > 220, "neither line nor filter");
  }

  void aRowsLayoutAndProjectGoOutAsText() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage() && !win.projectHost.activeProjectId.isEmpty(), 5000);
    CopyRequest at;
    at.id = storeRow(win);
    QClipboard* cb = QGuiApplication::clipboard();

    cb->clear();
    win.projectHost.copy.copyToClipboard(at, share::ShareItem::LAYOUT);
    QTRY_VERIFY_WITH_TIMEOUT(!cb->text().isEmpty(), 5000);
    const QJsonObject layout = QJsonDocument::fromJson(cb->text().toUtf8()).object();
    QCOMPARE(layout.value("lines").toArray().size(), 1);

    cb->clear();
    win.projectHost.copy.copyToClipboard(at, share::ShareItem::PROJECT);
    QTRY_VERIFY_WITH_TIMEOUT(!cb->text().isEmpty(), 5000);
    const QJsonObject file = QJsonDocument::fromJson(cb->text().toUtf8()).object();
    QCOMPARE(file.value("format").toString(), QStringLiteral("stencil-project"));
    QCOMPARE(file.value("name").toString(), QStringLiteral("Shared"));

    cb->clear();
    win.projectHost.copy.copyToClipboard(at, share::ShareItem::LINK_NAME, QStringLiteral("stencil://open?project=Shared"));
    QCOMPARE(cb->text(), QStringLiteral("stencil://open?project=Shared"));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsClipboard.gui.moc"
