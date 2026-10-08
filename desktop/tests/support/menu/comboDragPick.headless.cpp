// Press, drag and release on a selector (support/menu/SearchCombo's dragPick, browser twin
// ui/control/dropdownMenu.js wireDragPick), driven through the window system as cocoa delivers a held
// press: a release on a row picks it as a click does, off the list it closes with no change, a plain
// click keeps the list open; a plain QComboBox's own Qt list already picks the same way.
#include "SearchCombo.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QListView>
#include <QStyleFactory>
#include <QTest>

#include "../check.hpp"

using stencil::gui::SearchComboBox;

namespace {
  constexpr int REOPEN_GAP_MS = 200;   // SearchComboBox ignores a reopen 150 ms after a close

  QWindow* windowOf(QWidget* w) { return w->window()->windowHandle(); }
  QPoint inWindow(QWidget* w, const QPoint& global) { return windowOf(w)->mapFromGlobal(global); }
  QPoint centreOf(QWidget* w) { return w->mapToGlobal(w->rect().center()); }
  // Every event to the trigger's window, where the press began: the popup routing takes it from there.
  void press(QWidget* trigger) { QTest::mousePress(windowOf(trigger), Qt::LeftButton, {}, inWindow(trigger, centreOf(trigger))); }
  void moveTo(QWidget* trigger, const QPoint& global) { QTest::mouseMove(windowOf(trigger), inWindow(trigger, global)); }
  void release(QWidget* trigger, const QPoint& global) {
    QTest::mouseRelease(windowOf(trigger), Qt::LeftButton, {}, inWindow(trigger, global));
  }
  QPoint rowAt(QAbstractItemView* view, int row) {
    return view->viewport()->mapToGlobal(view->visualRect(view->model()->index(row, 0)).center());
  }
  bool shown(SearchComboBox* c) { return c->popupWindow() && c->popupWindow()->isVisible(); }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));   // the app's own style
  QWidget host;
  auto* row = new QHBoxLayout(&host);
  row->setContentsMargins(8, 8, 8, 300);
  auto* combo = new SearchComboBox(&host, /*searchable=*/false);
  combo->addItems({"one", "two", "three"});
  combo->setFixedWidth(120);
  row->addWidget(combo);
  auto* plain = new QComboBox(&host);
  plain->addItems({"x", "y", "z"});
  plain->setFixedWidth(120);
  row->addWidget(plain);
  row->addStretch(1);
  host.resize(420, 420);
  host.show();
  (void)QTest::qWaitForWindowExposed(&host);
  int activated = -1;
  QObject::connect(combo, &QComboBox::activated, [&activated](int i) { activated = i; });

  press(combo);
  check(shown(combo), "a press opens the list");
  release(combo, centreOf(combo));
  check(shown(combo) && combo->currentIndex() == 0 && activated == -1, "a plain click keeps it open, unchanged");
  combo->hidePopup();
  QTest::qWait(REOPEN_GAP_MS);

  press(combo);
  QListView* list = combo->popupList();
  const QPoint two = rowAt(list, 1);
  moveTo(combo, two);
  check(list->currentIndex().row() == 1, "dragging over a row highlights it");
  release(combo, two);
  check(!shown(combo) && combo->currentIndex() == 1 && activated == 1, "released on a row, it is picked as a click picks it");
  QTest::qWait(REOPEN_GAP_MS);

  activated = -1;
  press(combo);
  moveTo(combo, rowAt(list, 2));
  const QPoint padding = combo->popupWindow()->mapToGlobal(QPoint(1, 1));
  moveTo(combo, padding);
  release(combo, padding);
  check(shown(combo) && combo->currentIndex() == 1, "released inside the list but on no row, it stays open");
  combo->hidePopup();
  QTest::qWait(REOPEN_GAP_MS);

  press(combo);
  moveTo(combo, rowAt(list, 2));
  const QPoint away = host.mapToGlobal(QPoint(400, 400));
  moveTo(combo, away);
  release(combo, away);
  check(!shown(combo) && combo->currentIndex() == 1 && activated == -1, "released off the list, it closes with no change");

  press(plain);
  QAbstractItemView* view = plain->view();
  check(view->isVisible(), "a plain combo's press opens Qt's own list");
  const QPoint z = rowAt(view, 2);
  moveTo(plain, z);
  release(plain, z);
  check(!view->isVisible() && plain->currentIndex() == 2, "…which picks a row a press is dragged onto and released on");

  return failures ? 1 : 0;
}
