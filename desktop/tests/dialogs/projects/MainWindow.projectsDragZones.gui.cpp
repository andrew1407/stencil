// MainWindow GUI e2e — The Projects window's drag-out zones look as the browser's: the zone boxes,
// their muted fills, dashes and labels in both themes (the values computed in Chrome), the labels in
// their outer corners, and the over state. Browser twin: css/components/projects/projects.css .pdz*.
#include "projectsHeld.gui.hpp"
#include "../../../src/dialogs/projects/list/ProjectDragZones.hpp"

using Zones = stencil::gui::ProjectDragZones;
using Zone = Zones::Zone;

namespace {

  bool same(const QColor& a, const QColor& b, int tol = 1) {
    return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol &&
           std::abs(a.blue() - b.blue()) <= tol && std::abs(a.alpha() - b.alpha()) <= tol;
  }

  // Chrome's computed color(srgb r g b / a), as 8-bit.
  QColor srgb(double r, double g, double b, double a = 1.0) { return QColor::fromRgbF(r, g, b, a); }

  Zones* zonesOf(QWidget* win) {
    for (QWidget* c : win->findChildren<QWidget*>())
      if (auto* z = dynamic_cast<Zones*>(c)) return z;
    return nullptr;
  }

  // `fill` laid over black, as render() onto a black image lays it.
  QColor overBlack(const QColor& fill) {
    return QColor(qRound(fill.red() * fill.alphaF()), qRound(fill.green() * fill.alphaF()),
                  qRound(fill.blue() * fill.alphaF()));
  }

  // Ink across a `px` folder glyph's bottom edge at `dpr`, in logical px: its stroke. Chrome's
  // icon('folder', {size: 22}) measures 2 at 1x (pixel-snapped) and 1.75 at 2x; 2·22/24 = 1.83.
  double folderStroke(qreal dpr, double px = 22) {
    QImage img(QSize(22, 22) * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::white);
    QPainter g(&img);
    g.setRenderHint(QPainter::Antialiasing, true);
    Zones::drawGlyph(g, QStringLiteral("folder"), Qt::black, QPointF(11, 11), px);
    g.end();
    double sum = 0;
    for (double fx : {0.4, 0.5, 0.6}) {
      const int x = static_cast<int>(img.width() * fx);
      for (int y = static_cast<int>(img.height() * 0.7); y < img.height(); ++y) sum += (255 - qGreen(img.pixel(x, y))) / 255.0;
    }
    return sum / 3 / dpr;
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void zoneBoxesAndColoursAreTheBrowsers() {
    const QSize page(1280, 860);
    QCOMPARE(Zones::zoneRect(Zone::HERE, page), QRect(0, 0, 632, 602));
    QCOMPARE(Zones::zoneRect(Zone::NEW_WINDOW, page), QRect(648, 0, 632, 602));
    QCOMPARE(Zones::zoneRect(Zone::REMOVE, page), QRect(0, 612, 1280, 248));

    using stencil::gui::themePalette;
    const auto light = themePalette(false), dark = themePalette(true);
    const auto here = Zones::lookOf(Zones::pdzOf(Zone::HERE, light), light.bgContainer, false);
    QVERIFY(same(here.fill, srgb(0.829804, 0.847373, 0.872627, 0.9)));
    QVERIFY(same(here.dash, srgb(0.392157, 0.454902, 0.545098, 0.72)));
    QVERIFY(same(here.ink, srgb(0.604902, 0.645686, 0.704314)));
    const auto hereOver = Zones::lookOf(Zones::pdzOf(Zone::HERE, light), light.bgContainer, true);
    QVERIFY(same(hereOver.fill, srgb(0.708235, 0.738353, 0.781647, 0.92)));
    const auto removeLight = Zones::lookOf(Zones::pdzOf(Zone::REMOVE, light), light.bgContainer, false);
    QVERIFY(same(removeLight.fill, srgb(0.95498, 0.76502, 0.788079, 0.9)));
    QVERIFY(same(removeLight.ink, srgb(0.89549, 0.45451, 0.508039)));
    const auto newDark = Zones::lookOf(Zones::pdzOf(Zone::NEW_WINDOW, dark), dark.bgContainer, false);
    QVERIFY(same(newDark.fill, srgb(0.142275, 0.210353, 0.359686, 0.9)));
    QVERIFY(same(newDark.ink, srgb(0.444314, 0.602353, 0.94902)));
    const auto removeDark = Zones::lookOf(Zones::pdzOf(Zone::REMOVE, dark), dark.bgContainer, false);
    QVERIFY(same(removeDark.fill, srgb(0.365176, 0.216941, 0.235608, 0.9)));
    QVERIFY(same(removeDark.dash, srgb(0.941176, 0.411765, 0.478431, 0.72)));
    const auto hereDarkOver = Zones::lookOf(Zones::pdzOf(Zone::HERE, dark), dark.bgContainer, true);
    QVERIFY(same(hereDarkOver.fill, srgb(0.261647, 0.291765, 0.335059, 0.92)));
  }

  void zoneGlyphsStrokeAsTheBrowsers() {
    for (const qreal dpr : {1.0, 2.0}) {
      const double px = folderStroke(dpr);
      QVERIFY2(px > 1.65 && px < 2.1, qPrintable(QStringLiteral("@%1x the folder strokes %2px").arg(dpr).arg(px)));
      // At the pulse's 0.8 the stroke shrinks with it (2/24 · 17.6 = 1.47), never thinner: a pixmap
      // rescaled that far read 1.29.
      const double small = folderStroke(dpr, 22 * 0.8);
      QVERIFY2(std::abs(small - 1.467) < 0.12, qPrintable(QStringLiteral("@%1x at 0.8: %2px").arg(dpr).arg(small)));
    }
  }

  void heldRowZonesPaintAsTheBrowsers() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog drags need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkMagenta);
    const QString id = win.parts.chatAppliers.addImageProjectEntry(img, "zones-look");

