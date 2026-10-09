// MainWindow GUI e2e — a Lines row's eye and name, as the browser's tests/ui/panel/lines suites pin
// them: the eye hides and shows its line as one undo step each, a hidden line is dimmed in the list,
// never painted and never hit, and a name typed in place is one undo step, Escape keeping the old one.
// Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include <QLineEdit>

namespace {
  enum { NAME_COL = 1, EYE_COL = 7 };   // selectionPanelParts.hpp LineCol
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static stencil::core::Line lineAt(double y) {
    stencil::core::Line line;
    line.points = {{20, y}, {200, y}};
    line.color = "#ff0000";
    line.thickness = 6;
    return line;
  }
  static QTableWidget* listOf(MainWindow& win) { return win.findChild<QTableWidget*>("linesList"); }
  static QPushButton* eyeOf(QTableWidget* list, int row) {
    return list->cellWidget(row, EYE_COL)->findChild<QPushButton*>(QStringLiteral("linesEyeBtn"));
  }

  static CanvasWidget* seed(MainWindow& win) {
    CanvasWidget* canvas = openLoaded(win);
    canvas->setLines({lineAt(40), lineAt(120)});
    settle([&win] { return listOf(win)->rowCount() == 2; });
    return canvas;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theEyeHidesItsLineOutOfThePaintAndTheHits() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = seed(win);
    QTableWidget* list = listOf(win);
    QCOMPARE(eyeOf(list, 0)->toolTip(), QString("Hide line"));
    const int before = canvas->selectLineAt(100, 40);
    QCOMPARE(before, 0);
    canvas->deselect();
    eyeOf(list, 0)->click();
    QTRY_VERIFY(canvas->getLines()[0].hidden);
    QTRY_COMPARE(eyeOf(list, 0)->toolTip(), QString("Show line"));
    QCOMPARE(canvas->selectLineAt(100, 40), -1);   // a hidden line is no target
    const QImage shot = canvas->renderToImage(true);
    QVERIFY2(shot.pixelColor(100, 40) != QColor("#ff0000"), "a hidden line is not painted");
    QCOMPARE(shot.pixelColor(100, 120), QColor("#ff0000"));   // the shown one is
    canvas->undo();
    QVERIFY(!canvas->getLines()[0].hidden);   // one undo step
    canvas->redo();
    QVERIFY(canvas->getLines()[0].hidden);
    QTRY_VERIFY(eyeOf(list, 0));
    eyeOf(list, 0)->click();
    QTRY_VERIFY(!canvas->getLines()[0].hidden);
  }

  void aNameTypedInPlaceIsOneStepAndEscapeKeepsIt() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = seed(win);
    QTableWidget* list = listOf(win);
    QCOMPARE(list->item(1, NAME_COL)->text(), QString("Line 2"));   // unnamed: its "Line N"
    QVERIFY(list->item(1, NAME_COL)->font().italic());
    emit list->cellClicked(1, NAME_COL);
    QCOMPARE(canvas->getSelectedLineIdx(), -1);   // a name never selects
    list->editItem(list->item(1, NAME_COL));
    auto* edit = list->findChild<QLineEdit*>(QStringLiteral("linesNameEdit"));
    QVERIFY(edit);
    QCOMPARE(edit->text(), QString("Line 2"));   // an unnamed line opens on the name it shows…
    QCOMPARE(edit->selectedText(), QString("Line 2"));   // …selected
    QTest::keyClick(edit, Qt::Key_Return);
    QTest::qWait(30);
    QCOMPARE(canvas->getLines()[1].name, std::string());   // committed untouched, it stays unnamed
    list->editItem(list->item(1, NAME_COL));
    edit = list->findChild<QLineEdit*>(QStringLiteral("linesNameEdit"));
    QVERIFY(edit);
    edit->setText(QStringLiteral("  Roof ridge  "));
    QTest::keyClick(edit, Qt::Key_Return);
    QTRY_COMPARE(canvas->getLines()[1].name, std::string("Roof ridge"));
    QTRY_COMPARE(list->item(1, NAME_COL)->text(), QString("Roof ridge"));
    QVERIFY(!list->item(1, NAME_COL)->font().italic());
    list->editItem(list->item(1, NAME_COL));
    edit = list->findChild<QLineEdit*>(QStringLiteral("linesNameEdit"));
    QVERIFY(edit);
    edit->setText(QStringLiteral("Other"));
    QTest::keyClick(edit, Qt::Key_Escape);
    QTest::qWait(30);
    QCOMPARE(canvas->getLines()[1].name, std::string("Roof ridge"));
    canvas->undo();
    QCOMPARE(canvas->getLines()[1].name, std::string());   // one undo step
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.lineVisibility.gui.moc"
