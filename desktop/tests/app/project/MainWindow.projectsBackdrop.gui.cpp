// MainWindow GUI e2e — the window behind an open dialog is a photograph (support/modal/ModalBackdrop.hpp): when the
// Projects list removes the open project the editor clears, and that photograph is retaken, so the cleared editor
// shows behind the list rather than the picture that was removed (app/project/flows/ProjectFlowsCrud.cpp).
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/modal/ModalBackdrop.hpp"

#include <QDialog>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void clearingTheEditorRetakesTheBackdrop() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas && canvas->hasImage());
    settleLayout(&win, 120);
    QDialog dlg(&win);
    auto* bd = stencil::support::ModalBackdrop::behind(&dlg, &win);
    QVERIFY(bd);
    QTest::qWait(300);   // its fade in
    const QImage before = bd->grab().toImage();
    win.projectHost.flows.resetToBlankEditor();   // what removing the open project runs
    QVERIFY(!canvas->hasImage());
    QTRY_VERIFY2(bd->grab().toImage() != before, "the backdrop still shows the picture that was removed");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsBackdrop.gui.moc"
