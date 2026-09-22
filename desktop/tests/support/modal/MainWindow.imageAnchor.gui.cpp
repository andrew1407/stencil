// MainWindow GUI e2e — where an open-image question flies from and lands (support/modal/
// imageAnchor.hpp): a confirm about opening an image grows out of the CANVAS CENTRE whatever
// gesture raised it and pours into the toolbar's Open control once the answer opened one, while
// every other confirm is left to the press it came from. Browser twin: tests/ui/modal/imageAnchor.
#include "../../MainWindow.gui.hpp"
#include "imageAnchor.hpp"

using stencil::gui::canvasAnchorRect;
using stencil::gui::openImageAnchorRect;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void openImageConfirmFliesFromTheCanvasAndLandsByItsOutcome() {
    MainWindow win;
    QVERIFY(openLoaded(win));
    settleLayout(&win, 200);
    auto motion = withMotion();

    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    QPoint openOrigin(-1, -1);

    // Raise the blank-replace confirm, read where its OPEN flight starts, answer it, then read
    // where the CLOSE flight lands. exec() blocks, so a 0-timer drives the modal.
    const auto askAndAnswer = [&](bool accept) {
      watcher.reset();
      openOrigin = QPoint(-1, -1);
      QTimer::singleShot(0, &win, [&] {
        for (int i = 0; i < 400; ++i) {
          if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            settle([&] { return watcher.captured; }, 300);
            openOrigin = watcher.origin;
            watcher.reset();
            if (accept) dlg->accept(); else dlg->reject();
            return;
          }
          QTest::qWait(5);
        }
      });
      win.createBlankImageFromDialog(Qt::white, 200, 200);
      settle([&] { return watcher.captured; }, 600);
      QVERIFY(awaitFlights(&win));
    };

    const QPoint canvasHome = win.mapFromGlobal(canvasAnchorRect(&win).center());
    const QPoint gesture = win.mapFromGlobal(stencil::support::gestureAnchorRect().center());
    QVERIFY2(canvasHome != gesture, "the cursor sits on the canvas centre — the case proves nothing");
    QWidget* viewport = win.findChild<QWidget*>(QStringLiteral("canvasViewport"));
    QVERIFY(viewport && viewport->isVisible());
    QVERIFY2((canvasHome - flightPointOf(viewport, &win)).manhattanLength() <= 2,
             "the canvas anchor is not on the viewport's centre");

    // ── cancelled: out of the canvas, and back into it ──
    askAndAnswer(/*accept=*/false);
    QVERIFY2(openOrigin == canvasHome,
             qPrintable(QString("the confirm grew out of %1, the canvas centre is %2")
                            .arg(QDebug::toString(openOrigin), QDebug::toString(canvasHome))));
    QCOMPARE(watcher.origin, canvasHome);

    // ── accepted: a blank was created, so it pours into the toolbar's Open control ──
    const QPoint openCtrl = win.mapFromGlobal(openImageAnchorRect(&win).center());
    QVERIFY2(openCtrl != canvasHome, "the Open control is not distinguishable from the canvas");
    askAndAnswer(/*accept=*/true);
    QCOMPARE(openOrigin, canvasHome);
    QVERIFY2(watcher.origin == openCtrl,
             qPrintable(QString("the answer landed on %1, the Open control is at %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(openCtrl))));
  }

  // From the empty editor the icon row is not up yet — the image lands after the dialog is gone —
  // so the anchor is the place the icon TAKES, never the big button it replaces.
  void theAnchorIsWhereTheIconLandsNotWhereTheBigButtonStands() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 400);
    QWidget* icon = win.findChild<QWidget*>(QStringLiteral("openAnotherImageBtn"));
    QWidget* big = win.findChild<QWidget*>(QStringLiteral("openImageBtn"));
    QVERIFY(icon && big);
    QVERIFY2(!icon->isVisible() && big->isVisible(), "the empty editor shows the big Open button");
    const QRect aimed = openImageAnchorRect(&win);
    QVERIFY2(!aimed.contains(QRect(big->mapToGlobal(QPoint(0, 0)), big->size()).center()),
             "the anchor is still the big button the icon replaces");

    win.createBlankImageFromDialog(Qt::white, 200, 200);
    settleLayout(&win, 1200);
    QVERIFY(icon->isVisible());
    QCOMPARE(aimed, QRect(icon->mapToGlobal(QPoint(0, 0)), icon->size()));
  }

  // Clearing shrinks the Image cluster, which slides the rest of the row along: the trash the
  // answer pours into is read where it LANDS, not where it was pressed.
  void theTrashAnchorIsWhereClearingLeavesIt() {
    MainWindow win;
    QVERIFY(openLoaded(win));
    settleLayout(&win, 600);
    QWidget* trash = win.buttonForAction(win.actClearProject);
    QVERIFY(trash && trash->isVisible());
    const QRect pressed(trash->mapToGlobal(QPoint(0, 0)), trash->size());
    const QRect aimed = stencil::gui::emptiedControlRect(&win, trash);
    win.resetToBlankEditor();
    settleLayout(&win, 1200);
    QVERIFY(trash->isVisible());
    const QRect landed(trash->mapToGlobal(QPoint(0, 0)), trash->size());
    QVERIFY2(pressed != landed, "the trash stayed put — the case proves nothing");
    QCOMPARE(aimed, landed);
  }

  // …and the rule reaches no further: a confirm outside the flow names no anchor at all, so it is
  // left to the app-wide watcher, which forms it out of the gesture (modalReveal gestureAnchorRect).
  void anUnrelatedConfirmStillFliesFromItsGesture() {
    MainWindow win;
    QVERIFY(openLoaded(win));
    settleLayout(&win, 200);
    auto motion = withMotion();

    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });

    bool claimed = true;
    QTimer::singleShot(0, &win, [&] {
      for (int i = 0; i < 400; ++i) {
        if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
          QTest::qWait(80);
          claimed = dlg->property("stencilDialogRevealed").toBool();
          dlg->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    stencil::gui::ConfirmSpec spec;
    spec.title = QStringLiteral("Clear editor");
    spec.message = QStringLiteral("Clear this editor (image + lines)?");
    spec.danger = true;
    stencil::gui::confirmModal(&win, spec);
    QVERIFY(awaitFlights(&win));

    QVERIFY2(!claimed, "an ordinary confirm claimed a flight of its own");
    QVERIFY2(!watcher.captured, "an ordinary confirm was flown from an anchor the flow chose");
    // The fallback is the press: the box the watcher would use tracks the cursor.
    QCOMPARE(stencil::support::gestureAnchorRect().center() + QPoint(1, 1), QCursor::pos());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.imageAnchor.gui.moc"
