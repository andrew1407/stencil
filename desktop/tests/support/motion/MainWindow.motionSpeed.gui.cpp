// MainWindow GUI e2e — The Animation speed: its Settings row, and the flights and hover feedback it scales.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/notify/notificationsParts.hpp"
#include "../../../src/support/motion/ShimmerOverlay.hpp"
#include "../../../src/canvas/overlay/QuarterTurnOverlay.hpp"
#include "../../../src/support/control/dblReset.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // The rise a fresh toast plays, read the moment it starts (it deletes itself when done).
  // Each probe says something new: a repeated message only extends the standing toast.
  static int toastRiseMs(MainWindow& win) {
    static int probe = 0;
    const QString text = QStringLiteral("speed probe %1").arg(++probe);
    win.notify->info(text);
    for (QLabel* toast : win.findChildren<QLabel*>(QStringLiteral("toast")))
      if (toast->property(stencil::gui::TEXT_PROPERTY).toString() == text)
        if (auto* rise = toast->findChild<QPropertyAnimation*>(QStringLiteral("toastRise")))
          return rise->duration();
    return -1;
  }

  static void applySpeed(MainWindow& win, double speed) {
    Settings s = win.settings;
    s.motionSpeed = speed;
    win.applySettings(s, /*persist=*/false);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void motionSpeedRowClampsAppliesAndResets() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::SettingsDialog dlg(win.settings, &win);
    double applied = -1;
    int applies = 0;
    dlg.setOnChange([&applied, &applies](const Settings& s) { applied = s.motionSpeed; ++applies; });
    dlg.show();
    auto* slider = dlg.findChild<QSlider*>(QStringLiteral("motionSpeedSlider"));
    auto* spin = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("motionSpeedSpin"));
    QVERIFY(slider && spin);
    QCOMPARE(slider->minimum(), 10);
    QCOMPARE(slider->maximum(), 100);
    QCOMPARE(slider->value(), 37);
    QCOMPARE(spin->text(), QStringLiteral("0.37"));
    QVERIFY2(slider->toolTip().isEmpty() && spin->toolTip().isEmpty(), "the speed row carries no hover title");

    // A drag moves the speed itself on every tick; the whole-window apply and save run once it rests.
    for (int v = 38; v <= 100; ++v) slider->setValue(v);
    QCOMPARE(stencil::support::motionSpeed(), 1.0);
    QCOMPARE(spin->text(), QStringLiteral("1.00"));
    QCOMPARE(applies, 0);
    QTRY_COMPARE(applied, 1.0);
    QCOMPARE(applies, 1);

    auto* field = spin->findChild<QLineEdit*>();
    QVERIFY(field);
    const auto type = [&](const QString& text) {
      field->setText(text);
      QTest::keyClick(spin, Qt::Key_Return);
    };
    slider->setValue(50);
    type(QStringLiteral("5"));   // past the fastest end
    QCOMPARE(spin->text(), QStringLiteral("1.00"));
    QCOMPARE(slider->value(), 100);
    QTRY_COMPARE(applied, 1.0);
    type(QStringLiteral("0.05"));   // past the slowest
    QCOMPARE(spin->text(), QStringLiteral("0.10"));
    QCOMPARE(slider->value(), 10);
    QTRY_COMPARE(applied, 0.1);

    QPushButton* reset = nullptr;
    for (QPushButton* b : dlg.findChildren<QPushButton*>())
      if (b->text() == QStringLiteral("Reset All")) reset = b;
    QVERIFY(reset);
    reset->click();
    QCOMPARE(slider->value(), 37);
    QCOMPARE(spin->text(), QStringLiteral("0.37"));
    QCOMPARE(applied, 0.37);
    dlg.reject();
  }

  // A row's title stands for its control: a double-click on it puts the default back (browser dblReset.js).
  void settingsRowTitlesResetTheirRow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::SettingsDialog dlg(win.settings, &win);
    Settings applied;
    dlg.setOnChange([&applied](const Settings& s) { applied = s; });
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    const auto caption = [&dlg](const QString& text) -> QLabel* {
      for (QLabel* l : dlg.findChildren<QLabel*>())
        if (l->text() == text && l->property(stencil::support::CAPTION_FIELD_PROPERTY).isValid()) return l;
      return nullptr;
    };
    auto* slider = dlg.findChild<QSlider*>(QStringLiteral("motionSpeedSlider"));
    QVERIFY(slider);
    slider->setValue(90);
    QLabel* speedTitle = caption(QStringLiteral("Animation speed"));
    QVERIFY(speedTitle);
    QCOMPARE(speedTitle->cursor().shape(), Qt::ArrowCursor);   // a title, not a link
    for (QLabel* l : dlg.findChildren<QLabel*>())
      QVERIFY2(l->text() != QStringLiteral("Dim and blur behind windows"), "Multiple windows alone decides the backdrop");
    QTest::mouseDClick(speedTitle, Qt::LeftButton);
    QTRY_COMPARE(slider->value(), 37);
    QCOMPARE(applied.motionSpeed, 0.37);

    QLabel* thicknessTitle = caption(QStringLiteral("Line thickness"));
    QVERIFY(thicknessTitle);
    auto* thickness = qobject_cast<QDoubleSpinBox*>(
        thicknessTitle->property(stencil::support::CAPTION_FIELD_PROPERTY).value<QObject*>());
    QVERIFY(thickness);
    thickness->setValue(9);
    QTest::mouseDClick(thicknessTitle, Qt::LeftButton);
    QTRY_COMPARE(thickness->value(), Settings().defaultThickness);
    dlg.reject();
  }

  // The default plays the tuned length to the millisecond; the fastest end shortens the same flight.
  void motionSpeedScalesARealFlight() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    const int tuned = stencil::gui::FADE_IN_MS + 80;

    applySpeed(win, 0.37);
    QCOMPARE(toastRiseMs(win), tuned);

    applySpeed(win, 1.0);
    QCOMPARE(stencil::support::motionSpeed(), 1.0);
    const int fast = toastRiseMs(win);
    QCOMPARE(fast, stencil::support::motionMs(tuned));
    QVERIFY2(fast < tuned / 2, qPrintable(QStringLiteral("rise %1 ms at the fastest").arg(fast)));

    applySpeed(win, 7.0);   // out of range: the window keeps the clamped value
    QCOMPARE(win.settings.motionSpeed, 1.0);

    applySpeed(win, 0.37);
    QCOMPARE(toastRiseMs(win), tuned);
  }

  // Hover feedback follows the speed too: a button's glass sweep, read as it starts.
  void motionSpeedScalesAHoverSweep() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restore = qScopeGuard([&] {
      if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
      stencil::support::setMotionSpeed(0.37);
    });
    QWidget host;
    auto* btn = new QPushButton(QStringLiteral("Sweep"), &host);
    btn->resize(120, 30);
    auto* sweep = new stencil::gui::ShimmerOverlay(btn);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto sweepMs = [&] {
      QEvent leave(QEvent::Leave);
      QCoreApplication::sendEvent(btn, &leave);
      QEvent enter(QEvent::Enter);
      QCoreApplication::sendEvent(btn, &enter);
      auto* anim = sweep->findChild<QVariantAnimation*>();
      return anim ? anim->duration() : -1;
    };
    stencil::support::setMotionSpeed(0.37);
    QCOMPARE(sweepMs(), 325);
    stencil::support::setMotionSpeed(1.0);
    QCOMPARE(sweepMs(), stencil::support::motionMs(325));
    QVERIFY(sweepMs() < 325 / 2);
  }

  // The picture's quarter turn: the tuned ROTATE_MS at the default, 0.37 of it at the fastest.
  void motionSpeedScalesTheRotateFlight() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restore = qScopeGuard([&] {
      if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
      stencil::support::setMotionSpeed(0.37);
    });
    QWidget viewport;
    viewport.resize(300, 200);
    auto* canvas = new QWidget(&viewport);
    canvas->setGeometry(20, 20, 160, 100);
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));
    const auto flightMs = [&] {
      using stencil::gui::QuarterTurnOverlay;
      QuarterTurnOverlay::play(canvas, &viewport, QuarterTurnOverlay::boxIn(canvas, &viewport), 1);
      QuarterTurnOverlay* fx = nullptr;
      for (QWidget* w : viewport.findChildren<QWidget*>(QStringLiteral("quarterTurnOverlay")))
        fx = dynamic_cast<QuarterTurnOverlay*>(w);
      return fx ? fx->spanMs() : -1;
    };
    const int tuned = stencil::gui::quarterTurn::clock().ms;
    stencil::support::setMotionSpeed(0.37);
    QCOMPARE(flightMs(), tuned);
    stencil::support::setMotionSpeed(1.0);
    QCOMPARE(flightMs(), stencil::support::motionMs(tuned));
    QVERIFY(flightMs() < tuned / 2);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motionSpeed.gui.moc"
