#pragma once
#include <QLayout>
#include <QStyle>
#include <QWidget>

// Left-to-right layout that wraps onto a new line when it runs out of width, and reports
// a height that grows with however many lines that takes (heightForWidth) — Qt has no
// built-in flex-wrap equivalent, so a QHBoxLayout row either overflows or clips. Standard
// Qt Widgets "Flow Layout" example algorithm, ported in for the "Selected Line:" bar
// (browser parity: .selection-panel-inner is a flex row that wraps).
namespace stencil::gui {

  class FlowLayout : public QLayout {
   public:
    explicit FlowLayout(QWidget* parent, int margin = 0, int hSpacing = 8, int vSpacing = 6)
        : QLayout(parent), hSpace_(hSpacing), vSpace_(vSpacing) {
      setContentsMargins(margin, margin, margin, margin);
    }
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
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect& rect) override {
      QLayout::setGeometry(rect);
      doLayout(rect, false);
    }

   private:
    // Places every item left-to-right, wrapping to a new row when the next one would run
    // past the right edge. Each row is buffered and flushed once its height is known, then
    // vertically centered within it (browser parity: align-items: center).
    int doLayout(const QRect& rect, bool testOnly) const {
      const QMargins m = contentsMargins();
      const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
      int x = area.x(), y = area.y(), lineHeight = 0, totalHeight = 0;
      QList<QLayoutItem*> rowItems;
      QList<int> rowX;
      bool firstRow = true;

      auto flushRow = [&] {
        if (!firstRow) { y += vSpace_; totalHeight += vSpace_; }
        if (!testOnly) {
          for (int i = 0; i < rowItems.size(); ++i) {
            const QSize sz = rowItems[i]->sizeHint();
            rowItems[i]->setGeometry(QRect(rowX[i], y + (lineHeight - sz.height()) / 2, sz.width(), sz.height()));
          }
        }
        y += lineHeight;
        totalHeight += lineHeight;
        firstRow = false;
        rowItems.clear();
        rowX.clear();
      };

      for (QLayoutItem* item : items_) {
        if (item->isEmpty()) continue;  // hidden widgets (e.g. fillGroup_) take no space
        const QSize sz = item->sizeHint();
        int nextX = x + sz.width() + hSpace_;
        if (nextX - hSpace_ > area.right() && !rowItems.isEmpty()) {
          flushRow();
          x = area.x();
          nextX = x + sz.width() + hSpace_;
          lineHeight = 0;
        }
        rowItems.append(item);
        rowX.append(x);
        lineHeight = qMax(lineHeight, sz.height());
        x = nextX;
      }
      if (!rowItems.isEmpty()) flushRow();

      return totalHeight + m.top() + m.bottom();
    }

    QList<QLayoutItem*> items_;
    int hSpace_, vSpace_;
  };

}  // namespace stencil::gui
