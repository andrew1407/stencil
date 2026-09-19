// MainWindow GUI e2e — The name bar's hover, the app's own popups, and the height the window asks for.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Enabling the f(x,y) pill reveals the x/y inputs, which stay visible across an image load and
  // resizes. Hovering the project name reveals ✎/🎨, sized to the row so the header cannot grow.
  void nameHoverDoesNotResizeTheHeader() {
    MainWindow win(nullptr, false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 300);
    win.actToolbars_->setChecked(false);   // collapsed: the header is all there is
    settleLayout(&win, 400);
    QToolBar* hdr = win.headerToolbar_;
    QVERIFY(hdr);
    const int before = hdr->height();
    win.nameBar_.hover = true;
    win.refreshProjectNameButtons();
    QTest::qWait(200);
    QCOMPARE(hdr->height(), before);
    // …and the same going into edit mode, where ✓/✗ take their place.
    win.nameBar_.field->setEnabled(true);
    QTest::mouseClick(win.nameBar_.edit, Qt::LeftButton);
    QTest::qWait(200);
    QCOMPARE(hdr->height(), before);
  }

  // Every selector opens the app's OWN popup, never the platform one (macOS draws a native combo
  // popup itself); the browser makes the same swap (js/ui/customSelect.js). Zoom is the exception.
  void everySelectorUsesTheAppsOwnPopup() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    int checked = 0;
    for (QComboBox* c : win.findChildren<QComboBox*>()) {
      if (c == win.zoom_) { QVERIFY2(c->isEditable(), "zoom is the editable exception"); continue; }
      // dynamic_cast, not qobject_cast: SearchComboBox is deliberately MOC-free.
      QVERIFY2(dynamic_cast<stencil::gui::SearchComboBox*>(c),
               qPrintable(QString("%1 still opens the platform popup").arg(c->objectName())));
      QCOMPARE(c->cursor().shape(), Qt::PointingHandCursor);
      ++checked;
    }
    QVERIFY2(checked >= 4, "no toolbar selectors were found to check");

    // …and opening one really puts OUR popup on screen, with the rows in it.
    QVERIFY(win.lineStyle_);
    win.lineStyle_->showPopup();
    QTRY_VERIFY(QApplication::activePopupWidget());
    QWidget* popup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) popup = w;
    QVERIFY2(popup, "the themed popup never appeared");
    auto* list = popup->findChild<QListView*>("searchComboList");
    QVERIFY(list);
    QCOMPARE(list->model()->rowCount(), win.lineStyle_->count());
    // A short list carries no search box — three rows need no filter.
    QVERIFY2(!popup->findChild<QLineEdit*>("searchComboSearch"), "a 3-row list grew a search box");
    win.lineStyle_->hidePopup();
    // The long ISO page list keeps its search box, which is what it was built for.
    win.units_.pageSize->showPopup();
    QTRY_VERIFY(QApplication::activePopupWidget());
    QWidget* pagePopup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) pagePopup = w;
    QVERIFY(pagePopup);
    QVERIFY2(pagePopup->findChild<QLineEdit*>("searchComboSearch"), "page formats lost their search");
    win.units_.pageSize->hidePopup();
  }

  // The window opens at the size it asked for: the wrapping tool run (support/WrapRow.hpp) hints its
  // WRAPPED height from the first pass, since a STACKED hint becomes QToolBarLayout's minimum.
  void theWindowOpensAtTheHeightItAsksFor() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 500);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTRY_COMPARE_WITH_TIMEOUT(win.width(), 1400, 500);
    QVERIFY2(win.height() < 700,
             qPrintable(QString("opened %1px tall, asked for 500").arg(win.height())));
    // The run really does wrap, and the minimum tracks it: a narrower window needs more
    // lines and honestly asks for the height they take.
    auto* row = win.findChild<QWidget*>("toolWrapRow");
    QVERIFY(row);
    win.resize(1500, 500);
    settle([&] { return win.width() == 1500; }, 500);
    const int wideRow = row->height(), wideMin = win.minimumSizeHint().height();
    win.resize(900, 500);
    settle([&] { return row->height() > wideRow; }, 500);
    QVERIFY2(row->height() > wideRow, "the run did not wrap onto more lines when narrowed");
    QVERIFY2(win.minimumSizeHint().height() > wideMin, "…and the minimum did not follow it");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chrome.gui.moc"
