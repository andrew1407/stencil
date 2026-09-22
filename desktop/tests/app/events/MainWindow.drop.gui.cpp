// MainWindow GUI e2e — what a browser drag hands over, ranked into the order it is tried.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "notificationsParts.hpp"

namespace {
  const QString AVATAR = QStringLiteral("https://avatars.githubusercontent.com/u/43030001?v=4");
  const QString LINK = QStringLiteral("https://github.com/account");

  // What Chrome puts on a logged-in GitHub avatar drag: the WRAPPER link in the url list,
  // the picture itself only in the html fragment.
  void setChromeAvatarDrag(QMimeData& mime, const QString& src) {
    mime.setUrls({QUrl(LINK)});
    mime.setHtml(QStringLiteral("<a href=\"/account\"><img class=\"avatar\" src=\"%1\" /></a>")
                     .arg(src));
    mime.setText(LINK);
  }

  const QString FULL_SIZE = QStringLiteral("https://example.com/full.jpg");
  const QSize BITMAP_SIZE{37, 23};

  // What Chrome hands over beside the urls: the picture it already rendered. An odd size, so
  // the canvas can only be carrying THIS one.
  QImage dragBitmap() {
    QImage img(BITMAP_SIZE, QImage::Format_RGB32);
    img.fill(Qt::red);
    return img;
  }

  bool isBitmapSource(const QString& s) {
    return s.startsWith(QStringLiteral("data:image/png;base64,"));
  }

  // Opening auto-crops to the page aspect, so the width shifts: the height and the fill are
  // what say the canvas is carrying the dragged bitmap and not something fetched.
  bool carriesTheDragBitmap(const CanvasWidget* canvas) {
    const QImage img = canvas ? canvas->getImage() : QImage();
    return img.height() == BITMAP_SIZE.height() && img.pixelColor(0, 0) == QColor(Qt::red);
  }

  // Qt routes a drag to the WINDOW, which picks the child under the point and forwards: a drop
  // sent straight to a widget is discarded, and only this path can be taken by the chat dock.
  void dragTo(MainWindow& win, const QMimeData& mime, const QPoint& at, QEvent::Type type) {
    QDragMoveEvent ev(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier, type);
    QApplication::sendEvent(win.windowHandle(), &ev);
  }

  void dropOn(MainWindow& win, const QMimeData& mime, const QPoint& at) {
    dragTo(win, mime, at, QEvent::DragEnter);   // the enter is what makes the window a drag target
    QDropEvent ev(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(win.windowHandle(), &ev);
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A wrapped avatar must be opened as the PICTURE, not as the account page the <a> points at.
  void aWrappedAvatarRanksTheImgSrcAheadOfItsLink() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QMimeData mime;
    setChromeAvatarDrag(mime, AVATAR);
    dropOn(win, mime, QPoint(120, 300));
    QVERIFY2(win.mediaLoader, "the drop never reached the media loader");
    const QStringList ranked = win.mediaLoader->rankedSources();
    QVERIFY2(ranked.size() >= 2, qPrintable(QStringLiteral("only one candidate: %1")
                                                .arg(ranked.join(QLatin1Char(' ')))));
    QCOMPARE(ranked.value(0), AVATAR);
    QVERIFY2(ranked.contains(LINK), "the link stays on as a fallback");
  }

  // A drag fragment is SERIALISED html, so its src arrives escaped; a literal "&amp;" in the
  // query is a url that resolves to nothing.
  void anEscapedImgSrcIsDecodedBeforeItIsFetched() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QMimeData mime;
    setChromeAvatarDrag(mime, QStringLiteral(
        "https://avatars.githubusercontent.com/u/43030001?s=64&amp;v=4"));
    dropOn(win, mime, QPoint(120, 300));
    QVERIFY(win.mediaLoader);
    QCOMPARE(win.mediaLoader->rankedSources().value(0),
             QStringLiteral("https://avatars.githubusercontent.com/u/43030001?s=64&v=4"));
  }

