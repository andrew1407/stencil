// MainWindow GUI e2e — The theme lens (app/theme/ThemeLens, ThemePainterLens.cpp; browser twin
// ui/drag/themeLens.js): dragging the switch opens a disc on the other theme, photographed with no
// event-loop turn between its two restyles and nothing stored, the picture inverted under it; none
// opens over a wipe in flight or under the webcore skin. The release: MainWindow.themeLensDrop.gui.cpp.
#include "themeLensGui.hpp"
#include "ThemeLens.hpp"
#include "iconDrag.hpp"
#include "skinPrefs.hpp"

using stencil::gui::ThemeLens;
using stencil::support::iconDragActive;

namespace {
  // Counts the window's update requests, and those handled while `during` holds: a backing-store
  // flush can only follow one.
  class UpdateProbe : public QObject {
   public:
    explicit UpdateProbe(std::function<bool()> during) : during(std::move(during)) {}
    int seen = 0;
    int inside = 0;

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() == QEvent::UpdateRequest) {
        ++seen;
        if (during()) ++inside;
      }
      return QObject::eventFilter(o, e);
    }

   private:
    std::function<bool()> during;
  };

  QColor pixel(const QImage& shot, const QPoint& windowPx) {
    const qreal dpr = shot.devicePixelRatio();
    return shot.pixelColor(qRound(windowPx.x() * dpr), qRound(windowPx.y() * dpr));
  }

  // A button-sized, mouse-transparent child stacked after the lens: the switch's ghost.
  bool ghostAbove(const QWidget& win, QWidget* lens, const QSize& size) {
    const auto kids = win.findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
    for (qsizetype i = kids.indexOf(lens) + 1; i < kids.size(); ++i)
      if (kids[i]->isVisible() && kids[i]->testAttribute(Qt::WA_TransparentForMouseEvents) && kids[i]->size() == size)
        return true;
    return false;
  }
  // Counts restyles reaching one widget: a photograph of the other theme sends two.
  struct StyleProbe : QObject {
    int seen = 0;
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() == QEvent::StyleChange) ++seen;
      return QObject::eventFilter(o, e);
    }
  };
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static QToolButton* themeSwitch(MainWindow& win) { return switchFor(win, win.acts.theme); }
  static void storeTheme(MainWindow& win, const QString& mode) {
    Settings s = win.settings;
    s.themeMode = mode;
    win.applySettings(s, /*persist=*/true);
  }
  // The header row's empty right end: one plain ground the two themes paint differently.
  static QPoint groundOf(MainWindow& win) {
    QToolBar* bar = win.tools.headerToolbar;
    return bar->mapTo(&win, QPoint(bar->width() - 12, bar->height() / 2));
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theLensShowsTheOtherThemeAndTheLiveOneStays() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    const QString mode = win.settings.themeMode;
    const bool dark = win.painted.dark;
    const QString sheet = qApp->styleSheet();
    const std::optional<bool> forced = stencil::support::forcedDark();
    const QByteArray stored = storedSettings();
    const QPoint ground = groundOf(win);
    const QPoint picture = canvas->mapTo(&win, canvas->rect().center());
    const QImage before = win.grab().toImage();

    liftIcon(button);
    iconMouse(button, QEvent::MouseMove, win.mapToGlobal(ground));
    QWidget* lens = lensOf(win);
    QVERIFY2(lens && lens->isVisible() && iconDragActive(button), "the drag opens the lens");
    QVERIFY2(ghostAbove(win, lens, button->size()), "…with the switch's ghost riding above it");
    QCOMPARE(lens->pos() + QPoint(ThemeLens::REACH, ThemeLens::REACH), ground);
    QVERIFY2(win.painted.dark == dark && qApp->styleSheet() == sheet && stencil::support::forcedDark() == forced,
             "its photograph put the live theme back");
    QVERIFY2(storedSettings() == stored && win.settings.themeMode == mode, "…and stored nothing");
    const QImage overGround = win.grab().toImage();
    iconMouse(button, QEvent::MouseMove, win.mapToGlobal(picture));
    QCOMPARE(lens->pos() + QPoint(ThemeLens::REACH, ThemeLens::REACH), picture);
    const QImage overPicture = win.grab().toImage();
    const QPoint beside = picture + QPoint(0, ThemeLens::RADIUS / 2);   // the switch's ghost rides the centre
    QVERIFY2(nearColor(pixel(before, beside), Qt::white, 8) && nearColor(pixel(overPicture, beside), Qt::black, 8),
             "over the picture, which has no theme, the lens inverts it");
    pressEscape(win);
    QVERIFY2(!lensOf(win) && win.painted.dark == dark, "Escape takes the lens down and flips nothing");

    storeTheme(win, dark ? QStringLiteral("light") : QStringLiteral("dark"));
    const QImage other = win.grab().toImage();
    const QPoint probe = ground - QPoint(40, 0);   // beside the switch's ghost, which rides the centre
    QVERIFY2(!nearColor(pixel(before, probe), pixel(other, probe), 6), "the two themes differ at the probe");
    QVERIFY2(nearColor(pixel(overGround, probe), pixel(other, probe), 6), "the lens showed the other theme");
    storeTheme(win, mode);
  }

  // The photograph's two restyles share one turn: no update request is handled while the other
  // theme is on, so no frame of it can reach the screen. The button's own repaints (its press, the
  // drag's move away) flush the live theme, and show the probe hears them.
  void noEventLoopTurnSeparatesTheTwoRestyles() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    UpdateProbe probe([&win] { return win.painted.silent; });
    win.installEventFilter(&probe);
    win.windowHandle()->installEventFilter(&probe);
    win.update();
    iconMouse(button, QEvent::MouseButtonPress, iconCentre(button));
    QElapsedTimer took;
    took.start();
    iconMouse(button, QEvent::MouseMove, iconCentre(button) + QPoint(0, 40));
    qInfo("the lens's photograph took %lld ms", static_cast<long long>(took.elapsed()));
    QVERIFY(lensOf(win) && !win.painted.silent);
    QCoreApplication::processEvents();
    QVERIFY2(probe.seen > 0, "the probe hears the window's updates");
    QCOMPARE(probe.inside, 0);
    pressEscape(win);
    dropIcon(button, iconCentre(button) + QPoint(0, 40));
    win.removeEventFilter(&probe);
    win.windowHandle()->removeEventFilter(&probe);
  }

  // Reaching the switch photographs the other theme at idle, so the drag's start restyles nothing.
  void reachingTheSwitchPrimesTheLens() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win)->hasImage());
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    QElapsedTimer t;
    t.start();
    liftIcon(button);
    const qint64 cold = t.elapsed();
    pressEscape(win);
    dropIcon(button, iconCentre(button));
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(button, &enter);
    QCoreApplication::processEvents();
    StyleProbe probe;
    button->installEventFilter(&probe);
    t.restart();
    liftIcon(button);
    const qint64 warm = t.elapsed();
    QVERIFY2(lensOf(win) && probe.seen == 0, "the primed photograph opens the lens with no restyle");
    t.restart();
    QCoreApplication::processEvents();
    const qint64 firstPaint = t.elapsed();
    qInfo("lens start: %lld ms cold, %lld ms primed, first paint %lld ms", static_cast<long long>(cold),
          static_cast<long long>(warm), static_cast<long long>(firstPaint));
    button->removeEventFilter(&probe);
    pressEscape(win);
    dropIcon(button, iconCentre(button));
  }

  void aWipeInFlightOpensNoLens() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTRY_COMPARE(wipesOn(win), 0);
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    const QString mode = win.settings.themeMode;
    win.parts.theme.toggleTheme();
    QCOMPARE(wipesOn(win), 1);
    liftIcon(button);
    QVERIFY2(!lensOf(win) && iconDragActive(button), "no lens is photographed over a wipe in flight");
    pressEscape(win);
    dropIcon(button, iconCentre(button) + QPoint(0, 40));
    QTRY_VERIFY_WITH_TIMEOUT(wipesOn(win) == 0, 3000);
    storeTheme(win, mode);
  }

  // As in the browser, the skin opens no lens; the drag runs on and its release switches nothing.
  void underTheWebcoreSkinTheDragOpensNoLens() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    QVERIFY(win.parts.theme.toggleWebcore());
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    const bool dark = win.painted.dark;
    liftIcon(button);
    QVERIFY2(iconDragActive(button) && !lensOf(win), "the drag runs without a lens");
    dropIcon(button, iconCentre(button) + QPoint(-200, 240));
    QVERIFY2(win.painted.dark == dark, "…and its release switches nothing");
    QVERIFY(!win.parts.theme.toggleWebcore());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeLens.gui.moc"
