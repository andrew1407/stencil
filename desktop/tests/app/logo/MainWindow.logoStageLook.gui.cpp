// MainWindow GUI e2e — what a running logo show wears: its cloud, light and mark re-resolved live
// when the skin or the motion mode moves, and the cloud hugging the mark evenly in every style.
// Shared ground is in MainWindow.gui.hpp; the hold and the lock are in MainWindow.logoStage.
#include "../../MainWindow.gui.hpp"
#include "LogoStage.hpp"

#include <cmath>

using stencil::gui::LogoStage;
namespace support = stencil::support;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static LogoStage* stageOf(MainWindow& win) { return win.findChild<LogoStage*>("logoStage"); }
  static bool shown(MainWindow& win) {
    win.resize(900, 700);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }
  static void setMode(MainWindow& win, const QString& mode) {
    win.settings.motionMode = mode;
    win.applySettings(win.settings, /*persist=*/false);
  }
  // The mark the window would make right now, at the size the stage built its own.
  static bool markIsCurrent(MainWindow& win, LogoStage* stage) {
    const QPixmap& art = stage->markArt();
    const int px = qRound(art.deviceIndependentSize().width());
    return !art.isNull() && art.toImage() == win.makeLogoPixmap(px).toImage();
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aRunningShowTakesTheSkinsLookBothWaysWithoutClosing() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    for (const auto& [mode, style] : {std::pair{QStringLiteral("particles"), support::ParticleStyle::DUST},
                                      std::pair{QStringLiteral("water"), support::ParticleStyle::WATER}}) {
      setMode(win, mode);
      LogoStage* stage = stageOf(win);
      QVERIFY(stage->activateByName("randomWalk"));
      QTRY_VERIFY(stage->stageCloud().live() > 0);
      QVERIFY(stage->cloudy() && stage->cloudStyle() == style && stage->glowing());
      const QImage plain = stage->markArt().toImage();

      QVERIFY(win.toggleWebcore());
      QTRY_VERIFY2(!stage->cloudy(), "the skin stills the interface, so the cloud goes");
      QCOMPARE(stage->stageCloud().live(), 0);
      QVERIFY2(!stage->glowing(), "…and with no cloud a roaming show has no light");
      QVERIFY2(markIsCurrent(win, stage) && stage->markArt().toImage() != plain, "the pixel mark, at once");
      QVERIFY(stage->isOpen() && stage->showName() == QLatin1String("randomWalk"));

      QVERIFY(!win.toggleWebcore());
      QTRY_VERIFY2(stage->cloudy(), "the stored motion is back, and the cloud with it");
      QCOMPARE(stage->cloudStyle(), style);
      QVERIFY(stage->glowing());
      QVERIFY2(markIsCurrent(win, stage) && stage->markArt().toImage() == plain, "the plain mark again");
      QVERIFY(stage->isOpen() && stage->showName() == QLatin1String("randomWalk"));
      QTRY_VERIFY(stage->stageCloud().live() > 0);
      stage->dismiss();
      QTest::qWait(450);
    }
  }

  // A styled show flies its own style whatever the mode, the skin's included.
  void aStyledShowKeepsItsOwnCloudUnderTheSkin() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    setMode(win, QStringLiteral("particles"));
    LogoStage* stage = stageOf(win);
    QVERIFY(stage->activateByName("firework"));
    QVERIFY(win.toggleWebcore());
    QTest::qWait(80);
    QVERIFY(stage->cloudy() && stage->cloudStyle() == support::ParticleStyle::FIRE);
    QVERIFY(!win.toggleWebcore());
    stage->dismiss();
  }

  // With motion none only the light shows shine; a roaming or bouncing mark stands unlit.
  void withMotionOffOnlyTheLightShowsGlow() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    setMode(win, QStringLiteral("none"));
    LogoStage* stage = stageOf(win);
    for (const char* name : {"neonOn", "makeSomeSunshine", "randomWalk", "chaseMe", "runaway", "makeItSmall"}) {
      QVERIFY(stage->activateByName(name));
      QTest::qWait(40);
      const bool light = QByteArray(name) == "neonOn" || QByteArray(name) == "makeSomeSunshine";
      QVERIFY2(stage->glowing() == light, name);
      QVERIFY2(!stage->cloudy(), name);
    }
    stage->dismiss();
  }

  // Every grain's drawn place, gathered over several frames: centred on the mark and as far out
  // on every side, whichever style is flying.
  void theCloudHugsTheMarkEvenlyInEveryStyle() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    for (const QString& mode : {QStringLiteral("particles"), QStringLiteral("water"), QStringLiteral("fire")}) {
      setMode(win, mode);
      LogoStage* stage = stageOf(win);
      QVERIFY(stage->activateByName("randomWalk"));
      QTest::qWait(900);
      double sx = 0, sy = 0, x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
      int n = 0;
      for (int frame = 0; frame < 6; ++frame, QTest::qWait(60)) {
        for (const support::StageMote& m : stage->stageCloud().list()) {
          const QPointF at = stage->stageCloud().placed(m, frame * 60.0);
          sx += at.x(); sy += at.y(); ++n;
          x0 = std::min(x0, at.x()); x1 = std::max(x1, at.x());
          y0 = std::min(y0, at.y()); y1 = std::max(y1, at.y());
        }
      }
      QVERIFY2(n > 500, qPrintable(mode));
      const double size = stage->markSize();
      const QString why = QStringLiteral("%1: centroid (%2, %3), box x %4..%5 y %6..%7, mark %8")
          .arg(mode).arg(sx / n).arg(sy / n).arg(x0).arg(x1).arg(y0).arg(y1).arg(size);
      QVERIFY2(std::abs(sx / n) < 0.04 * size && std::abs(sy / n) < 0.04 * size, qPrintable(why));
      QVERIFY2(std::abs(x0 + x1) < 0.15 * size && std::abs(y0 + y1) < 0.15 * size, qPrintable(why));
      stage->dismiss();
      QTest::qWait(450);
    }
  }

  // The mark's frame, found by its colour in the painted stage, sits square on markPos.
  void theMarkIsPaintedCentredOnItsPlace() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    win.settings.accentColor = QStringLiteral("#00ff00");
    LogoStage* stage = stageOf(win);
    QVERIFY(stage->activateByName("randomWalk"));
    QTest::qWait(40);
    const QImage shot = stage->grab().toImage();
    int x0 = shot.width(), x1 = -1, y0 = shot.height(), y1 = -1;
    for (int y = 0; y < shot.height(); ++y)
      for (int x = 0; x < shot.width(); ++x) {
        const QColor c = shot.pixelColor(x, y);
        if (c.green() < 200 || c.red() > 80 || c.blue() > 80) continue;
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
      }
    QVERIFY2(x1 > x0 && y1 > y0, "the mark's frame is not on the stage");
    const QPointF centre((x0 + x1 + 1) / 2.0, (y0 + y1 + 1) / 2.0), at = stage->markPos();
    QVERIFY2(std::abs(centre.x() - at.x()) <= 1.0 && std::abs(centre.y() - at.y()) <= 1.0,
             qPrintable(QStringLiteral("frame centre (%1, %2) vs markPos (%3, %4)")
                            .arg(centre.x()).arg(centre.y()).arg(at.x()).arg(at.y())));
    QVERIFY(std::abs((x1 - x0) - (y1 - y0)) <= 1);
    stage->dismiss();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.logoStageLook.gui.moc"
