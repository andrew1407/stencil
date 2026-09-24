#pragma once
// The points table's columns, chevron metrics and row delegate, private to the SelectionPanel*.cpp TUs.
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QModelIndex>
#include <QPainter>
#include <QPen>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include "../../support/skinPrefs.hpp"

namespace stencil::gui {

  // One for one with the browser's coordinates table (mainContent.js <thead>).
  enum PointCol { COL_INDEX = 0, COL_X, COL_Y, COL_PAGE_X, COL_PAGE_Y, COL_DEL, COL_COUNT };
  // …and with its lines table, the same widget so the two tabs read as one panel.
  enum LineCol { LCOL_INDEX = 0, LCOL_SWATCH, LCOL_NAME, LCOL_PTS, LCOL_DEL, LCOL_COUNT };
  // A row the CANVAS selected; the view's own selection is the current row, what Delete acts on.
  inline constexpr int SELECTED_ROLE = Qt::UserRole + 1;

  // Also the floating re-open chevron's (mainWindow PANEL_TOGGLE_BOX).
  inline constexpr int TOGGLE_BOX = 24;
  inline constexpr int TOGGLE_GLYPH = 15;

  // The Lines tab's fixed columns (browser .lines-table states every width but the name's).
  // SWATCH is sized by the word "Color" over the chip, not by the 14px chip itself.
  inline constexpr int LINE_COL_SWATCH = 46;
  inline constexpr int LINE_COL_PTS = 40;

  // Browser .coordinates-table rows: one text line, td padding 6px above and below, a 1px rule.
  inline void fitTableRows(QTableWidget* t) {
    t->ensurePolished();
    t->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    t->verticalHeader()->setDefaultSectionSize(t->fontMetrics().height() + 13);
    t->updateGeometry();
  }

  // A table no taller than its rows: a scroll area's own minimum is its scrollbars' length,
  // which outgrew the one-row empty message and left a blank band under it.
  class FitTable : public QTableWidget {
   public:
    using QTableWidget::QTableWidget;
    QSize minimumSizeHint() const override {
      const QSize base = QTableWidget::minimumSizeHint();
      return {base.width(), qMin(base.height(), sizeHint().height())};
    }
  };

  // A cell widget held at the cell's centre (browser td text-align/vertical-align center); a
  // bare one is stretched, or pinned to the top once its QSS caps its height.
  inline QWidget* centeredCell(QWidget* child, QWidget* parent) {
    auto* cell = new QWidget(parent);
    auto* lay = new QHBoxLayout(cell);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(child, 0, Qt::AlignCenter);
    return cell;
  }

  // A selected row is a flat accent outline; hover tint + text come from QSS. The Lines tab
  // marks its rows with SELECTED_ROLE, because there the canvas owns the selection.
  class PointRowDelegate : public QStyledItemDelegate {
   public:
    explicit PointRowDelegate(QObject* parent, int lastColumn = COL_COUNT - 1)
        : QStyledItemDelegate(parent), lastColumn(lastColumn) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
      // The selection fill is transparent via QSS; only the outline is stroked here.
      QStyledItemDelegate::paint(p, opt, idx);
      if (support::isWebcore()) return;   // the skin marks a picked row with its navy fill instead
      if (!(opt.state & QStyle::State_Selected) && !idx.data(SELECTED_ROLE).toBool()) return;
      p->save();
      p->setRenderHint(QPainter::Antialiasing, false);
      p->setPen(QPen(opt.palette.color(QPalette::Highlight), 2));
      const QRect r = opt.rect.adjusted(0, 1, 0, -1);
      p->drawLine(r.topLeft(), r.topRight());
      p->drawLine(r.bottomLeft(), r.bottomRight());
      if (idx.column() == 0) p->drawLine(r.topLeft(), r.bottomLeft());
      if (idx.column() == this->lastColumn) p->drawLine(r.topRight(), r.bottomRight());
      p->restore();
    }

    // A cell widget (the row's bin) takes the whole cell: the base insets it by the QSS
    // ::item padding, which clipped the bin and its hover ring. Real editors keep that inset.
    void updateEditorGeometry(QWidget* w, const QStyleOptionViewItem& opt,
                              const QModelIndex& idx) const override {
      const auto* view = qobject_cast<const QAbstractItemView*>(opt.widget);
      if (view && view->indexWidget(idx) == w) w->setGeometry(opt.rect);
      else QStyledItemDelegate::updateEditorGeometry(w, opt, idx);
    }

   private:
    int lastColumn;
  };

}  // namespace stencil::gui
