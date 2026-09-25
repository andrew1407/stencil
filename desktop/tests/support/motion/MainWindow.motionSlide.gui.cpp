// MainWindow GUI e2e — the slide flight: a window's photograph magnifying out of its anchor and
// back, quick both ways, and never clipped by the editor. Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include <QGraphicsOpacityEffect>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Slide flies the window's photograph: it MAGNIFIES out of its anchor on an ease-out and is
  // up by the browser's time over 1.5; the close is as quick and lands easing out too.
  void slideMagnifiesTheWindowQuicklyBothWays() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::support::setMotionMode(stencil::support::MotionMode::SLIDE);
    QDialog dlg(&win);
    dlg.resize(300, 200);
    const QRect anchor(win.mapToGlobal(QPoint(40, 40)), QSize(26, 26));
    stencil::support::revealDialog(dlg, nullptr, anchor);
    dlg.show();
    QVector<QSize> sizes;
    QVector<double> alphas;
    const auto alphaOf = [](QLabel* g) {
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(g->graphicsEffect());
      return fx ? fx->opacity() : g->windowOpacity();
    };
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 600) {
      if (QLabel* g = modalGhost(&win)) { sizes.push_back(g->size()); alphas.push_back(alphaOf(g)); }
      else if (!sizes.isEmpty()) break;
      QTest::qWait(10);
    }
    const qint64 opened = t.elapsed();
    QVERIFY2(sizes.size() > 3, "the open flight played");
    QVERIFY2(sizes.first().width() < 150, "it starts small, at its anchor");
    for (int i = 1; i < sizes.size(); ++i) QVERIFY2(sizes[i].width() >= sizes[i - 1].width(), "it only grows");
    QVERIFY2(alphas.first() < 0.6, "it fades in with the growth, not already solid");
    for (int i = 1; i < alphas.size(); ++i) QVERIFY2(alphas[i] >= alphas[i - 1] - 1e-9, "…brightening all the way");
    QVERIFY2(alphas[alphas.size() / 2] < 0.999, "still fading in mid-flight");
    QVERIFY2(opened < 420, qPrintable(QStringLiteral("up in %1ms").arg(opened)));
    t.restart();
    dlg.hide();
    QTest::qWait(10);
    QVERIFY2(modalGhost(&win), "the close flight played");
    while (modalGhost(&win) && t.elapsed() < 900) QTest::qWait(10);
    QVERIFY2(t.elapsed() < 480, qPrintable(QStringLiteral("gone in %1ms").arg(t.elapsed())));
    stencil::support::setMotionMode(stencil::support::MotionMode::PARTICLES);
  }

  // A window standing past the editor's top edge flies whole: its ghost is a window of its own,
  // never a child the editor clips. Offscreen has no real desktop to escape into.
  void aSlideGhostPastTheWindowEdgeIsNotClipped() {
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) QSKIP("needs a real screen");
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.setGeometry(200, 300, 900, 600);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::support::setMotionMode(stencil::support::MotionMode::SLIDE);
    QDialog dlg(&win);
    dlg.setGeometry(win.x() + 100, win.y() - 150, 400, 500);
    stencil::support::revealDialog(dlg, nullptr, QRect(win.mapToGlobal(QPoint(40, 40)), QSize(26, 26)));
    dlg.show();
    QLabel* ghost = nullptr;
    for (int i = 0; i < 40 && !ghost; ++i) {
      QTest::qWait(10);
      ghost = modalGhost(&win);
    }
    QVERIFY2(ghost, "the open flight played");
    QVERIFY2(ghost->isWindow(), "a ghost reaching past the editor is a window, not a clipped child");
    dlg.hide();
    QTest::qWait(500);
    stencil::support::setMotionMode(stencil::support::MotionMode::PARTICLES);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motionSlide.gui.moc"
