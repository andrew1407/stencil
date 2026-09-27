// MainWindow GUI e2e — the veil a surface wears while its dust stands in for it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "OpenImageDialog.hpp"
#include <QHeaderView>
#include <QTabWidget>
#include <cstdlib>

namespace {
  // The window rendered over a sheet of `sentinel`: what is left of it inside `rect` is what no
  // widget painted, and `ink` counts the pixels still wearing the header's own fill.
  struct Frame { int unpainted = 0; int ink = 0; };

  Frame renderOver(QWidget& win, const QRect& rect, qreal dpr, const QColor& sentinel, const QColor& ink) {
    QImage img(win.size() * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(sentinel);
    win.render(&img);
    Frame f;
    const QRect px(rect.topLeft() * dpr, rect.size() * dpr);
    for (int y = px.top(); y <= px.bottom(); ++y)
      for (int x = px.left(); x <= px.right(); ++x) {
        const QRgb c = img.pixel(x, y);
        if (c == sentinel.rgba()) ++f.unpainted;
        if (c == ink.rgba()) ++f.ink;
      }
    return f;
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The fold veils the panel (panelSurfaceFlight); the header's opaque viewport kept its strip out of
  // the window's repaint, so its last frame stayed up as an accent band over the dust (user report).
  void veiledPanelLeavesNoHeaderBehind() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTRY_VERIFY(win.canvas->hasImage());
    stencil::core::Line line;
    line.color = "#000000";
    line.thickness = 4;
    line.points = {{20.0, 20.0}, {100.0, 100.0}, {150.0, 60.0}};
    win.canvas->setLines({line});
    win.canvas->selectLineByIndex(0);
    auto* tabs = win.selPanel->findChild<QTabWidget*>(QStringLiteral("selectionTabs"));
    QVERIFY(tabs);
    const QColor sentinel(255, 0, 255);
    for (const char* name : {"pointsTable", "linesList"}) {
      auto* table = win.selPanel->findChild<QTableWidget*>(QString::fromLatin1(name));
      QVERIFY(table);
      tabs->setCurrentWidget(table->parentWidget());
      QHeaderView* header = table->horizontalHeader();
      QTRY_VERIFY2(header->isVisible() && table->rowCount() > 0, name);
      QVERIFY(header->viewport()->autoFillBackground());
      const QRect strip(header->mapTo(&win, QPoint(0, 0)), header->size());
      const QImage open = win.grab(strip).toImage();
      // A section's fill: inside its border, above its text.
      const qreal at = open.devicePixelRatio();
      const QColor ink = open.pixelColor(qRound((header->sectionViewportPosition(1) + 3) * at), qRound(2 * at));
      for (const qreal dpr : {1.0, 2.0})
        QCOMPARE(renderOver(win, strip, dpr, sentinel, ink).unpainted, 0);

      // Veiled twice, as an interrupted fold does: the veil it replaces must not undress the new one.
      QPointer<QGraphicsOpacityEffect> veil = stencil::gui::veilBehindDust(win.selPanel);
      veil = stencil::gui::veilBehindDust(win.selPanel);
      QCOMPARE(win.selPanel->graphicsEffect(), veil.data());
      for (const qreal dpr : {1.0, 2.0}) {
        const Frame veiled = renderOver(win, strip, dpr, sentinel, ink);
        QVERIFY2(veiled.unpainted == 0, qPrintable(QStringLiteral("%1 @%2x: %3 px left unpainted under the veil")
                                                       .arg(name).arg(dpr).arg(veiled.unpainted)));
        QVERIFY2(veiled.ink == 0, qPrintable(QStringLiteral("%1 @%2x: the header's fill showed through the veil")
                                                 .arg(name).arg(dpr)));
      }

      // Dropped, the panel is handed back whole: the header paints its own fill again.
      win.selPanel->setGraphicsEffect(nullptr);
      QVERIFY(!veil);
      QVERIFY(header->viewport()->autoFillBackground());
      QCOMPARE(win.grab(strip).toImage(), open);
    }
  }

  // The open-image dialog's pages ease in behind the same veil. None holds an opaque child today,
  // so one is planted: hidden at 0, blended at the veil's own opacity mid-fade, whole after it.
  void pageFadeCarriesAnOpaqueChildInStep() {
    const auto motion = withMotion();
    stencil::gui::OpenImageDialog dlg(nullptr, false, 800, 600);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    QWidget* page = dlg.tabs->currentWidget();
    QVERIFY(page);
    auto* plate = new QWidget(page);
    plate->setAutoFillBackground(true);
    QPalette pal = plate->palette();
    pal.setColor(QPalette::Window, QColor(20, 160, 90));
    plate->setPalette(pal);
    plate->setGeometry(8, 8, 48, 20);
    plate->show();
    QVERIFY(plate->palette().brush(plate->backgroundRole()).isOpaque());
    const QRect box(plate->mapTo(&dlg, QPoint(0, 0)), plate->size());
    const QImage open = dlg.grab(box).toImage();
    const QColor ink = open.pixelColor(open.width() / 2, open.height() / 2);
    const QColor sentinel(255, 0, 255);

    dlg.fadeInCurrentPage();
    auto* veil = qobject_cast<QGraphicsOpacityEffect*>(page->graphicsEffect());
    QVERIFY2(veil, "the page did not fade in");
    for (const qreal dpr : {1.0, 2.0}) {
      const Frame hidden = renderOver(dlg, box, dpr, sentinel, ink);
      QVERIFY2(hidden.unpainted == 0, qPrintable(QStringLiteral("@%1x: %2 px left unpainted under the veil")
                                                     .arg(dpr).arg(hidden.unpainted)));
      QCOMPARE(hidden.ink, 0);
    }
    const auto centre = [&dlg, &box, &sentinel] {
      QImage img(dlg.width(), dlg.height(), QImage::Format_ARGB32_Premultiplied);
      img.fill(sentinel);
      dlg.render(&img);
      return img.pixelColor(box.center());
    };
    const QColor under = centre();
    veil->setOpacity(0.5);   // the fade's own clock has not ticked: no event loop ran since it started
    const QColor mid = centre();
    const auto blend = [](int a, int b) { return (a + b) / 2; };
    QVERIFY2(std::abs(mid.red() - blend(ink.red(), under.red())) <= 3
                 && std::abs(mid.green() - blend(ink.green(), under.green())) <= 3
                 && std::abs(mid.blue() - blend(ink.blue(), under.blue())) <= 3,
             qPrintable(QStringLiteral("mid-fade %1, want half %2 over %3").arg(mid.name(), ink.name(), under.name())));

    QTRY_VERIFY_WITH_TIMEOUT(!page->graphicsEffect(), 2000);
    QVERIFY(plate->autoFillBackground());
    QCOMPARE(dlg.grab(box).toImage(), open);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motionVeil.gui.moc"
