// MainWindow GUI e2e — the window's COMPOSITION pin: what the constructor builds, and in
// which order. Construction order is observable — tab order, sibling stacking, the order
// findChildren reports, dock placement, toolbar rows — so the ctor's setup phases may be
// re-cut freely but must leave every expectation below byte-identical. A deliberate UI
// change re-records them, in its own commit, the way tests/pins re-records a render.
#include "mainWindow.gui.hpp"

namespace {
  // One widget, named so a failure reads: class plus objectName.
  QString tag(const QWidget* w) {
    const QString name = w->objectName();
    return QString::fromLatin1(w->metaObject()->className()) +
           (name.isEmpty() ? QString() : "#" + name);
  }

  // Every focusable widget of `start`'s window, in tab order — which Qt derives from
  // CREATION order for all of them, since nothing here calls setTabOrder.
  QStringList focusChain(QWidget* start) {
    QStringList out;
    QWidget* w = start;
    for (int i = 0; i < 4000; ++i) {
      w = w->nextInFocusChain();
      if (!w || w == start) break;
      if (w->focusPolicy() != Qt::NoFocus && w->window() == start->window()) out << tag(w);
    }
    return out;
  }

  // That chain with each run of one class collapsed to "Class N": compact, and still
  // sensitive to any widget built out of turn.
  QString focusRuns(const QStringList& chain) {
    QStringList runs;
    for (const QString& e : chain) {
      const QString cls = e.section(u'#', 0, 0);
      if (!runs.isEmpty() && runs.last().section(u' ', 0, 0) == cls)
        runs.last() = cls + " " + QString::number(runs.last().section(u' ', 1, 1).toInt() + 1);
      else
        runs << cls + " 1";
    }
    return runs.join(u'|');
  }

  // …and the named widgets in it, which is what a reader can actually locate.
  QString focusMarks(const QStringList& chain) {
    QStringList marks;
    for (const QString& e : chain) {
      const QString name = e.section(u'#', 1);
      if (!name.isEmpty() && !name.startsWith("qt_") && !name.startsWith("Scroll")) marks << name;
    }
    return marks.join(u' ');
  }

  // Direct child widgets in QObject order — the stacking of siblings nobody raised.
  QString childOrder(const QWidget* parent) {
    QStringList out;
    for (QObject* o : parent->children())
      if (auto* w = qobject_cast<QWidget*>(o)) out << tag(w);
    return out.join(u' ');
  }
}

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Re-record by running this binary with -maxwarnings 0: every failure prints the
  // string it actually saw.
  void constructionOrderIsPinned() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    const QStringList chain = focusChain(&win);
    QCOMPARE(chain.size(), 104);
    QCOMPARE(focusRuns(chain),
             QStringLiteral(
                 "stencil::gui::CanvasWidget 1|QScrollArea 1|QToolButton 2|QTabWidget 1|"
                 "QTabBar 1|QToolButton 2|QTableWidget 1|QListWidget 1|QPushButton 2|"
                 "stencil::gui::ExprSpinBox 1|QLineEdit 1|stencil::gui::ExprSpinBox 1|"
                 "QLineEdit 1|QComboBox 1|QPushButton 4|QToolButton 6|QScrollArea 1|"
                 "QToolButton 2|QPlainTextEdit 1|QToolButton 5|QComboBox 2|QLineEdit 1|"
                 "QToolButton 2|QLineEdit 1|QToolButton 21|QComboBox 1|QToolButton 10|"
                 "stencil::gui::ExprSpinBox 1|QLineEdit 1|stencil::gui::ExprSpinBox 1|"
                 "QLineEdit 1|QComboBox 1|QToolButton 1|QComboBox 1|QCheckBox 2|"
                 "QToolButton 1|QComboBox 1|QToolButton 2|"
                 "stencil::gui::ExprDoubleSpinBox 1|QLineEdit 1|"
                 "stencil::gui::ExprDoubleSpinBox 1|QLineEdit 1|QCheckBox 1|QLineEdit 2|"
                 "QToolButton 10"));
    QCOMPARE(focusMarks(chain),
             QStringLiteral("canvasViewport selectionTabs pointsTable linesList "
                            "selectedLineFillSwatch selectedLineFillClear "
                            "selectedLineUnchain selectedLineDeselect chatJumpBtn "
                            "chatJumpBtn chatInput chatSend chatAttach chatGear chatMore "
                            "chatClear controlsPill projectNameField drawFaceBtn "
                            "drawFaceBtn formulaPill"));

    // The canvas overlays stack in the order the ctor makes them: incognito frame, the
    // split image-drop zones, then the project drag zones.
    auto* viewport = win.findChild<QScrollArea*>("canvasViewport")->viewport();
    QCOMPARE(childOrder(viewport),
             QStringLiteral("stencil::gui::CanvasWidget stencil::gui::IncognitoOverlay "
                            "QWidget QWidget"));

    // The central column: the canvas row, then the coord readout, then the drop hint.
    QStringList central;
    for (int i = 0; i < win.centralLayout_->count(); ++i) {
      QLayoutItem* it = win.centralLayout_->itemAt(i);
      central << (it->widget() ? tag(it->widget()) : QStringLiteral("<layout>"));
    }
    QCOMPARE(central.join(u' '),
             QStringLiteral("<layout> QLabel#coordStatus QWidget#dropHintBar"));

    // Toolbar rows, in the order addToolBar/addToolBarBreak ran (4 = Qt::TopToolBarArea).
    QStringList bars;
    for (QToolBar* tb : win.findChildren<QToolBar*>())
      bars << tag(tb) + "@" + QString::number(int(win.toolBarArea(tb)));
    QCOMPARE(bars.join(u' '),
             QStringLiteral("QToolBar#headerToolbar@4 QToolBar#mainToolbar@4"));

    // Docks, in creation order, with the area each was added to and its boot visibility
    // (2 = Right, 4 = Top, 1 = Left). The chat dock is session-transient: always hidden
    // at its default left placement, whatever a saved layout said.
    QStringList docks;
    for (QDockWidget* d : win.findChildren<QDockWidget*>())
      docks << tag(d) + "@" + QString::number(int(win.dockWidgetArea(d))) +
                   (d->isHidden() ? ":hidden" : ":shown");
    QCOMPARE(docks.join(u' '),
             QStringLiteral("stencil::gui::SelectionPanel#selectionPanelDock@2:shown "
                            "QDockWidget#selectedLineDock@4:hidden "
                            "stencil::gui::ChatDock#llmChatDock@1:hidden "
                            "QDockWidget#imageInfoDock@4:shown"));

    // The controllers and overlays the ctor owns all exist, and the window is wired to
    // its own filters (Escape leaves fullscreen from any focus; the viewport zooms).
    QVERIFY(win.notify_ && win.remoteSession_ && win.dataExport_ && win.remoteSync_ &&
            win.projectTransfer_ && win.tooltip_ && win.arrowPanTimer_ &&
            win.scrollbarHideTimer_ && win.chatNaturalMin_.width() > 0);
    QVERIFY(win.testAttribute(Qt::WA_Hover));
    QVERIFY(win.acceptDrops());
    QCOMPARE(win.windowTitle(), QStringLiteral("Stencil"));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.composition.gui.moc"
