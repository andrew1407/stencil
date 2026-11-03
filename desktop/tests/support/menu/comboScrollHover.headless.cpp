// A selector's list scrolled under a still pointer (support/motion/RowHoverSlide.hpp, browser
// .accent-dd-opt:hover): the row that lands under the pointer takes the hover, its background and its 2px
// slide, as a browser re-applies :hover once a scroll ends; no row it left behind keeps either.
#include "SearchCombo.hpp"

#include <QApplication>
#include <QCursor>
#include <QListView>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QStyleFactory>
#include <QTest>
#include <QWheelEvent>
#include <map>

#include "../check.hpp"

using stencil::gui::SearchComboBox;

namespace {
  struct Probe : QStyledItemDelegate {
    std::map<int, bool> over;
    std::map<int, int> x;
    void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const override {
      const_cast<Probe*>(this)->over[i.row()] = o.state & QStyle::State_MouseOver;
      const_cast<Probe*>(this)->x[i.row()] = o.rect.x();
      QStyledItemDelegate::paint(p, o, i);
    }
  };
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  QWidget host;
  auto* combo = new SearchComboBox(&host, false);
  for (int i = 0; i < 40; ++i) combo->addItem(QString("item %1").arg(i));
  combo->setGeometry(8, 8, 160, 24);
  host.resize(400, 600);
  host.show();
  (void)QTest::qWaitForWindowExposed(&host);
  auto* probe = new Probe;
  combo->setListDelegate(probe);
  combo->showPopup();
  QListView* list = combo->popupList();
  QTest::qWait(50);
  const QPoint at = list->viewport()->mapToGlobal(list->visualRect(list->model()->index(3, 0)).center());
  QCursor::setPos(at);
  QTest::mouseMove(list->viewport(), list->viewport()->mapFromGlobal(at));
  QTest::qWait(200);
  const auto painted = [list, probe](int* hovered, int* slid) {
    probe->over.clear();
    probe->x.clear();
    list->viewport()->repaint();
    int base = 1 << 30;
    for (auto& [r, xx] : probe->x) base = std::min(base, xx);
    *hovered = *slid = -1;
    for (auto& [r, o] : probe->over) if (o) *hovered = r;
    for (auto& [r, xx] : probe->x) if (xx != base) *slid = r;
  };
  int hovered = -1, slid = -1;
  painted(&hovered, &slid);
  check(hovered == 3 && slid == 3, "the row under the pointer is hovered and slid");
  QWheelEvent wheel(list->viewport()->mapFromGlobal(at), at, QPoint(0, -120), QPoint(0, -120), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(list->viewport(), &wheel);
  QTest::qWait(200);
  const int under = list->indexAt(list->viewport()->mapFromGlobal(QCursor::pos())).row();
  check(list->verticalScrollBar()->value() > 0 && under > 3, "the wheel scrolled other rows under the pointer");
  painted(&hovered, &slid);
  check(hovered == under, "the row scrolled under the pointer takes the hover background");
  check(slid == under, "…and the hover slide, none left on the row scrolled away");
  return failures ? 1 : 0;
}
