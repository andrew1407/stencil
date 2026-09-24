// MainWindow GUI e2e — Ink: the danger red, the muted disabled pixmaps, and a disabled select.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Destructive toolbar buttons wear the browser's `.danger.btn-icon` face: a SOLID red fill with a
  // glyph that is not itself red. A QToolButton re-copies its action's icon on every ActionChanged.
  void dangerToolButtonsAreFilledRed() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QList<QToolButton*> filled;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->property("toolFill").toString() == QLatin1String("danger")) filled << b;
    QVERIFY2(!filled.isEmpty(), "no destructive toolbar button carries the danger fill");

    const QColor danger = stencil::gui::themePalette(false).danger;
    const QColor dangerDark = stencil::gui::themePalette(true).danger;
    // The glyph as the BUTTON draws it: white, and specifically not either danger red.
    const auto glyphIsWhite = [&](QToolButton* b) {
      const QImage im = b->icon().pixmap(18, 18).toImage();
      int white = 0, red = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          if (c.alpha() < 60) continue;
          const auto near = [&c](const QColor& d) {
            return qAbs(c.red() - d.red()) < 45 && qAbs(c.green() - d.green()) < 45
                && qAbs(c.blue() - d.blue()) < 45;
          };
          if (near(QColor(Qt::white))) ++white;
          else if (near(danger) || near(dangerDark)) ++red;
        }
      return white > 0 && red == 0;
    };
    for (QToolButton* b : filled) {
      QVERIFY2(glyphIsWhite(b), qPrintable(QString("%1: the glyph is not white on the red fill")
                                               .arg(b->defaultAction()->text())));
      // Anything that flips the action re-syncs the button — the glyph must survive it.
      QAction* a = b->defaultAction();
      const bool was = a->isEnabled();
      a->setEnabled(!was);
      a->setEnabled(was);
      win.refreshActions();
      QVERIFY2(glyphIsWhite(b), qPrintable(QString("%1: the action's red glyph came back after a refresh")
                                               .arg(a->text())));
      // …while the ACTION — what the menus and the canvas context menu render — keeps the NEUTRAL glyph:
      // the browser paints every menu icon in --text-muted and reserves the red for this filled button.
      const QImage menu = a->icon().pixmap(18, 18).toImage();
      bool menuRed = false;
      for (int y = 0; y < menu.height() && !menuRed; ++y)
        for (int x = 0; x < menu.width(); ++x) {
          const QColor c = menu.pixelColor(x, y);
          if (c.alpha() < 60) continue;
          const auto near = [&c](const QColor& d) {
            return qAbs(c.red() - d.red()) < 45 && qAbs(c.green() - d.green()) < 45
                && qAbs(c.blue() - d.blue()) < 45;
          };
          if (near(danger) || near(dangerDark)) { menuRed = true; break; }
        }
      QVERIFY2(!menuRed,
               qPrintable(QString("%1: the menu entry is still painted danger red").arg(a->text())));
    }
  }
  // A disabled toolbar icon must READ as disabled: `color: MUTED` never reaches a rasterised pixmap, so
  // themedIcon carries a faded QIcon::Disabled variant. Checked at 1x AND 2x.
  void disabledIconsTakeTheMutedInk() {
    const auto inkBox = [](const QImage& im) {
      int minx = im.width(), miny = im.height(), maxx = -1, maxy = -1;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if (im.pixelColor(x, y).alpha() > 20) {
            minx = qMin(minx, x); maxx = qMax(maxx, x);
            miny = qMin(miny, y); maxy = qMax(maxy, y);
          }
      return maxx < 0 ? QRect() : QRect(minx, miny, maxx - minx + 1, maxy - miny + 1);
    };
    const auto meanAlpha = [](const QImage& im) {
      double sum = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) sum += im.pixelColor(x, y).alphaF();
      return sum / (im.width() * im.height());
    };
    for (qreal dpr : {1.0, 2.0}) {
      const QIcon ic = stencil::gui::themedIcon("download", QColor("#e0e0e0"), 18, dpr);
      const QImage on = ic.pixmap(18, 18, QIcon::Normal).toImage();
      const QImage off = ic.pixmap(18, 18, QIcon::Disabled).toImage();
      const QString at = QString(" (at %1x)").arg(dpr);
      QCOMPARE(off.size(), on.size());
      // Same glyph, same place, same size — only the ink is lighter.
      QVERIFY2(inkBox(off) == inkBox(on),
               qPrintable(QString("disabled glyph moved/resized%1: %2 vs %3")
                              .arg(at, QDebug::toString(inkBox(off)), QDebug::toString(inkBox(on)))));
      // …and at FULL strength, re-inked rather than faded: the browser's disabled button paints its .ic in
      // --disabled-text at opacity 1, and a faded dark glyph is a ghost on the light theme's pale chip.
      const double a = meanAlpha(on), b = meanAlpha(off);
      QVERIFY2(b > 0.0, qPrintable("a disabled glyph must still be visible" + at));
      QVERIFY2(b > a * 0.9, qPrintable(QString("faded, not re-inked%1: %2 vs %3").arg(at).arg(a).arg(b)));
      // The ink is the theme's --disabled-text — what the stylesheet greys the LABEL to.
      const auto densest = [](const QImage& im) {
        QColor best;
        int bestA = -1;
        for (int y = 0; y < im.height(); ++y)
          for (int x = 0; x < im.width(); ++x) {
            const QColor c = im.pixelColor(x, y);
            if (c.alpha() > bestA) { bestA = c.alpha(); best = c; }
          }
        return best;
      };
      const QColor muted = QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText);
      const QColor got = densest(off);
      QVERIFY2(qAbs(got.red() - muted.red()) <= 8 && qAbs(got.green() - muted.green()) <= 8
                   && qAbs(got.blue() - muted.blue()) <= 8,
               qPrintable(QString("disabled ink %1, wanted the muted %2%3")
                              .arg(got.name(), muted.name(), at)));
      QVERIFY2(got != QColor("#e0e0e0"), qPrintable("still the enabled colour" + at));
    }
  }
  // A dead combo has to LOOK dead (browser: button:disabled drops to --disabled-bg/--disabled-text). The
  // Qt sheet painted every QComboBox in the live input colours, so a dead picker looked identical.
  void disabledSelectReadsAsDisabled() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    QComboBox* filter = win.compareCombo;   // the tint picker is live before an image; compare is not
    QVERIFY(filter);
    // The face colour, plus how loudly the text/caret stand out against it.
    const auto face = [](QWidget* w) {
      QTest::qWait(30);
      const QImage im = w->grab().toImage();
      const QColor ground = im.pixelColor(2, im.height() / 2);
      double ink = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          ink += qAbs(c.red() - ground.red()) + qAbs(c.green() - ground.green())
                 + qAbs(c.blue() - ground.blue());
        }
      return std::pair<QColor, double>{ground, ink / (im.width() * im.height())};
    };
    QVERIFY2(!filter->isEnabled(), "the filter picker is live with no image loaded");
    const auto dead = face(filter);
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(filter->isEnabled());
    const auto live = face(filter);
    QVERIFY2(dead.first != live.first, "a disabled combo keeps the live input background");
    QVERIFY2(dead.second < live.second * 0.9,
             "a disabled combo's text is as loud as a live one's");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarInk.gui.moc"