  // Some sources fill text/x-moz-url ("URL\ntitle") and nothing else.
  void aMozUrlOnlyDragStillCarriesThePicture() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QMimeData mime;
    mime.setUrls({QUrl(LINK)});
    mime.setData(QStringLiteral("text/x-moz-url"),
                 QStringLiteral("https://example.com/cat.png\nCat").toUtf8());
    dropOn(win, mime, QPoint(120, 300));
    QVERIFY2(win.mediaLoader, "a moz-url drag was refused outright");
    QCOMPARE(win.mediaLoader->rankedSources().value(0),
             QStringLiteral("https://example.com/cat.png"));
  }

  // Chrome may hand Qt UTF-16 with a BOM; read by the BOM, the fragment parses like any other.
  void aUtf16HtmlFragmentIsStillRead() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    const QString html =
        QStringLiteral("<a href=\"/account\"><img src=\"%1\" /></a>").arg(AVATAR);
    QByteArray utf16 = QStringEncoder(QStringEncoder::Utf16LE).encode(html);
    utf16.prepend("\xFF\xFE", 2);
    QMimeData mime;
    mime.setUrls({QUrl(LINK)});
    mime.setData(QStringLiteral("text/html"), utf16);
    dropOn(win, mime, QPoint(120, 300));
    QVERIFY(win.mediaLoader);
    QCOMPARE(win.mediaLoader->rankedSources().value(0), AVATAR);
  }

  // A logged-in avatar is wrapped in an <a>, so nothing in the drag NAMES an image — but Chrome
  // handed over the picture itself, and that beats fetching the account page behind it.
  void aDraggedBitmapBeatsAUrlThatNamesNoImage() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QMimeData mime;
    mime.setUrls({QUrl(LINK)});
    mime.setText(LINK);
    mime.setImageData(dragBitmap());
    dropOn(win, mime, QPoint(120, 300));
    QVERIFY2(win.mediaLoader, "the drop never reached the media loader");
    const QStringList ranked = win.mediaLoader->rankedSources();
    QVERIFY2(isBitmapSource(ranked.value(0)),
             qPrintable(QStringLiteral("head is %1").arg(ranked.value(0).left(64))));
    QVERIFY2(ranked.contains(LINK), "the link stays on as a fallback");
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY2(carriesTheDragBitmap(canvas), "the dragged bitmap never opened");
    for (const QLabel* toast : win.findChildren<QLabel*>())
      QVERIFY2(!toast->text().contains(QStringLiteral("Not a readable")),
               qPrintable(toast->text()));
  }

  // A gallery lists the FULL SIZE and renders a thumbnail, so a url that names an image still
  // wins; the bitmap only drops in behind it.
  void aGalleryDragStillPrefersTheUrlThatNamesAnImage() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QMimeData mime;
    mime.setUrls({QUrl(FULL_SIZE)});
    mime.setImageData(dragBitmap());
    dropOn(win, mime, QPoint(120, 300));
    QVERIFY2(win.mediaLoader, "the drop never reached the media loader");
    const QStringList ranked = win.mediaLoader->rankedSources();
    QCOMPARE(ranked.value(0), FULL_SIZE);
    QVERIFY2(isBitmapSource(ranked.value(1)),
             qPrintable(QStringLiteral("second is %1").arg(ranked.value(1).left(64))));
  }

  // Preview and a raw pixel paste carry a bitmap and nothing else; it takes the same one path.
  void aBitmapOnlyDragStillOpens() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QMimeData mime;
    mime.setImageData(dragBitmap());
    dropOn(win, mime, QPoint(120, 300));
    QVERIFY2(win.mediaLoader, "a raw pixel drop must take the media loader path too");
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY2(carriesTheDragBitmap(canvas), "a raw pixel drop must still open");
  }

  // Chrome publishes only the wrapper link for an image behind an <a>, so nothing that crossed
  // is a picture. The toast must blame the DRAG, not an image it never had.
  void aLinkOnlyDropSaysTheDragCarriedALink() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QMimeData mime;
    mime.setUrls({QUrl(LINK)});
    mime.setText(LINK);
    dropOn(win, mime, QPoint(120, 300));
    QTest::qWait(50);

    QVERIFY2(!win.mediaLoader, "a link-only drop must open nothing");
    bool said = false;
    for (const QLabel* toast : win.findChildren<QLabel*>()) {
      if (!toast->text().contains(QStringLiteral("carried a link"))) {
        QVERIFY2(!toast->text().contains(QStringLiteral("Not a readable")),
                 qPrintable(toast->text()));
        continue;
      }
      said = true;
      // A refused drop reads as a failure, not a notice (browser: notify(..., 'fail')).
      for (const QWidget* w = toast; w; w = w->parentWidget()) {
        const QVariant lvl = w->property(stencil::gui::LEVEL_PROPERTY);
        if (!lvl.isValid()) continue;
        QCOMPARE(lvl.toInt(), static_cast<int>(stencil::gui::Notifications::Level::ERROR));
        break;
      }
    }
    QVERIFY2(said, "the toast must say the drag carried a link");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.drop.gui.moc"
