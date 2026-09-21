// MainWindow GUI e2e — A second Right into the assistant's input, and the radio flyouts the keys pick through.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The Assistant flyout's own version of the rule above: the first → reveals the chat, the second
  // → lands in its text box (user decision), so a reply can be typed without touching the mouse.
  void ctxAssistantFlyoutSecondRightFocusesItsInput() {
    MainWindow win(nullptr, false);
    win.settings.llmProvider = "ollama";
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    bool opened = false, revealedOnly = false, entered = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      QAction* assistAct = nullptr;
      for (QAction* a : root->actions()) if (a->text().startsWith("Assistant")) assistAct = a;
      if (!assistAct || !assistAct->menu()) { root->close(); return; }
      root->setActiveAction(assistAct);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* chat = assistAct->menu();
      settle([&] { return chat->isVisible(); }, 1000);
      opened = chat->isVisible();
      QTest::qWait(80);
      revealedOnly = QApplication::focusWidget() != win.chatMenuInput;
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);
      QTest::qWait(30);
      entered = QApplication::focusWidget() == win.chatMenuInput;
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Assistant row did not open the chat flyout");
    QVERIFY2(revealedOnly, "the first Right already put the caret in the chat input");
    QVERIFY2(entered, "the second Right did not focus the chat input");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  // Radio-style flyouts pick as the keys move (browser parity): in Image Filter the second → lands
  // on the checked radio and ↓/↑ pick the neighbour; Style's Solid/Dashed/Dotted rows do the same.
  void ctxRadioFlyoutsPickAsTheKeysMove() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    const auto checkedFilter = [&win] {
      for (QAbstractButton* b : win.filterButtons->buttons())
        if (b->isChecked()) return b->property("filterValue").toString();
      return QString();
    };
    bool filterOpened = false, landedOnChecked = false, downPicked = false, upPicked = false,
         stayedOpen = false, styleOpened = false, styleApplied = false, styleStayedOpen = false,
         kbIconMotion = false, rootRouteEntered = false, rootRoutePicked = false, rootRouteFolded = false;
    QString afterDown, afterUp, afterRootDown;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      auto rowNamed = [&](const QString& title) -> QAction* {
        for (QAction* a : root->actions()) if (a->text().startsWith(title)) return a;
        return nullptr;
      };
      // Walk down from the top of the root to a row (the keys go to the active popup).
      auto walkTo = [&](QAction* act) {
        return walkMenu(root, Qt::Key_Down, [&] { return root->activeAction() == act; });
      };
      QAction* filterAct = rowNamed("Image Filter");
      if (!filterAct || !filterAct->menu() || !walkTo(filterAct)) { root->close(); return; }
      // Landing on a row with the keys is a hover: its icon motion runs (iconMotion.hpp).
      QTest::qWait(60);
      kbIconMotion = stencil::support::motionReduced()
                     || stencil::gui::icm::runnerOfAction(filterAct) != nullptr;
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* filter = filterAct->menu();
      settle([&] { return filter->isVisible(); }, 1000);
      filterOpened = filter->isVisible();
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);   // enter
      QTest::qWait(50);
      auto* focused = qobject_cast<QRadioButton*>(QApplication::focusWidget());
      landedOnChecked = focused && focused->isChecked() && checkedFilter() == "none";
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
      QTest::qWait(80);
      afterDown = checkedFilter();
      downPicked = afterDown == "bw" && qobject_cast<QRadioButton*>(QApplication::focusWidget())
                   && qobject_cast<QRadioButton*>(QApplication::focusWidget())->isChecked();
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
      QTest::qWait(80);
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
      QTest::qWait(80);
      afterUp = checkedFilter();
      upPicked = afterUp == "bw";
      stayedOpen = filter->isVisible() && root->isVisible();
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Left);
      settle([&] { return !(filter->isVisible()); }, 1000);

      // The other route: a flyout opened the way a HOVER opens it leaves the keyboard with the root, so
      // its keys must still reach the focused radio (StayOpenMenu.cpp forwards them).
      root->setActiveAction(filterAct);
      settle([&] { return filter->isVisible(); }, 1000);
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      QTest::keyClick(root, Qt::Key_Right);
      QTest::qWait(50);
      rootRouteEntered = qobject_cast<QRadioButton*>(QApplication::focusWidget()) != nullptr;
      QTest::keyClick(root, Qt::Key_Down);
      QTest::qWait(80);
      afterRootDown = checkedFilter();
      rootRoutePicked = afterRootDown == "sepia";
      QTest::keyClick(root, Qt::Key_Left);
      settle([&] { return !(filter->isVisible()); }, 1000);
      rootRouteFolded = !filter->isVisible() && root->isVisible();

      // Style: reveal it, enter it (the point-size spinner), Tab past both spinners onto its first plain
      // row, then walk. Keys go where the platform sends them: the popup's focus widget if it has one.
      auto keyTo = [](Qt::Key k) {
        QWidget* popup = QApplication::activePopupWidget();
        QWidget* receiver = popup && popup->focusWidget() ? popup->focusWidget() : popup;
        QTest::keyClick(receiver, k);
      };
      QAction* styleAct = rowNamed("Style");
      if (!styleAct || !styleAct->menu()) { root->close(); return; }
      walkMenu(root, Qt::Key_Up, [&] { return root->activeAction() == styleAct; });
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* style = styleAct->menu();
      settle([&] { return style->isVisible(); }, 1000);
      styleOpened = style->isVisible();
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      keyTo(Qt::Key_Right);   // enter: the point-size spinner
      QTest::qWait(40);
      keyTo(Qt::Key_Tab);     // thickness
      QTest::qWait(40);
      keyTo(Qt::Key_Tab);     // off the last control → the first plain row (Solid)
      QTest::qWait(40);
      for (int i = 0; i < 6 && style->activeAction() != win.actStyleDashed; ++i) {
        keyTo(Qt::Key_Down);
        QTest::qWait(30);
      }
      styleApplied = style->activeAction() == win.actStyleDashed && win.actStyleDashed->isChecked()
                     && win.settings.defaultStyle == "dashed";
      styleStayedOpen = style->isVisible() && root->isVisible();
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(filterOpened, "Right on the Image Filter row did not open its flyout");
    QVERIFY2(landedOnChecked, "the second Right did not land on the checked radio (None)");
    QVERIFY2(downPicked, qPrintable("Down did not pick the next filter — checked: " + afterDown));
    QVERIFY2(upPicked, qPrintable("Up did not pick the previous filter — checked: " + afterUp));
    QVERIFY2(stayedOpen, "picking a filter with the arrows closed the menu");
    QVERIFY2(kbIconMotion, "landing on a row with the keys did not run its icon motion");
    QVERIFY2(rootRouteEntered, "root-held keys: Right did not focus a radio in the hover-opened flyout");
    QVERIFY2(rootRoutePicked, qPrintable("root-held keys: Down did not pick the next filter — checked: " + afterRootDown));
    QVERIFY2(rootRouteFolded, "root-held keys: Left did not fold the flyout");
    QVERIFY2(styleOpened, "Right on the Style row did not open its flyout");
    QVERIFY2(styleApplied, "walking onto Dashed did not apply the dashed style");
    QVERIFY2(styleStayedOpen, "applying a style with the arrows closed the menu");
    win.applyImageFilter("none");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menuKeysFlyout.gui.moc"
