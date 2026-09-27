// MainWindow GUI e2e — A picture file dropped on the chat composer decodes off the GUI thread: it
// becomes a chip, or — its header read but its pixels would not — a failure toast, as the browser's
// panel.js says "Attachment failed — …". Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include <QImageReader>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // A PNG whose signature and header read, cut off inside its pixel data.
  static QString brokenPng() {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::magenta);
    img.save(&buf, "PNG");
    const QString path = QDir::temp().filePath(QStringLiteral("stencil_e2e_broken.png"));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(bytes.left(60));
    return path;
  }

  // The drag the composer's input box takes: enter, then drop, both through its event filter.
  static bool dropOnComposer(stencil::gui::ChatDock* dock, const QString& path) {
    auto* inputArea = dock->findChild<QWidget*>("chatInputArea");
    auto* inputBox = dock->findChild<QPlainTextEdit*>("chatInput");
    if (!inputArea || !inputBox) return false;
    const QPoint at = inputBox->mapTo(inputArea, inputBox->rect().center());
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(inputArea, &enter);
    QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(inputArea, &drop);
    return drop.isAccepted();
  }

  // Every toast now up that says `part`, with its level (NotifyLevel: 2 = error).
  static QList<int> toastLevels(MainWindow& win, const QString& part) {
    QList<int> out;
    for (QWidget* t : win.findChildren<QWidget*>(QStringLiteral("toast")))
      if (t->property("stencilToastText").toString().contains(part))
        out << t->property("stencilToastLevel").toInt();
    return out;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aDroppedFileThatWillNotDecodeIsReported() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.parts.dockChrome.setChatShown(true, false);
    auto* dock = win.chatDock;
    QVERIFY(dock);
    const QString broken = brokenPng();
    QVERIFY2(QImageReader(broken).canRead() && QImage(broken).isNull(),
             "the fixture must read as a PNG and still fail to decode");

    // A good file becomes a chip once it lands, and says nothing.
    QVERIFY2(dropOnComposer(dock, guiTestImage()), "the composer refused a readable picture");
    QTRY_COMPARE_WITH_TIMEOUT(dock->attachedImages().size(), 1, 5000);
    QVERIFY(toastLevels(win, QStringLiteral("Attachment failed")).isEmpty());

    // The broken one is taken on its header, so it can no longer be refused: it is reported.
    QVERIFY2(dropOnComposer(dock, broken), "the composer refused a file whose header reads");
    QTRY_COMPARE_WITH_TIMEOUT(toastLevels(win, QStringLiteral("Attachment failed — stencil_e2e_broken.png")),
                              QList<int>{2}, 5000);
    QCOMPARE(dock->attachedImages().size(), 1);   // and nothing was queued for it
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatAttach.gui.moc"
