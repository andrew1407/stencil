// MainWindow GUI e2e — The logo stage: the hold that opens it, the lock it puts on the editor,
// and the pink show's edit. Shared ground (helpers, the loaded window) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"
#include "LogoStage.hpp"
#include "logoStageRules.hpp"

using stencil::gui::LogoStage;
namespace support = stencil::support;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  // The hold is seconds long in the table; a test shrinks its timer the way the menu suites do.
  static void holdLogo(MainWindow& win, LogoStage* stage) {
    auto* hold = stage->findChild<QTimer*>("logoHold");
    QVERIFY(hold);
    QToolButton* logo = win.logoBtn;
    const QPoint c = logo->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(c), logo->mapToGlobal(c),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(logo, &press);
    QVERIFY(hold->isActive());
    hold->setInterval(1);
    QTest::qWait(40);
  }
  static LogoStage* stageOf(MainWindow& win) { return win.findChild<LogoStage*>("logoStage"); }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }


  // Violet is the neon show's accent, so a hold opens it — and the accent must NOT cycle.
  void holdingTheMarkOpensItsShowWithoutCyclingTheAccent() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.accentColor = "violet";
    LogoStage* stage = stageOf(win);
    QVERIFY2(stage, "the stage is not installed");
    QCOMPARE(stage->isOpen(), false);
    QCOMPARE(stage->heldShow(), QString("neonOn"));

    holdLogo(win, stage);
    QVERIFY2(stage->isOpen(), "a long hold opens the show");
    QCOMPARE(stage->showName(), QString("neonOn"));
    QVERIFY2(stage->isVisible(), "…and it covers the window");
    QCOMPARE(stage->size(), win.rect().size());

    // The release after a hold is consumed, so the 250ms accent cycle never arms.
    QToolButton* logo = win.logoBtn;
    const QPoint c = logo->rect().center();
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(c), logo->mapToGlobal(c),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(logo, &release);
    QVERIFY2(!win.logoClickTimer->isActive(), "the accent cycle must not follow a hold");
    QCOMPARE(win.settings.accentColor, QString("violet"));
  }

  // While it is up the editor hears nothing; Escape is the way out.
  void whileTheStageIsUpEveryKeyButEscapeIsSwallowed() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.accentColor = "violet";
    LogoStage* stage = stageOf(win);
    QVERIFY(stage->activateByName("neonOn"));

    QKeyEvent key(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, "z");
    QVERIFY2(QApplication::sendEvent(&win, &key), "a plain key is taken by the stage");
    QVERIFY(stage->isOpen());
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&win, &esc);
    QVERIFY2(!stage->isOpen(), "Escape ends it");
  }

  // Nothing at all happens outside the bare window.
  void aModalOrFullscreenWindowOpensNoShow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.accentColor = "violet";
    LogoStage* stage = stageOf(win);
    win.fs.active = true;
    QVERIFY2(!stage->activateByName("neonOn"), "fullscreen opens nothing");
    win.fs.active = false;
    QVERIFY(stage->activateByName("neonOn"));
    QVERIFY2(!stage->activateByName("makeSomeSunshine"), "one stage at a time");
    stage->dismiss();
  }

  // A press away from the mark ends the show, as it does in the browser; and nothing of the
  // window's own chrome may sit on top of the stage while it is up. Through the window HANDLE,
  // where a real press lands before any widget sees it — sent to the stage it cannot catch the
  // lock swallowing it on the way.
  void aPressAwayFromTheMarkEndsItAndTheHeaderMarkIsCovered() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.accentColor = "violet";
    LogoStage* stage = stageOf(win);
    QVERIFY(stage->activateByName("neonOn"));
    QWidget* fx = win.findChild<QWidget*>("logoHoverFx");
    QVERIFY2(fx && !fx->isVisible(), "the header mark must not paint over the stage");
    // The dock edges re-raise themselves on every layout pass, so a RESIZE is what put them
    // back over the stage; they have to stay down through one.
    QWidget* grip = win.findChild<QWidget*>("panelGrip");
    QVERIFY(grip);
    QVERIFY2(!grip->isVisible(), "the panel grip must not paint over the stage");
    win.resize(1100, 820);
    QTest::qWait(80);
    QVERIFY2(!grip->isVisible(), "…and a resize must not raise it back over");

    // The mark is the one thing a press acts on, so it — and only it — wears the hand.
    const QPoint corner(12, win.height() - 12);   // far from the centred mark
    const auto hover = [&](QPoint p) {
      QMouseEvent mv(QEvent::MouseMove, QPointF(p), win.mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(stage, &mv);
      return stage->cursor().shape();
    };
    QCOMPARE(hover(win.rect().center()), Qt::PointingHandCursor);
    QCOMPARE(hover(corner), Qt::ArrowCursor);
    QTest::mouseClick(win.windowHandle(), Qt::LeftButton, Qt::NoModifier, corner);
    QTest::qWait(60);
    QVERIFY2(!stage->isOpen(), "a press away from the mark ends the show");
    QTest::qWait(60);
    QVERIFY2(fx && fx->isVisible(), "…and the header mark comes back");
    QVERIFY2(grip->isVisible(), "…and so does the panel grip");
  }

  // A press EASES the light and the cloud in, and eases them back out — never a step.
  void aPressRampsTheIntensityInsteadOfSteppingIt() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.accentColor = "violet";
    LogoStage* stage = stageOf(win);
    QVERIFY(stage->activateByName("neonOn"));
    const double full = support::logoStageConfig().holdBoost;
    QCOMPARE(stage->boostNow(), 1.0);

    const QPoint c = stage->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(c), win.mapToGlobal(c),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(stage, &press);
    QTest::qWait(60);
    const double early = stage->boostNow();
    QVERIFY2(early > 1.0 && early < full, "the press is part way in, not already there");
    QTest::qWait(700);
    QVERIFY2(stage->boostNow() > full * 0.9, "…and it arrives while held");

    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(c), win.mapToGlobal(c),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(stage, &release);
    QTest::qWait(60);
    const double falling = stage->boostNow();
    QVERIFY2(falling < full && falling > 1.0, "letting go eases back out, not off");
    QTest::qWait(900);
    QVERIFY2(stage->boostNow() < 1.05, "…all the way to rest");
    stage->dismiss();
  }

  // Typing a show's name into the bare window opens it, as it does in the browser.
  void typingAShowsNameOpensIt() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.accentColor = "violet";
    LogoStage* stage = stageOf(win);
    QVERIFY(stage);
    qWarning("focus at rest: %s", QApplication::focusWidget()
             ? QApplication::focusWidget()->metaObject()->className() : "none");
    // Through Qt's own delivery — shortcut map, focus widget and all — not straight to the window.
    const auto type = [&win](const QString& word) {
      QWidget* to = QApplication::focusWidget() ? QApplication::focusWidget() : &win;
      QTest::keyClicks(to, word);
    };
    type(QStringLiteral("neonon"));
    QVERIFY2(stage->isOpen(), "the typed word opens its show");
    stage->dismiss();
    QTest::qWait(500);   // the hide plays out, and the stage lets go of the keyboard

    // …and the NEXT word is heard just as well: one show must not deafen the window.
    type(QStringLiteral("makesomesunshine"));
    QVERIFY2(stage->isOpen(), "a second typed word opens its show too");
    QCOMPARE(stage->showName(), QString("makeSomeSunshine"));
    stage->dismiss();
  }

  // The pink show is an edit: the tint, and the heart as one step on the user's own stack.
  void thePinkShowTintsThePageAndDrawsItsHeart() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    const int before = int(canvas->getLines().size());
    LogoStage* stage = stageOf(win);
    QVERIFY(stage->activateByName("pinkVibe"));
    QVERIFY2(!stage->isOpen(), "the pink show opens no stage");

    const support::LogoStageConfig& cfg = support::logoStageConfig();
    QCOMPARE(win.settings.imageFilter, QString("custom"));
    QCOMPARE(canvas->getFilterColor().name(), cfg.pinkTint.name());
    QCOMPARE(int(canvas->getLines().size()), before + 1);
    const stencil::core::Line& heart = canvas->getLines().back();
    QVERIFY2(heart.locked, "the heart is a closed shape");
    QCOMPARE(QString::fromStdString(heart.fillColor), cfg.heartFill);
    QCOMPARE(int(heart.points.size()), cfg.heartPoints);
    QVERIFY2(canvas->canUndo(), "…and it is one undoable step");
    // The heart IS the show: a page taller than the viewport would hide it, so the view fits.
    const QSize view = win.scroll->viewport()->size();
    const double scale = canvas->getScale();
    QVERIFY2(canvas->getImage().width() * scale <= view.width() + 1
                 && canvas->getImage().height() * scale <= view.height() + 1,
             "the whole page is in view when the show ends");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.logoStage.gui.moc"
