// MainWindow GUI e2e — creating a server project against the in-process REST stand-in: the canvas
// goes up as the project's original, its PNG encoded off the GUI thread, and the window links it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../support/connectNow.hpp"
#include "../../support/mockRest.hpp"
#include "../../../src/app/remote/RemoteSession.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void createOnServerUploadsTheOriginalAndLinks() {
    stencil::test::MockRest mock;
    QVERIFY(mock.listen());
    QPointer<MainWindow> win = new MainWindow(nullptr, false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    CanvasWidget* canvas = openLoaded(*win);
    QVERIFY(canvas->hasImage());
    QString err;
    QVERIFY2(stencil::test::connectNow(*win->ensureConnections(), mock.url(), QString(), err),
             qPrintable(err));

    bool linked = false;
    win->parts.projects.createServerProject(mock.url(), QStringLiteral("Published"), [&] { linked = true; });
    QTRY_VERIFY(linked);
    QCOMPARE(mock.created, 1);
    const QImage uploaded = QImage::fromData(mock.projects.value(QStringLiteral("p1")).original, "PNG");
    QCOMPARE(uploaded.size(), QSize(canvas->imageWidth(), canvas->imageHeight()));
    QCOMPARE(uploaded.pixelColor(12, 12), canvas->getImage().pixelColor(12, 12));
    QCOMPARE(win->remote.session->getLink().id, QStringLiteral("p1"));

    win->close();
    QTRY_VERIFY(win.isNull());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.publish.gui.moc"
