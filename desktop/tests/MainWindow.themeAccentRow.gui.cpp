// MainWindow GUI e2e — The popover's tick following an outside change, the Alt glide, and the sliding row.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The ✓ in an OPEN accent popover follows the accent wherever it moved from — the logo's
  // click-cycle (applySettings), not only the popover's own row picks (user report).
  void accentPopoverTickFollowsAnOutsideAccentChange() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);
    QToolButton* logo = win.logoBtn;
    QVERIFY(logo);
    const QString original = win.settings.accentColor;
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(presets.size() >= 2);
    QString other;
    for (const auto& a : presets) if (a.key != original) { other = a.key; break; }
    bool opened = false, markedBefore = false, markedAfter = false, oneMark = true;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop.active.data();
      opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        auto* was = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + original);
        markedBefore = was && was->property("currentAccent").toBool();
        // The accent moves OUTSIDE the popover — the logo click-cycle's own path.
        auto next = win.settings;
        next.accentColor = other;
        win.applySettings(next, true);
        settleLayout(&win, 30);
        int marks = 0;
        for (const auto& a : presets) {
          auto* r = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + a.key);
          if (r && r->property("currentAccent").toBool()) ++marks;
        }
        auto* now = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + other);
        markedAfter = now && now->property("currentAccent").toBool();
        oneMark = marks == 1;
        pop->reject();
      }
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(markedBefore, "the current accent's row starts marked");
    QVERIFY2(markedAfter, "an accent applied from outside the popover must move its tick");
    QVERIFY2(oneMark, "exactly one row carries the tick");
    auto restore = win.settings;
    restore.accentColor = original;
    win.applySettings(restore, true);
  }

  // Alt held with the cursor resting ON the open popover must not glide onto an icon the box is
  // COVERING: the glide's cursor-rect fallback is pure geometry and read that as hovering them.
  void altGlideIgnoresIconsUnderTheOpenPopover() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);
    QToolButton* logo = win.logoBtn;
    QVERIFY(logo);
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint cursorWas = QCursor::pos();
    bool opened = false, covered = false, stayed = false, armed = true;
    QTimer::singleShot(700, &win, [&] {   // past the popover's open flight
      QWidget* box = win.pop.overlay.data();
      opened = box && box->isVisible() && win.pop.active &&
               win.pop.active->objectName() == QLatin1String("accentPopover");
      if (opened) {
        const QRect boxGlobal(box->mapToGlobal(QPoint(0, 0)), box->size());
        QPoint on;   // a point on the box AND on a popover icon it covers
        for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it) {
          auto* b = static_cast<QToolButton*>(it.key());
          if (b == logo || !b->isVisible() || !it.value()->isEnabled()) continue;
          const QRect hit = QRect(b->mapToGlobal(QPoint(0, 0)), b->size()).intersected(boxGlobal);
          if (hit.isEmpty()) continue;
          covered = true;
          on = hit.center();
          break;
        }
        if (covered) {
          win.altHeldForTest = true;   // the glide poll's stand-in for a held Alt
          QCursor::setPos(on);
          QTest::qWait(300);            // several glide ticks (80ms)
          stayed = win.pop.active &&
                   win.pop.active->objectName() == QLatin1String("accentPopover");
          armed = !win.pop.peekNextAction.isNull();
          win.altHeldForTest = false;
          // Never leave a peek queued: it would open (and block) after this unwinds.
          win.pop.peekNextAction.clear();
          win.pop.peekNextButton.clear();
        }
      }
      if (win.pop.active) win.pop.active->reject();
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's loop until the timer acts
    QCursor::setPos(cursorWas);
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(covered, "the accent box covers no popover icon — nothing to guard here");
    QVERIFY2(stayed, "resting on the box glided onto an icon underneath it");
    QVERIFY2(!armed, "resting on the box armed a covered icon's peek");
  }

  // A row under the pointer eases a couple of pixels RIGHT (browser `.accent-dd-opt:hover`) and
  // SURVIVES the preview's flood, which re-polishes every stylesheet and re-lays the popover out.
  void accentRowSlidesUnderThePointer() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);
    QToolButton* logo = win.logoBtn;
    QVERIFY(logo);
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(presets.size() >= 2);
    const QString original = win.settings.accentColor;
    QString other;
    for (const auto& a : presets) if (a.key != original) { other = a.key; break; }
    bool opened = false, foundRow = false;
    int restX = 0, hoverX = 0, floodedX = 0, backX = 0;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop.active.data();
      opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + presets.front().key);
        foundRow = row != nullptr;
        if (row) {
          restX = row->x();
          const QPointF p(4, 4);
          QEnterEvent enter(p, p, row->mapToGlobal(QPoint(4, 4)));
          QApplication::sendEvent(row, &enter);
          QTest::qWait(200);            // the 120ms slide, with room to spare
          hoverX = row->x();
          // The accent flood the preview plays, straight through applySettings.
          auto next = win.settings;
          next.accentColor = other;
          win.applySettings(next, true);
          QTest::qWait(150);
          floodedX = row->x();
          QEvent leave(QEvent::Leave);
          QApplication::sendEvent(row, &leave);
          QTest::qWait(200);
          backX = row->x();
        }
        pop->reject();
      }
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(foundRow, "the popover must carry the preset rows");
    QCOMPARE(hoverX, restX + 2);
    QCOMPARE(floodedX, restX + 2);
    QCOMPARE(backX, restX);
    auto restore = win.settings;
    restore.accentColor = original;
    win.applySettings(restore, true);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeAccentRow.gui.moc"
