// MainWindow GUI e2e — Alt+hover peeks a toolbar selector's list (support/menu/comboAltPeek)
// through the real filter, no Alt override: both entry orders with a text field focused, the
// release outside / inside with the linger close, the glide, and a click-open list left alone.
#include "altPeekGui.hpp"
#include "uiTimings.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void cleanup() { stencil::support::setMotionMode(MotionMode::PARTICLES); }

  void aToolbarSelectorPeeksOnAltHover_data() { addMotionRows(); }
  void aToolbarSelectorPeeksOnAltHover() {
    QFETCH(int, mode);
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode(mode));
    if (!cursorWarps(win)) QSKIP("the platform ignores QCursor::setPos");
    QComboBox* style = win.tools.lineStyle;
    QVERIFY(style && style->isVisible());
    QWidget* field = focusAField(&win);
    QVERIFY2(field, "no text field on the toolbar to hold the focus");
    QTRY_VERIFY(field->hasFocus());
    const QPoint away = centreOf(win.canvas);

    // Alt first, then the pointer arrives.
    QCursor::setPos(away);
    altKey(QEvent::KeyPress);
    QVERIFY2(!comboPopup(style), "Alt over the canvas opened a selector");
    QCursor::setPos(centreOf(style));
    enterWidget(style);
    QTRY_VERIFY2(comboPopup(style), "entering a selector with Alt held did not open its list");
    QWidget* list = comboPopup(style);
    QTRY_COMPARE_WITH_TIMEOUT(list->windowOpacity(), 1.0, 2000);
    QCursor::setPos(away);
    altKey(QEvent::KeyRelease);
    QTRY_VERIFY2(!list->isVisible(), "Alt released outside the list left it open");
    QTRY_VERIFY2_WITH_TIMEOUT(strayWindows(win).isEmpty(),
                              qPrintable(strayWindows(win).join(", ")), 2000);

    // The pointer rests first, then Alt — while a text field holds the focus.
    reactivate(win);
    field->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(field->hasFocus());
    QCursor::setPos(centreOf(style));
    altKey(QEvent::KeyPress);
    QTRY_VERIFY2(comboPopup(style), "Alt pressed while resting on a selector did not open it");
    list = comboPopup(style);
    QTRY_COMPARE_WITH_TIMEOUT(list->windowOpacity(), 1.0, 2000);
    QCursor::setPos(list->geometry().center());
    altKey(QEvent::KeyRelease);   // to the list, which holds the focus now
    QTest::qWait(stencil::support::uiTimings().lingerCloseMs + 250);
    QVERIFY2(comboPopup(style), "released over the list, it must linger");
    QCursor::setPos(away);
    QTRY_VERIFY2(!list->isVisible(), "the lingering list did not close once the pointer left");
    QTRY_VERIFY2_WITH_TIMEOUT(strayWindows(win).isEmpty(),
                              qPrintable(strayWindows(win).join(", ")), 2000);

    // An Alt glide carries the peek from one selector to the next.
    reactivate(win);
    QCursor::setPos(centreOf(style));
    altKey(QEvent::KeyPress);
    QTRY_VERIFY(comboPopup(style));
    QComboBox* page = nullptr;   // a selector the open list does not cover
    for (QComboBox* c : win.findChildren<QComboBox*>())
      if (c != style && c->isVisible() && c->isEnabled() &&
          !comboPopup(style)->geometry().contains(centreOf(c)))
        page = c;
    QVERIFY2(page, "no second selector clear of the open list");
    QCursor::setPos(centreOf(page));
    QTRY_VERIFY2(comboPopup(page) && !comboPopup(style), "the glide did not move the peek");
    QWidget* pageList = comboPopup(page);
    QCursor::setPos(away);
    altKey(QEvent::KeyRelease);
    QTRY_VERIFY2(!pageList->isVisible(), "the glided-to peek outlived the Alt release");

    // A list opened by click ignores Alt entirely.
    reactivate(win);
    QTest::mouseClick(style, Qt::LeftButton, Qt::NoModifier, style->rect().center());
    QTRY_VERIFY2(comboPopup(style), "a click did not open the selector");
    list = comboPopup(style);
    altKey(QEvent::KeyPress);
    QCursor::setPos(away);
    altKey(QEvent::KeyRelease);
    QTest::qWait(400);
    QVERIFY2(comboPopup(style), "an Alt release closed a list opened by click");
    style->hidePopup();
    QTRY_VERIFY(!list->isVisible());

    // A disabled selector opens nothing.
    reactivate(win);
    style->setEnabled(false);
    altKey(QEvent::KeyPress);
    QCursor::setPos(centreOf(style));
    enterWidget(style);
    QTest::qWait(150);
    QVERIFY2(!comboPopup(style), "a disabled selector peeked");
    altKey(QEvent::KeyRelease);
    style->setEnabled(true);
  }

  // A pick from a peeked list applies as a click-open pick does.
  void clickingARowInAPeekPicksIt() {
    const auto motion = withoutMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode::NONE);
    if (!cursorWarps(win)) QSKIP("the platform ignores QCursor::setPos");
    auto* style = static_cast<stencil::gui::SearchComboBox*>(win.tools.lineStyle);
    const int before = style->currentIndex();
    QCursor::setPos(centreOf(style));
    altKey(QEvent::KeyPress);
    QTRY_VERIFY(comboPopup(style));
    QListView* rows = style->popupList();
    const int want = (before + 1) % style->count();
    const QRect row = rows->visualRect(rows->model()->index(want, 0));
    QTest::mouseClick(rows->viewport(), Qt::LeftButton, Qt::AltModifier, row.center());
    QTRY_VERIFY(!comboPopup(style));
    QCOMPARE(style->currentIndex(), want);
    altKey(QEvent::KeyRelease);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.altPeekSelectors.gui.moc"
