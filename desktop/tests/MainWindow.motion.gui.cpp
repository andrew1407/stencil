// MainWindow GUI e2e — The reveal snapshot, an outside press, the motion combo's glyphs and a dialog's origin.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The open flight photographs the dialog before its scroll area has decided its
  // scrollbar, so the picture that flew was a scrollbar too wide.
  // settleLayout brings the scrollbar in before the shot.
  void revealSnapshotWaitsForTheScrollbar() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::SettingsDialog dlg(win.settings_, &win);
    dlg.resize(dlg.width(), 320);   // short enough that the body must scroll
    dlg.show();
    auto* scroll = dlg.findChild<QScrollArea*>();
    QVERIFY(scroll);
    // Straight after show() the vertical bar may not be decided yet — the frame the old
    // snapshot was taken on. After the settle the viewport is the real row width.
    stencil::support::settleLayout(dlg);
    QVERIFY2(scroll->verticalScrollBar()->isVisible(), "the settled shell shows its scrollbar");
    const int settledViewport = scroll->viewport()->width();
    QTest::qWait(80);   // …and nothing moves once the event loop has had its say
    QCOMPARE(scroll->viewport()->width(), settledViewport);
    QVERIFY(scroll->viewport()->width() < scroll->width());
    dlg.reject();
  }

  // A press outside a modal dismisses it, like a press on the browser's modal overlay
  // (ui/base.js). This covers the decision only: QTest hands the widget a QMouseEvent,
  // while a real press on a window a modal blocks never becomes one — that half is
  // modalDismissMac.mm reading the NSEvent, which no offscreen test can reach.
  void clickOutsideAModalDismissesIt() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::support::installModalDismiss();   // the app installs it at startup; explicit here

    QDialog dlg(&win);
    dlg.setObjectName(QStringLiteral("probeModal"));
    dlg.resize(200, 150);
    dlg.setModal(true);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));

    // A press INSIDE the box changes nothing…
    QTest::mouseClick(&dlg, Qt::LeftButton, {}, QPoint(60, 60));
    QTest::qWait(40);
    QVERIFY2(dlg.isVisible(), "a press inside the dialog closed it");

    // …and one on a widget the dialog RAISED (a popup keeps it as its parent) neither.
    QWidget popup(&dlg, Qt::Popup);
    popup.resize(40, 40);
    QTest::mouseClick(&popup, Qt::LeftButton, {}, QPoint(5, 5));
    QTest::qWait(40);
    QVERIFY2(dlg.isVisible(), "a press in the dialog's own popup closed it");

    // A press on the blocked main window dismisses it — posted the way the PLATFORM does,
    // not with QTest::mouseClick: that hands the widget a QMouseEvent directly and skips
    // the window-system layer, which is exactly where Qt drops a blocked window's clicks.
    QTest::mouseClick(&win, Qt::LeftButton, {}, QPoint(60, 400));
    QTRY_VERIFY_WITH_TIMEOUT(!dlg.isVisible(), 1500);
    QCOMPARE(dlg.result(), int(QDialog::Rejected));

    // …unless the dialog says it must be answered.
    QDialog must(&win);
    must.setProperty(stencil::support::NO_OUTSIDE_DISMISS_PROPERTY, true);
    must.resize(200, 150);
    must.setModal(true);
    must.show();
    QVERIFY(QTest::qWaitForWindowExposed(&must));
    QTest::mouseClick(&win, Qt::LeftButton, {}, QPoint(60, 400));
    QTest::qWait(80);
    QVERIFY2(must.isVisible(), "an opted-out dialog was clicked away");
    must.close();
  }

  void motionComboRowsWearOneGlyph() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::SettingsDialog dlg(win.settings_, &win);
    dlg.show();
    settleLayout(&dlg, 30);
    auto* combo = static_cast<stencil::gui::SearchComboBox*>(   // no Q_OBJECT on the combo: found as its base
        dlg.findChild<QComboBox*>(QStringLiteral("motionModeCombo")));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 5);
    QVERIFY2(combo->toolTip().isEmpty(), "the browser's dropdown carries no tooltip");
    for (int i = 0; i < combo->count(); ++i) QVERIFY(!combo->itemIcon(i).isNull());
    combo->showPopup();
    QTRY_VERIFY(QApplication::activePopupWidget());
    QListView* list = combo->popupList();
    QVERIFY(list && list->isVisible());
    // Measured in LOGICAL pixels: the grab is at the screen's ratio (2x offscreen here).
    const QPixmap shot = list->grab();
    const int dpr = qMax(1, qRound(shot.devicePixelRatio()));
    const QImage img = shot.toImage().convertToFormat(QImage::Format_ARGB32)
                           .scaled(shot.width() / dpr, shot.height() / dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // Per row: the ICON slot's lit columns (the label starts past 36px), as runs. One
    // glyph is one run no wider than a 16px icon; a second icon would be a second run.
    const int rowH = img.height() / 5;
    QVERIFY(rowH > 10);
    for (int r = 0; r < 5; ++r) {
      QString cols;
      // Ink is whatever differs from the ROW's own background (a selected row wears the
      // accent wash), sampled just inside the popup's border.
      const QColor bg = img.pixelColor(5, r * rowH + rowH / 2);
      // Alpha counts: the list's viewport grabs transparent, so black ink on a clear row
      // differs from it in alpha alone.
      const auto far = [&](const QColor& c) {
        return qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) + qAbs(c.blue() - bg.blue())
               + qAbs(c.alpha() - bg.alpha()) > 90;
      };
      for (int x = 4; x < qMin(36, img.width()); ++x) {   // past the popup's own border
        bool lit = false;
        for (int y = r * rowH + 4; y < (r + 1) * rowH - 4 && !lit; ++y) lit = far(img.pixelColor(x, y));
        cols += lit ? QLatin1Char('#') : QLatin1Char('.');
      }
      qDebug("row %d icon columns: %s", r, qPrintable(cols));
      const QStringList runs = cols.split(QLatin1Char('.'), Qt::SkipEmptyParts);
      QVERIFY2(runs.size() >= 1, qPrintable(QStringLiteral("row %1 shows no glyph (%2)").arg(r).arg(cols)));
      int widest = 0, gaps = 0;
      for (const QString& run : runs) widest = qMax(widest, int(run.size()));
      // Runs parted by a gap of three or more columns are separate glyphs.
      for (int i = 3; i < cols.size(); ++i)
        if (cols.mid(i - 3, 3) == QLatin1String("...") && cols[i] == QLatin1Char('#') && cols.left(i - 3).contains(QLatin1Char('#'))) ++gaps;
      QVERIFY2(gaps == 0, qPrintable(QStringLiteral("row %1: two glyphs (%2)").arg(r).arg(cols)));
      QVERIFY2(widest <= 20, qPrintable(QStringLiteral("row %1: glyph ink %2px wide — more than one icon (%3)").arg(r).arg(widest).arg(cols)));
    }
    // …and a pick from the popup reports itself as a user pick — activated(), the signal
    // the dialog applies live from — BEFORE the list leaves. setCurrentIndex alone never
    // emits it, which is why a popup pick used to apply only on OK.
    QSignalSpy picked(combo, &QComboBox::activated);
    list->setCurrentIndex(list->model()->index(2, 0));   // Fire
    emit list->clicked(list->currentIndex());
    QCOMPARE(picked.count(), 1);
    QCOMPARE(picked.at(0).at(0).toInt(), 2);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("fire"));
    QVERIFY(!list->isVisible());
    combo->hidePopup();
  }

  // The dialog reveal must START at the icon that opened it: support::revealDialog dusts
  // a snapshot of the dialog across the window, every mote streaming out of that icon, so
  // the flight's target point IS the origin the user sees the window come out of.
  void dialogRevealStartsAtTheIconThatOpenedIt() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    // ctest runs this suite with STENCIL_NO_ANIM=1 (dialogs are driven immediately), and
    // revealDialog is a no-op under it — this test is about the flight, so turn it back on.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    // The cloud is already in flight by the time we can look, so read the point it aims
    // at — that is where the user sees the window come out of.
    const auto flightOrigin = [&](QWidget* anchor) {
      QDialog dlg(&win);
      dlg.resize(300, 200);
      stencil::support::revealDialog(dlg, anchor, QRect());
      dlg.show();
      QTest::qWait(50);          // past the 0-timer that builds the cloud, inside the flight
      const QPoint from = surfaceFlightTarget(&win);
      dlg.close();
      return from;
    };
    QToolButton* icon = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->isVisible() && b->property("toolSection").isValid()) { icon = b; break; }
    QVERIFY2(icon, "no visible toolbar icon to fly out of");
    const QPoint origin = flightOrigin(icon);
    QVERIFY2(origin != QPoint(-1, -1), "no reveal flight was created");
    const QPoint want = flightPointOf(icon, &win);
    QVERIFY2(origin == want, qPrintable(QString("the flight starts at %1, the icon is at %2")
                                            .arg(QDebug::toString(origin), QDebug::toString(want))));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motion.gui.moc"
