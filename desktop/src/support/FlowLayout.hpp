#pragma once
#include <QLayout>
#include <QStyle>
#include <QWidget>

// The Qt Widgets "Flow Layout" example, ported for the "Selected Line:" bar (browser
// .selection-panel-inner wraps). An item wider than the row takes the row's width.
namespace stencil::gui {

  class FlowLayout : public QLayout {
   public:
    explicit FlowLayout(QWidget* parent, int margin = 0, int hSpacing = 8, int vSpacing = 6)
        : QLayout(parent), hSpace_(hSpacing), vSpace_(vSpacing) {
      setContentsMargins(margin, margin, margin, margin);
    }
    // sizeHint = the whole row on ONE line: what a controlReveal slot slides open to.
    void setLineSizeHint(bool on) { lineHint_ = on; }
    // One line while the owner's maximumWidth is capped, so a controlReveal slot wipes
    // edge-on instead of re-flowing into a column (browser .reveal-group-transition nowrap).
    void setHoldsLineWhileCapped(bool on) { holdWhileCapped_ = on; }
    ~FlowLayout() override {
      QLayoutItem* item;
      while ((item = takeAt(0))) delete item;
    }

    void addItem(QLayoutItem* item) override { items_.append(item); }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int index) const override { return items_.value(index); }
    QLayoutItem* takeAt(int index) override {
      return (index >= 0 && index < items_.size()) ? items_.takeAt(index) : nullptr;
    }
    QSize minimumSize() const override {
      QSize size;
      for (QLayoutItem* item : items_) size = size.expandedTo(item->minimumSize());
      const QMargins m = contentsMargins();
      return size + QSize(m.left() + m.right(), m.top() + m.bottom());
    }
    QSize sizeHint() const override {
      if (!lineHint_) return minimumSize();
      QSize size(0, 0);   // NOT QSize(): that is (-1,-1), and the width sum starts short
      int n = 0;
      for (QLayoutItem* item : items_) {
        if (item->isEmpty()) continue;
        const QSize sz = item->sizeHint();
        size.setWidth(size.width() + sz.width());
        size.setHeight(qMax(size.height(), sz.height()));
        ++n;
      }
      size.rwidth() += hSpace_ * qMax(0, n - 1);
      const QMargins m = contentsMargins();
      return size + QSize(m.left() + m.right(), m.top() + m.bottom());
    }
    void setGeometry(const QRect& rect) override {
      QLayout::setGeometry(rect);
      doLayout(rect, false);
    }

   private:
    // Rows are buffered and flushed once their height is known, then vertically centred.
    int doLayout(const QRect& rect, bool testOnly) const {
      const QMargins m = contentsMargins();
      const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
      const QWidget* owner = parentWidget();
      const bool wrap = !(holdWhileCapped_ && owner && owner->maximumWidth() < QWIDGETSIZE_MAX);
      int x = area.x(), y = area.y(), lineHeight = 0, totalHeight = 0;
      QList<QLayoutItem*> rowItems;
      QList<int> rowX;
      QList<QSize> rowSize;
      bool firstRow = true;

      auto flushRow = [&] {
        if (!firstRow) { y += vSpace_; totalHeight += vSpace_; }
        if (!testOnly) {
          for (int i = 0; i < rowItems.size(); ++i) {
            const QSize sz = rowSize[i];
            // Vertical expand takes the whole line — the browser's `align-self: stretch` (.ctrl-sep).
            const QWidget* w = rowItems[i]->widget();
            const bool stretch =
                w && (w->sizePolicy().verticalPolicy() & QSizePolicy::ExpandFlag) != 0;
            const int h = stretch ? lineHeight : sz.height();
            rowItems[i]->setGeometry(QRect(rowX[i], y + (lineHeight - h) / 2, sz.width(), h));
          }
        }
        y += lineHeight;
        totalHeight += lineHeight;
        firstRow = false;
        rowItems.clear();
        rowX.clear();
        rowSize.clear();
      };

      for (QLayoutItem* item : items_) {
        if (item->isEmpty()) continue;  // hidden widgets (e.g. fillGroup_) take no space
        QSize sz = item->sizeHint();
        if (wrap && sz.width() > area.width()) {   // wider than the row: take the row
          sz.setWidth(qMax(area.width(), item->minimumSize().width()));
          if (item->hasHeightForWidth()) sz.setHeight(item->heightForWidth(sz.width()));
        }
        int nextX = x + sz.width() + hSpace_;
        // An item occupying [x, x + w - 1] FITS while its last pixel is on area.right();
        // `x + w > right()` wrapped a row that fitted exactly, one pixel short of sizeHint().
        if (wrap && nextX - hSpace_ - 1 > area.right() && !rowItems.isEmpty()) {
          flushRow();
          x = area.x();
          nextX = x + sz.width() + hSpace_;
          lineHeight = 0;
        }
        rowItems.append(item);
        rowX.append(x);
        rowSize.append(sz);
        lineHeight = qMax(lineHeight, sz.height());
        x = nextX;
      }
      if (!rowItems.isEmpty()) flushRow();

      return totalHeight + m.top() + m.bottom();
    }

    QList<QLayoutItem*> items_;
    int hSpace_, vSpace_;
    bool lineHint_ = false;
    bool holdWhileCapped_ = false;
  };

}  // namespace stencil::gui
