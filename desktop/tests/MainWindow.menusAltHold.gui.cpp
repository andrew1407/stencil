// MainWindow GUI e2e — The Alt-hold peek: hovering an export row never takes the menu down.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Holding Alt over an export-variant row peeks its live preview instead of closing the menu, from
  // all three routes — including the nested chain, where a bare Alt lands on the chain's ROOT.
  void altHoldOverAnExportRowNeverClosesTheMenu() {
    // The offscreen QPA plugin does not honour Qt::ToolTip's coexistence with an open popup's grab, so
    // the preview tooltip closes the menu there regardless: nothing to check without a real platform.
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
      QSKIP("Alt-hover's preview tooltip needs a real platform's popup-grab handling");
    enum Route { TOOLBAR_POPUP, NESTED_ROOT, NESTED_OVER_TOOLBAR_BUTTON };
    for (const Route route : {TOOLBAR_POPUP, NESTED_ROOT, NESTED_OVER_TOOLBAR_BUTTON}) {
      const char* name = route == TOOLBAR_POPUP ? "toolbar options popup"
                         : route == NESTED_ROOT ? "nested chain, Alt on the root"
                                               : "nested chain, cursor on the toolbar button";
      MainWindow win(nullptr, false);
      win.resize(1000, 760);
      win.show();
      QVERIFY2(QTest::qWaitForWindowExposed(&win), name);
      // Away from every icon before Alt is touched: QCursor::pos() is one process-wide value, and a stray
      // pop.buttons match opens a modal that blocks forever in execMaybePopover's event loop.
      QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
      win.openPathFromOS(guiTestImage());
      QTRY_VERIFY2(win.findChild<CanvasWidget*>()->hasImage(), name);
      QWidget* copyBtn = win.buttonForAction(win.actCopyImage);
      QVERIFY2(copyBtn, name);

      if (route == TOOLBAR_POPUP) {
        // "Current"'s own row (actCopyImageCurrentRow) only shows once something is
        // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays.
        stencil::core::Line line;
        line.points.push_back({4.0, 20.0});
        line.points.push_back({36.0, 20.0});
        win.canvas->setLines({line});
        win.refreshActions();
        QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                              copyBtn->mapToGlobal(copyBtn->rect().center()));
        QApplication::sendEvent(copyBtn, &ctx);
        QMenu* menu = win.copyImageOptionsMenu;
        QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");
        // Hover the first row (QMenu::hovered is what wireExportPreviewHover listens on) so AltPreviewFilter
        // has an activeAction(); set it directly, as QMenu does internally, since mouseMove is unreliable.
        menu->setActiveAction(win.actCopyImageCurrentRow);
        QCOMPARE(menu->activeAction(), win.actCopyImageCurrentRow);
        QTest::keyPress(menu, Qt::Key_Alt);
        QVERIFY2(menu->isVisible(), "holding Alt over an export row closed the menu");
        QTest::keyRelease(menu, Qt::Key_Alt);
        QVERIFY2(menu->isVisible(), "releasing Alt closed the menu");
        menu->close();
        // Let the preview's dust-out flight (DUST_OUT_MS, exportPreview.cpp) finish and its
        // DisintegrateOverlay — parented to this popup — be collected before `win` dies.
        QTest::qWait(260);
        continue;
      }

      const bool overButton = route == NESTED_OVER_TOOLBAR_BUTTON;
      bool reached = false, survived = false, previewShown = false, hijacked = false;
      QTimer::singleShot(0, [&] {
        QMenu* root = nullptr;
        for (int i = 0; i < 200 && !root; ++i) {
          root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!root) QTest::qWait(10);
        }
        if (!root) return;
        QAction* layoutAct = nullptr;
        for (QAction* a : root->actions()) if (a->text() == "Image / Layout") layoutAct = a;
        if (!layoutAct || !layoutAct->menu()) { root->close(); return; }
        root->setActiveAction(layoutAct);
        QTest::keyClick(root, Qt::Key_Right);
        QMenu* layoutMenu = layoutAct->menu();
        settle([&] { return layoutMenu->isVisible(); }, 1000);
        QAction* copyAct = nullptr;
        for (QAction* a : layoutMenu->actions()) if (a->text().startsWith("Copy Image")) copyAct = a;
        if (!copyAct || !copyAct->menu()) { root->close(); return; }
        layoutMenu->setActiveAction(copyAct);
        QTest::keyClick(layoutMenu, Qt::Key_Right);
        QMenu* copyMenu = copyAct->menu();
        settle([&] { return copyMenu->isVisible(); }, 1000);
        if (!copyMenu->isVisible()) { root->close(); return; }
        // actCopyImageOriginal, not actCopyImage: the latter is no longer a row in this submenu, while
        // Original always is, and this route's point is Alt-key ROUTING, not which row it lands on.
        copyMenu->setActiveAction(win.actCopyImageOriginal);
        reached = true;
        // underMouse() backs up the cursor-position check in MainWindowEvents.cpp and is what an
        // offscreen-adjacent test can mock — a real QCursor::setPos warp may not land in time.
        if (overButton) copyBtn->setAttribute(Qt::WA_UnderMouse, true);
        QTest::keyPress(root, Qt::Key_Alt);
        if (overButton) {
          QTest::qWait(30);
          hijacked = win.copyImageOptionsMenu && win.copyImageOptionsMenu->isVisible();
        } else {
          // Checked directly, not just "did the menu survive" — a filter that does nothing
          // at all would trivially pass that half too.
          for (QWidget* w : QApplication::topLevelWidgets())
            if (w->objectName() == QLatin1String("exportPreviewTip") && w->isVisible())
              previewShown = true;
        }
        survived = copyMenu->isVisible() && layoutMenu->isVisible() && root->isVisible();
        QTest::keyRelease(root, Qt::Key_Alt);
        if (overButton) copyBtn->setAttribute(Qt::WA_UnderMouse, false);
        root->close();
        if (overButton && win.copyImageOptionsMenu) win.copyImageOptionsMenu->close();
      });
      win.showContextMenu(win.mapToGlobal(QPoint(500, 400)));
      QVERIFY2(reached, "never reached the nested Copy Image submenu");
      if (overButton)
        QVERIFY2(!hijacked, "Alt over the row opened the toolbar button's OWN options popup on top");
      else
        QVERIFY2(previewShown,
                 "Alt delivered to the chain's ROOT never reached the leaf's preview at all");
      QVERIFY2(survived, qPrintable(QString("%1: holding Alt closed the menu chain").arg(name)));
      QTest::qWait(260);
    }
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusAltHold.gui.moc"
