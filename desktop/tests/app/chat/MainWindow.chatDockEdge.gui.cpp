// MainWindow GUI e2e — The resize edge per dock area, the session-transient restore, and the slide.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The chat dock's resize handle (browser .chat-resizer): a strip INSIDE the dock's own edge,
  // 6px like the browser's, that resizes the dock itself when dragged; the page sits flush outside.
  void chatResizeEdgeFollowsTheDockInEveryArea() {
    MainWindow win(nullptr, false);
    win.resize(1200, 1000);   // room for a top/bottom dock to grow past the editor shell's minimum
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.chatDock);
    win.chatDock->show();
    settleLayout(&win, 120);
    auto* edge = win.chatEdge;
    QVERIFY(edge);
    const struct { Qt::DockWidgetArea area; Qt::Orientation split; const char* name; } AREAS[] = {
        {Qt::LeftDockWidgetArea, Qt::Horizontal, "left"},
        {Qt::RightDockWidgetArea, Qt::Horizontal, "right"},
        {Qt::TopDockWidgetArea, Qt::Vertical, "top"},
        {Qt::BottomDockWidgetArea, Qt::Vertical, "bottom"},
    };
    for (const auto& a : AREAS) {
      win.addDockWidget(a.area, win.chatDock, a.split);
      settleLayout(&win, 120);
      const QRect dock = win.chatDock->geometry();
      const QRect band = edge->geometry();
      QVERIFY2(edge->isVisible(), a.name);
      QVERIFY2(dock.contains(band), a.name);
      QCOMPARE(qMin(band.width(), band.height()), stencil::gui::DockEdgeOverlay::THICKNESS);
      if (a.area == Qt::LeftDockWidgetArea) QCOMPARE(band.right(), dock.right());
      if (a.area == Qt::RightDockWidgetArea) QCOMPARE(band.left(), dock.left());
      if (a.area == Qt::TopDockWidgetArea) QCOMPARE(band.bottom(), dock.bottom());
      if (a.area == Qt::BottomDockWidgetArea) QCOMPARE(band.top(), dock.top());
      // Dragging the strip resizes the dock: 40px of travel toward the window's centre.
      const bool horiz = a.split == Qt::Horizontal;
      const int before = horiz ? win.chatDock->width() : win.chatDock->height();
      const int sign = (a.area == Qt::LeftDockWidgetArea || a.area == Qt::TopDockWidgetArea) ? 1 : -1;
      const QPoint at = band.center() - band.topLeft();
      const QPoint to = at + (horiz ? QPoint(sign * 40, 0) : QPoint(0, sign * 40));
      QTest::mousePress(edge, Qt::LeftButton, {}, at);
      QTest::mouseMove(edge, to);
      QMouseEvent mv(QEvent::MouseMove, to, edge->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(edge, &mv);
      QTest::mouseRelease(edge, Qt::LeftButton, {}, to);
      settleLayout(&win, 120);
      const int after = horiz ? win.chatDock->width() : win.chatDock->height();
      QVERIFY2(std::abs((after - before) - 40) <= 2,
               qPrintable(QString("%1: %2 -> %3 after a 40px drag").arg(a.name).arg(before).arg(after)));
      QCOMPARE(win.chatRestoreExtent, after);
    }
    // Nothing to grab while it floats — the window frame owns that resize.
    win.chatDock->setFloating(true);
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
      s.windowState = QString::fromLatin1(win.editor->saveState().toBase64());
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
      // Header chevron path: collapseRequested → actPanel → animated hide.
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
    const auto motion = withMotion();   // `none` (the suite's default) is a plain show/hide
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
      } while (win.chatAnim && t.elapsed() < 1200);
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
    awaitAnim(win.chatAnim);
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