    QRect here, newWin, remove, hereLabel, newLabel, removeLabel;
    QColor idle, over, idleWant, overWant;
    QTimer::singleShot(0, [&] {
      ProjectsWindow w = projectsWindow();
      if (!w.list) {
        if (w.dlg) w.dlg->reject();
        return;
      }
      w.list->setCurrentItem(rowFor(w.list, id));
      w.list->onDragStart();
      if (Zones* z = zonesOf(&win)) {
        here = Zones::zoneRect(Zone::HERE, z->size());
        newWin = Zones::zoneRect(Zone::NEW_WINDOW, z->size());
        remove = Zones::zoneRect(Zone::REMOVE, z->size());
        hereLabel = z->labelRect(Zone::HERE);
        newLabel = z->labelRect(Zone::NEW_WINDOW);
        removeLabel = z->labelRect(Zone::REMOVE);
        const auto pal = stencil::gui::themePalette(z->palette().color(QPalette::Window).lightness() < 128);
        const QColor pdz = Zones::pdzOf(Zone::HERE, pal);
        idleWant = overBlack(Zones::lookOf(pdz, pal.bgContainer, false).fill);
        overWant = overBlack(Zones::lookOf(pdz, pal.bgContainer, true).fill);
        const QPoint probe(here.left() + 20, here.bottom() - 20);   // clear of label, border and dialog
        const auto shot = [z, probe] {
          QImage black(z->size(), QImage::Format_ARGB32_Premultiplied);
          black.fill(Qt::black);
          z->render(&black, QPoint(), QRegion(), QWidget::DrawChildren);   // no window background
          return black.pixelColor(probe);
        };
        idle = shot();
        holdAt(z->mapToGlobal(probe), 80);
        over = shot();
      }
      holdAt(w.dlg->mapToGlobal(QPoint(w.dlg->width() / 2, w.dlg->height() - 20)));
      w.list->onDragEnd();
      w.dlg->reject();
    });
    win.parts.projects.openProjects();
    QVERIFY2(here.isValid(), "the zones came with the drag");
    QCOMPARE(newWin.top(), here.top());
    QCOMPARE(newWin.left() - here.right() - 1, win.width() % 2 + 16);
    QCOMPARE(remove.height(), qRound(win.height() * 0.3) - 10);
    QCOMPARE(hereLabel.topLeft(), here.topLeft() + QPoint(35, 31));
    QCOMPARE(newLabel.right(), newWin.right() - 35);
    QCOMPARE(newLabel.top(), newWin.top() + 31);
    QCOMPARE(removeLabel.bottom(), remove.bottom() - 25);
    QVERIFY(std::abs(removeLabel.center().x() - remove.center().x()) <= 1);
    QVERIFY2(same(idle, idleWant, 2), qPrintable(idle.name() + " vs " + idleWant.name()));
    QVERIFY2(same(over, overWant, 2), qPrintable(over.name() + " vs " + overWant.name()));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsDragZones.gui.moc"
