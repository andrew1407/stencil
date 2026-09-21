// MainWindow GUI e2e — The script flyout owning Tab and Ctrl+Enter.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The script flyout: the same two-step → as the Assistant, but the editor then OWNS Tab — it
  // indents by two rather than walking the four actions (browser ctxScriptItem.js), Ctrl+Enter runs.
  void ctxScriptFlyoutOwnsTabAndCtrlEnter() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    auto* canvas = win.findChild<CanvasWidget*>();
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(20);
    bool opened = false, revealedOnly = false, entered = false;
    bool indented = false, stillInEditor = false, ran = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);
      QAction* scriptAct = nullptr;   // startsWith: the row carries its Alt+Shift+S hint
      for (QAction* a : root->actions())
        if (a->text().startsWith("Stencil Script")) scriptAct = a;
      if (!scriptAct || !scriptAct->menu()) { root->close(); return; }
      root->setActiveAction(scriptAct);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* flyout = scriptAct->menu();
      settle([&] { return flyout->isVisible(); }, 1000);
      opened = flyout->isVisible();
      QTest::qWait(80);
      revealedOnly = QApplication::focusWidget() != win.scriptMenuEditor;
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);
      QTest::qWait(30);
      entered = QApplication::focusWidget() == win.scriptMenuEditor;

      auto* edit = flyout->findChild<QPlainTextEdit*>("scriptMenuText");
      if (!edit) { root->close(); return; }
      edit->clear();
      QTest::keyClick(flyout, Qt::Key_Tab);
      indented = edit->toPlainText() == QLatin1String("  ");
      stillInEditor = QApplication::focusWidget() == win.scriptMenuEditor;

      const int before = int(canvas->allLines().size());
      edit->setPlainText(QStringLiteral("@line (1,1) (10,1) (10,8)"));
      QTest::keyClick(flyout, Qt::Key_Return, Qt::ControlModifier);
      settle([&] { return int(canvas->allLines().size()) > before; }, 1000);
      ran = int(canvas->allLines().size()) == before + 1 && flyout->isVisible();
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Stencil Script row did not open the editor flyout");
    QVERIFY2(revealedOnly, "the first Right already put the caret in the editor");
    QVERIFY2(entered, "the second Right did not focus the editor");
    QVERIFY2(indented, "Tab walked the controls instead of indenting the script");
    QVERIFY2(stillInEditor, "Tab moved the focus out of the editor");
    QVERIFY2(ran, "Ctrl+Enter did not run the script with the flyout still open");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menuKeysScript.gui.moc"
