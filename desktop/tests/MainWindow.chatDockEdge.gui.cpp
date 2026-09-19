// MainWindow GUI e2e — The resize edge per dock area, the session-transient restore, and the slide.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The chat dock's resize edge (browser .chat-resizer): the strip is QMainWindow chrome with no
  // widget of its own, so a mouse-through band is painted where Qt would start the resize.
  void chatResizeEdgeFollowsTheDockInEveryArea() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.chatDock_);
    win.chatDock_->show();
    settleLayout(&win, 120);
    auto* edge = win.chatEdge_;
    QVERIFY(edge);
    const struct { Qt::DockWidgetArea area; Qt::Orientation split; const char* name; } AREAS[] = {
        {Qt::LeftDockWidgetArea, Qt::Horizontal, "left"},
        {Qt::RightDockWidgetArea, Qt::Horizontal, "right"},
        {Qt::TopDockWidgetArea, Qt::Vertical, "top"},
        {Qt::BottomDockWidgetArea, Qt::Vertical, "bottom"},
    };
    for (const auto& a : AREAS) {
      win.addDockWidget(a.area, win.chatDock_, a.split);
      settleLayout(&win, 120);
      const QRect dock = win.chatDock_->geometry();
      const QRect hit = win.chatEdgeHit_;
      const QRect band = edge->geometry();
      QVERIFY2(edge->isVisible(), a.name);
      QVERIFY2(!hit.isEmpty(), a.name);
      // The strip sits OUTSIDE the panel, against the edge it is docked by.
      QVERIFY2(!hit.intersects(dock), a.name);
      if (a.area == Qt::LeftDockWidgetArea) QCOMPARE(hit.left(), dock.right() + 1);
      if (a.area == Qt::RightDockWidgetArea) QCOMPARE(hit.right(), dock.left() - 1);
      if (a.area == Qt::TopDockWidgetArea) QCOMPARE(hit.top(), dock.bottom() + 1);
      if (a.area == Qt::BottomDockWidgetArea) QCOMPARE(hit.bottom(), dock.top() - 1);
      // …and the band is drawn AROUND that strip, never thinner than an affordance can
      // be seen at (the horizontal separators are a hairline by design, theme.cpp).
      QVERIFY2(band.contains(hit), a.name);
      QVERIFY2(qMin(band.width(), band.height()) >= stencil::gui::DockEdgeOverlay::MIN_THICKNESS, a.name);
    }
    // Nothing to grab while it floats — the window frame owns that resize.
    win.chatDock_->setFloating(true);
    QTRY_VERIFY(!edge->isVisible());
  }

  // The chat dock is session-transient (browser full-reset-on-reload parity): a saved layout must
  // NOT resurrect it. The selection panel DOES restore, and its toggle tracks that visibility.
  void chatDockSessionTransientAndPanelToggleAfterRestore() {
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    // Session 1: open + float the chat dock, then persist the layout the way
    // closeEvent does.
    {
      MainWindow win(nullptr, false);
      win.resize(1200, 800);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      auto* dock = win.findChild<QDockWidget*>("llmChatDock");
      auto* chat = win.findChild<QAction*>("actChat");
      QVERIFY(dock && chat);
      chat->setChecked(true);
      QTRY_VERIFY(dock->isVisible());
      dock->setFloating(true);
      QTRY_VERIFY(dock->isFloating());
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState = QString::fromLatin1(win.saveState().toBase64());
      stencil::gui::fileStore::saveSettings(s);
    }
    // Session 2: chat dock reset to hidden/docked-left/unchecked; the selection
    // panel's toggle is synced to the RESTORED visibility and the chevron works.
    {
      MainWindow win(nullptr, false);
      win.resize(1200, 800);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      auto* dock = win.findChild<QDockWidget*>("llmChatDock");
      QVERIFY(dock);
      QVERIFY(dock->isHidden());
      QVERIFY(!dock->isFloating());
      QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
      auto* chat = win.findChild<QAction*>("actChat");
      QVERIFY(chat && !chat->isChecked());

      auto* selPanel = win.findChild<QDockWidget*>("selectionPanelDock");
      QVERIFY(selPanel && !selPanel->isHidden());
      QAction* panelAct = nullptr;
      for (QAction* a : win.findChildren<QAction*>())
        if (a->text() == "Selection Panel") { panelAct = a; break; }
      QVERIFY(panelAct);
      QVERIFY(panelAct->isChecked());  // synced to the restored visibility
      // Header chevron path: collapseRequested → actPanel_ → animated hide.
      QVERIFY(QMetaObject::invokeMethod(selPanel, "collapseRequested"));
      QTRY_VERIFY(selPanel->isHidden());
      QVERIFY(!panelAct->isChecked());
    }
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    beat();
  }

  // The chat dock SLIDES: docked left, its WIDTH animates 0 → natural and back. Afterwards the
  // min==max pinning is released so the dock stays resizable and a reopen keeps its extent.
  void chatDockSlideAnimation() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);  // width axis

    // Sample the docked extent every ~16 ms across the slide (a hidden dock
    // counts as 0 — that IS its contribution to the layout).
    auto sample = [&] {
      QList<int> s;
      QElapsedTimer t;
      t.start();
      do {
        s.append(dock->isVisible() ? dock->width() : 0);
        QTest::qWait(16);
      } while (win.chatAnim_ && t.elapsed() < 1200);
      return s;
    };
    auto hasIntermediate = [](const QList<int>& s, int full) {
      for (int v : s) if (v > 2 && v < full - 2) return true;
      return false;
    };

    // ── open: 0 → natural ──
    chat->setChecked(true);
    QVERIFY(dock->isVisible());  // shown immediately; it grows into place
    const QList<int> up = sample();
    QTRY_VERIFY(dock->width() > 200);
    const int full = dock->width();
    QVERIFY2(hasIntermediate(up, full),
             "chat dock popped open (no intermediate widths)");
    // The pinning is released at the end: still resizable, and the dock's own
    // 260 px floor is back (not clamped to the animation's last frame).
    QTRY_COMPARE(dock->maximumWidth(), QWIDGETSIZE_MAX);
    QCOMPARE(dock->minimumWidth(), 260);

    // ── close: natural → 0, then hidden ──
    chat->setChecked(false);
    QVERIFY(dock->isVisible());  // still on screen while it slides out
    const QList<int> down = sample();
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY2(hasIntermediate(down, full),
             "chat dock popped shut (no intermediate widths)");
    for (int i = 1; i < down.size(); ++i)
      QVERIFY2(down[i] <= down[i - 1] + 1, "chat dock hide width oscillated");
    QTRY_COMPARE(dock->maximumWidth(), QWIDGETSIZE_MAX);

    // Reopening restores the extent it was dismissed at.
    chat->setChecked(true);
    QTRY_VERIFY(dock->width() > 200);
    awaitAnim(win.chatAnim_);
    QVERIFY2(qAbs(dock->width() - full) <= 8, "reopen lost the remembered width");

    // Floating: no edge to slide from — plain show/hide, and the tear-off size
    // is never clamped by a leftover animation constraint.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    chat->setChecked(false);
    QVERIFY(!dock->isVisible());  // immediate, no slide
    chat->setChecked(true);
    QVERIFY(dock->isVisible());
    QTRY_COMPARE(dock->size(), QSize(385, 480));   // +5px of room for the row "…"
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatDockEdge.gui.moc"
