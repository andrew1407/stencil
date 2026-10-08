#pragma once
// The Lines tab's own parts, private to the SelectionPanel*.cpp TUs: the colour chips, the header
// that heads each colour-and-size pair once, and the spin box a size is edited in (browser
// ui/panel/lines/list.js, lines/numEdit.js and the .lines-table colgroup).
#include "selectionPanelParts.hpp"
#include "../../support/control/lineLimits.hpp"
#include "../../support/control/numericInput.hpp"
#include "../../support/theme/themeTokens.hpp"
#include <QAbstractButton>
#include <QHeaderView>
#include <QPainter>
#include <functional>
#include <utility>

namespace stencil::gui {

  inline constexpr int LINE_CHIP_PX = 14;

  // A row's colour, painted: square for the line, round for its points, a clear face a hollow ring;
  // no rim of its own takes the theme's border. A button, so wireColorChip can time its clicks.
  class LineChip : public QAbstractButton {
   public:
    LineChip(bool round, QWidget* parent) : QAbstractButton(parent), round(round) {
      setFixedSize(LINE_CHIP_PX, LINE_CHIP_PX);
      setCursor(Qt::PointingHandCursor);
      setFocusPolicy(Qt::NoFocus);
    }
    void setColors(const QColor& face, const QColor& rim) {
      this->face = face;
      this->rim = rim;
      update();
    }
    QColor faceColor() const { return face; }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      const bool square = support::isWebcore();   // the skin rounds nothing (browser webcore/chrome.css)
      p.setRenderHint(QPainter::Antialiasing, !square);
      const bool dark = palette().color(QPalette::Window).lightness() < 128;
      p.setPen(QPen(rim.isValid() ? rim : themeToken("--border-main", dark), 1));
      p.setBrush(face);
      const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
      if (round) p.drawEllipse(box);
      else p.drawRoundedRect(box, square ? 0 : 3, square ? 0 : 3);
    }

   private:
    bool round;
    QColor face;
    QColor rim;
  };

  // "Line" over its colour and thickness, "Point" over theirs: each half paints its slice of the
  // pair's one section, so a repaint of either half matches (browser colspan="2").
  class PairedHeader : public QHeaderView {
   public:
    explicit PairedHeader(QWidget* parent) : QHeaderView(Qt::Horizontal, parent) {}

   protected:
    void paintSection(QPainter* p, const QRect& rect, int logical) const override {
      const int first = logical == LCOL_COLOR || logical == LCOL_THICK ? LCOL_COLOR
                        : logical == LCOL_POINT || logical == LCOL_SIZE ? LCOL_POINT : -1;
      if (first < 0) { QHeaderView::paintSection(p, rect, logical); return; }
      const QRect pair(sectionViewportPosition(first), rect.y(),
                       sectionSize(first) + sectionSize(first + 1), rect.height());
      p->save();
      p->setClipRect(rect);
      QHeaderView::paintSection(p, pair, first);
      p->restore();
    }
  };

  // The row outline, and a thickness or point size edited in a bare spin box held to LIMITS; none
  // opens in a read-only view.
  class LineCellDelegate : public PointRowDelegate {
   public:
    LineCellDelegate(QObject* parent, std::function<bool()> readOnly)
        : PointRowDelegate(parent, LCOL_COUNT - 1), readOnly(std::move(readOnly)) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& idx) const override {
      const bool thick = idx.column() == LCOL_THICK;
      if ((!thick && idx.column() != LCOL_SIZE) || (readOnly && readOnly())) return nullptr;
      const support::lineLimits::Table& limits = support::lineLimits::table();
      auto* spin = new ExprSpinBox(parent);
      spin->setRange(thick ? limits.thickMin : limits.pointMin, thick ? limits.thickMax : limits.pointMax);
      spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
      spin->setFrame(false);
      return spin;
    }

   private:
    std::function<bool()> readOnly;
  };

}  // namespace stencil::gui
