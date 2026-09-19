// MainWindow GUI e2e — The blocked cursor on a disabled tool, and the compare tooltip's edited half.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowTip.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A dead icon says so under the pointer (the browser's `cursor: not-allowed`). Qt applies no cursor to
  // a DISABLED widget, which gets no mouse events, so the row it sits in carries one for it.
  void disabledToolIconShowsTheBlockedCursor() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);   // let the toolbar's own deferred layout pass settle first
    // A fresh window has no .stencil link, so live-sync is dead while Projects beside it
    // is live: one row, both states.
    QVERIFY(!win.actStencilLiveSync_->isEnabled());
    QToolButton* dead = nullptr;
    QToolButton* live = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>()) {
      if (!b->isVisible() || !b->parentWidget()) continue;
      if (!b->parentWidget()->property("toolRow").toBool()) continue;
      if (b->defaultAction() == win.actStencilLiveSync_) dead = b;
      if (b->defaultAction() == win.actProjects_ && !live) live = b;
    }
    QVERIFY2(dead, "no toolbar button for live sync");
    QWidget* row = dead->parentWidget();
    QVERIFY(row->hasMouseTracking());

    // The move Qt actually delivers reaches the application filters with the BUTTON as its object and is
    // then thrown away; what hangs off it is an OVERRIDE cursor, the only kind Qt applies at once.
    auto blocked = [] {
      const QCursor* c = QApplication::overrideCursor();
      return c && c->shape() == Qt::ForbiddenCursor;
    };
    auto moveOver = [&](QWidget* target) {
      const QPoint local(target->width() / 2, target->height() / 2);
      QMouseEvent me(QEvent::MouseMove, QPointF(local), target->mapToGlobal(local),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      qApp->sendEvent(target, &me);
    };
    QVERIFY(!blocked());
    moveOver(dead);
    QVERIFY2(blocked(), "a dead icon left the pointer unchanged");
    // Over a LIVE icon in the same row, and it goes back at once.
    QVERIFY2(live && live->parentWidget() == row, "no live icon beside it to move onto");
    moveOver(live);
    QVERIFY2(!blocked(), "the blocked cursor stuck over the live icon next door");
    moveOver(dead);
    QVERIFY(blocked());
    // The same again through the PLATFORM path (window → childAt → notify), which is what a
    // real pointer does — the synthetic sends above only prove the filter's arithmetic.
    moveOver(live);
    QTest::mouseMove(&win, win.mapFromGlobal(dead->mapToGlobal(dead->rect().center())));
    QTRY_VERIFY2(blocked(), "a real move over the dead icon left the plain cursor");
    // …and leaving the window clears it even with no move to land anywhere else.
    QEvent leave(QEvent::Leave);
    qApp->sendEvent(row, &leave);
    QVERIFY2(!blocked(), "leaving must put the cursor back");
    // A LIVE icon still carries its own hand cursor — nothing here touches that.
    if (live) QCOMPARE(live->cursor().shape(), Qt::PointingHandCursor);
  }

  // COMPARE + hover tooltip: hovering a point still labels its coordinates while comparing, but ONLY
  // over the half showing the EDITED image — elsewhere there is nothing on screen to point at.
  void compareTooltipOnlyOverTheEditedHalf() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    settleLayout(&win, 150);
    win.settings_.tooltipEnabled = true;    // independent of the machine's saved settings
    win.settings_.tooltipShowScreen = true;

    // One horizontal line across the 240x160 picture: a point in each half of a
    // centred vertical split, the segment between them crossing the divider.
    stencil::core::Line line;
    line.points = {{60, 100}, {180, 100}};
    canvas->setLines({line});
    const double s = canvas->scale();
    QLabel* body = win.tooltip_->findChild<QLabel*>();
    QVERIFY(body);

    // Hover an image-space spot and report what the tooltip says (empty = hidden). The reveal waits out
    // the toolbar tooltip's own 200 ms delay (scheduleHoverShow), so this outwaits it.
    const auto hoverText = [&](double ix, double iy) {
      const QPoint p(qRound(ix * s), qRound(iy * s));
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
      settle([&win] { return win.tooltip_->isVisible(); }, 240);
      return win.tooltip_->isVisible() ? body->text() : QString();
    };
    const auto compareAt = [&](const char* mode, double split) {
      win.setCompareModeUi(QString::fromLatin1(mode));
      canvas->setCompareSplit(split);
      settleLayout(&win, 60);
    };

    // Baseline (compare off): BOTH points and the segment between them are labelled —
    // without this the "hidden" assertions below would pass on a tooltip that never shows.
    const QString leftOff = hoverText(60, 100), rightOff = hoverText(180, 100);
    QVERIFY2(leftOff.contains("Pixel") && leftOff.contains("60, 100"), qPrintable(leftOff));
    QVERIFY2(rightOff.contains("Pixel") && rightOff.contains("180, 100"), qPrintable(rightOff));
    const QString lineOff = hoverText(150, 100);   // mid-segment, off both points
    QVERIFY2(!lineOff.contains("Pixel") && lineOff.contains("60, 100 px") &&
                 lineOff.contains("180, 100 px"),
             qPrintable("line hover should list its endpoints: " + lineOff));

    // Vertical split at the middle (divider = image x 120): the right point and the
    // right stretch of the line keep their tooltip, the left ones lose it.
    compareAt("vertical", 0.5);
    QVERIFY(canvas->compareReadOnly());
    QVERIFY2(hoverText(180, 100).contains("180, 100"), "the visible point lost its tooltip");
    QVERIFY2(hoverText(60, 100).isEmpty(), "a point behind the original half was labelled");
    QVERIFY2(hoverText(150, 100).contains("px"), "the visible line lost its tooltip");
    QVERIFY2(hoverText(90, 100).isEmpty(), "a line behind the original half was labelled");

    // Slide the divider past the right point (x 216) — the same point is now hidden…
    canvas->setCompareSplit(0.9);
    QVERIFY2(hoverText(180, 100).isEmpty(), "the divider move did not hide the point");
    // …and back before the left one (x 24), which reveals it.
    canvas->setCompareSplit(0.1);
    QVERIFY2(hoverText(60, 100).contains("60, 100"), "the divider move did not reveal the point");

    // Horizontal split: the original is the TOP, so y decides. Divider y 80 leaves the
    // line (y 100) below it; y 144 puts it above.
    compareAt("horizontal", 0.5);
    QVERIFY2(hoverText(180, 100).contains("180, 100"), "the point below the divider lost its tooltip");
    canvas->setCompareSplit(0.9);
    QVERIFY2(hoverText(180, 100).isEmpty(), "a point above the divider was labelled");
    QVERIFY2(hoverText(150, 100).isEmpty(), "a line above the divider was labelled");

    // "original" shows no layout anywhere — nothing is ever labelled.
    compareAt("original", 0.5);
    QVERIFY2(hoverText(180, 100).isEmpty() && hoverText(150, 100).isEmpty(),
             "the original-only view still labelled the layout");
    // …and the Alt+Shift+O peek is the same view, so it gates the same way.
    win.setCompareModeUi(QStringLiteral("none"));
    canvas->setCompareHoldOriginal(true);
    QVERIFY2(hoverText(180, 100).isEmpty(), "the held peek still labelled the layout");
    canvas->setCompareHoldOriginal(false);

    // Back to normal: the tooltip returns everywhere.
    QVERIFY2(hoverText(60, 100).contains("60, 100"), "the tooltip did not come back");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.tooltipsCursor.gui.moc"
