#pragma once
// The points table's columns, chevron metrics and row delegate, private to the selectionPanel*.cpp TUs.
#include <QModelIndex>
#include <QPainter>
#include <QPen>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

namespace stencil::gui {

  // Points-table columns, one for one with the browser's coordinates table
  // (mainContent.js <thead>): index · X px (editable) · Y px (editable) · X page ·
  // Y page (both read-only, in the app's current unit) · 🗑.
  enum PointCol { ColIndex = 0, ColX, ColY, ColPageX, ColPageY, ColDel, ColCount };

  // The header chevron's box + glyph. Also the floating re-open chevron's, which has to
  // read as the same button (mainWindow kPanelToggleBox).
  inline constexpr int kToggleBox = 24;
  inline constexpr int kToggleGlyph = 15;


  // Paints a selected row as a flat accent OUTLINE (not a filled background); hover tint + cell
  // text come from QSS / the base. Mirrors the browser row treatment but with an outline.
  class PointRowDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
      // The selection FILL is made transparent via QSS (selection-background-color); here we
      // just stroke an accent outline around the selected row on top of the normal item paint.
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
