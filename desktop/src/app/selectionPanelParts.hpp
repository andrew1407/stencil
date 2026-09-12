#pragma once
// The points table's columns, chevron metrics and row delegate, private to the selectionPanel*.cpp TUs.
#include <QModelIndex>
#include <QPainter>
#include <QPen>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

namespace stencil::gui {

  // One for one with the browser's coordinates table (mainContent.js <thead>).
  enum PointCol { ColIndex = 0, ColX, ColY, ColPageX, ColPageY, ColDel, ColCount };

  // Also the floating re-open chevron's (mainWindow kPanelToggleBox).
  inline constexpr int kToggleBox = 24;
  inline constexpr int kToggleGlyph = 15;


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
      if (idx.column() == ColIndex) p->drawLine(r.topLeft(), r.bottomLeft());
      if (idx.column() == ColCount - 1) p->drawLine(r.topRight(), r.bottomRight());
      p->restore();
    }
  };

}  // namespace stencil::gui
