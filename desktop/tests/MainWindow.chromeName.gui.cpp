// MainWindow GUI e2e — The image-size padding, the panel chevrons, and the name affordances and chips.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

// The shared box of the panel's two collapse chevrons (mainWindow PANEL_TOGGLE_BOX /
// selectionPanel TOGGLE_BOX) — asserted equal so the pair can't drift apart.
constexpr int PANEL_CHEVRON_BOX = 24;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // imageSizeInfo_ needs real top/bottom breathing room via contentsMargins, not
  // stylesheet `padding` — QSS padding on this QLabel had no effect on paint or sizeHint().
  void imageSizeInfoHasRealVerticalPadding() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY(win.imageSizeInfo_);
    // 10px left/right (browser parity: css/layout.css .info padding: 10px), 11px top/bottom
    // so the readout reads as its own band between the toolbars and the canvas.
    QCOMPARE(win.imageSizeInfo_->contentsMargins(), QMargins(10, 11, 10, 11));
    // Not just set — actually taken into account: the reserved fixed height must exceed
    // the bare font height by at least the vertical margins.
    win.reserveImageInfoHeight();
    const int fontH = QFontMetrics(win.imageSizeInfo_->font()).height();
    QVERIFY2(win.imageSizeInfo_->height() >= fontH + 22,
             "the reserved height leaves no room for 11px top + 11px bottom");
  }

  // The panel toggles are mouse affordances: taking focus draws the platform's halo
  // around the chevron, which reads as a second, taller pill sitting over the canvas.
  void panelToggleChevronsTakeNoFocusHalo() {
    MainWindow win;
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actPanel_->setChecked(false);
    settle([&] { return win.panelReopenBtn_ != nullptr; }, 600);
    QVERIFY(win.panelReopenBtn_);
    QCOMPARE(win.panelReopenBtn_->focusPolicy(), Qt::NoFocus);
    QCOMPARE(win.panelReopenBtn_->size(), QSize(PANEL_CHEVRON_BOX, PANEL_CHEVRON_BOX));
    // …and its twin in the panel header, so the pair stays consistent.
    QWidget* bar = nullptr;
    for (QDockWidget* d : win.findChildren<QDockWidget*>())
      if (d->objectName() != QLatin1String("llmChatDock") &&
          d->objectName() != QLatin1String("selectedLineDock") &&
          d->objectName() != QLatin1String("imageInfoDock") && d->titleBarWidget())
        bar = d->titleBarWidget();
    QVERIFY2(bar, "no selection-panel title bar");
    // By NAME, not "every QToolButton in the header": the header also carries the
    // Points | Lines strip, and a QTabBar owns two internal scroll arrows that are
    // QToolButtons of its own sizing.
    const auto chevrons = bar->findChildren<QToolButton*>(QStringLiteral("panelCollapseBtn"));
    QVERIFY2(!chevrons.isEmpty(), "the panel header has no collapse chevron");
    for (QToolButton* b : chevrons) {
      QCOMPARE(b->focusPolicy(), Qt::NoFocus);
      QCOMPARE(b->size(), QSize(PANEL_CHEVRON_BOX, PANEL_CHEVRON_BOX));
    }
  }

  // The ✎/🎨 are hover-revealed over the name group, and a pointer that lands anywhere else
  // has left it — even when the group's own Leave never arrives (crossing straight onto
  // another row's icon left the pair lit three clusters away).
  void nameAffordancesGoWhenThePointerLeavesTheGroup() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "hover-out");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    settleLayout(&win, 300);
    const auto paintedOut = [](QWidget* w) { return w->property("stencilPaintedOut").toBool(); };

    QCursor::setPos(win.nameBar_.group->mapToGlobal(win.nameBar_.group->rect().center()));
    win.updateNameHover();
    QTRY_VERIFY(!paintedOut(win.nameBar_.edit));
    QVERIFY(!paintedOut(win.nameBar_.colorBtn));

    // Onto another control, and the pair goes — driven by the same recompute the app runs
    // when a pointer enters anything else (here: called directly, as the poll would).
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 60, 200)));
    win.updateNameHover();
    QTRY_VERIFY_WITH_TIMEOUT(paintedOut(win.nameBar_.edit), 2000);
    QVERIFY(paintedOut(win.nameBar_.colorBtn));
    // …and they keep their slots either way: painting out must never move the row.
    QVERIFY(win.nameBar_.edit->isVisible() && win.nameBar_.colorBtn->isVisible());
    beat();
  }

  // Edit mode SWAPS the name affordances in place: ✎/🎨 out, ✓/✗ in, and back again. The
  // pair returning while the marks were still flying out put all four in the row at once —
  // it widened, and the ✎/🎨 appeared BESIDE the leaving marks instead of in their place
  //. Never more than two hold a slot at any moment.
  void nameChipsSwapInPlaceWithoutWideningTheRow() {
    const auto motion = withMotion();   // the flights below ARE the thing under test
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "swap-row");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    settleLayout(&win, 300);
    win.nameBar_.hover = true;   // ✎/🎨 are hover-revealed; pin them on for the swap
    const auto held = [&win] {
      int n = 0;
      for (QToolButton* b : { win.nameBar_.edit, win.nameBar_.colorBtn,
                              win.nameBar_.accept, win.nameBar_.cancel })
        if (b && b->isVisible()) ++n;
      return n;
    };

    win.enterNameEdit();
    for (int i = 0; i < 10; ++i) {   // through the whole in-flight
      QTest::qWait(50);
      QVERIFY2(held() <= 2, qPrintable(QString("entering: %1 chips held a slot").arg(held())));
    }
    QVERIFY(win.nameBar_.accept->isVisible() && win.nameBar_.cancel->isVisible());

    win.cancelProjectName();
    for (int i = 0; i < 10; ++i) {   // …and the whole way back
      QTest::qWait(50);
      QVERIFY2(held() <= 2, qPrintable(QString("leaving: %1 chips held a slot").arg(held())));
    }
    QTRY_VERIFY(win.nameBar_.edit->isVisible() && win.nameBar_.colorBtn->isVisible());
    QVERIFY(!win.nameBar_.accept->isVisible() && !win.nameBar_.cancel->isVisible());
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeName.gui.moc"
