#pragma once
// The points table's columns, chevron metrics and row delegate, private to the SelectionPanel*.cpp TUs.
#include <QModelIndex>
#include <QPainter>
#include <QPen>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

namespace stencil::gui {

  // One for one with the browser's coordinates table (mainContent.js <thead>).
  enum PointCol { COL_INDEX = 0, COL_X, COL_Y, COL_PAGE_X, COL_PAGE_Y, COL_DEL, COL_COUNT };

  // Also the floating re-open chevron's (mainWindow PANEL_TOGGLE_BOX).
  inline constexpr int TOGGLE_BOX = 24;
  inline constexpr int TOGGLE_GLYPH = 15;


  // A selected row is a flat accent outline; hover tint + text come from QSS.
  class PointRowDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
      // The selection fill is transparent via QSS; only the outline is stroked here.
      QStyledItemDelegate::paint(p, opt, idx);
      if (!(opt.state & QStyle::State_Selected)) return;
      p->save();
      p->setRenderHint(QPainter::Antialiasing, false);
      p->setPen(QPen(opt.palette.color(QPalette::Highlight), 2));
      const QRect r = opt.rect.adjusted(0, 1, 0, -1);
      p->drawLine(r.topLeft(), r.topRight());
      p->drawLine(r.bottomLeft(), r.bottomRight());
      if (idx.column() == COL_INDEX) p->drawLine(r.topLeft(), r.bottomLeft());
      if (idx.column() == COL_COUNT - 1) p->drawLine(r.topRight(), r.bottomRight());
      p->restore();
    }
  };

}  // namespace stencil::gui
