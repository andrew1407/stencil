#pragma once
// The Lines tab's own parts, private to the SelectionPanel*.cpp TUs: the colour chips, the eye, the
// header that heads each colour-and-size pair once, and the editors a size and a name are typed in
// (browser ui/panel/lines/list.js, lines/numEdit.js, lines/nameEdit.js and the .lines-table colgroup).
#include "selectionPanelParts.hpp"
#include "iconSet.hpp"
#include "lineName.hpp"
#include "../../support/control/lineLimits.hpp"
#include "../../support/control/numericInput.hpp"
#include "../../support/motionPrefs.hpp"
#include "../../support/theme/themeTokens.hpp"
#include <QAbstractButton>
#include <QHeaderView>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVariantAnimation>
#include <cmath>
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

  // A row's eye (browser .lines-eye): the eye glyph, muted while its line shows and in the accent,
  // crossed by a slash, while it hides. `flip` plays the slash in (hiding) or out (showing) once.
  class LineEyeButton : public QPushButton {
   public:
    static constexpr int FLIP_MS = 260;   // browser click.css linesEyeSlash

    LineEyeButton(bool hidden, bool flip, QWidget* parent)
        : QPushButton(parent), hidden(hidden), slash(hidden ? 1.0 : 0.0) {
      setObjectName(QStringLiteral("linesEyeBtn"));
      setFlat(true);
      setCursor(Qt::PointingHandCursor);
      setToolTip(hidden ? QStringLiteral("Show line") : QStringLiteral("Hide line"));
      restyle();
      if (!flip || support::motionReduced()) return;
      auto* anim = new QVariantAnimation(this);
      anim->setDuration(FLIP_MS);
      anim->setStartValue(1.0 - slash);
      anim->setEndValue(slash);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        slash = v.toDouble();
        update();
      });
      slash = 1.0 - slash;
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    bool lineHidden() const { return hidden; }
    void restyle() { setIcon(themedIcon("eye", ink(), 14)); }

   protected:
    void paintEvent(QPaintEvent* e) override {
      QPushButton::paintEvent(e);
      if (slash <= 0.0) return;
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(QPen(ink(), 1.5, Qt::SolidLine, Qt::RoundCap));
      // 17px long at full length, bottom-left to top-right, grown from the middle (rotate(-45deg)).
      const QPointF c = QRectF(rect()).center();
      const double d = 8.5 * slash / std::sqrt(2.0);
      p.drawLine(c + QPointF(-d, d), c + QPointF(d, -d));
    }

   private:
    QColor ink() const { return palette().color(hidden ? QPalette::Highlight : QPalette::PlaceholderText); }

    bool hidden;
    double slash;
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

  // The row outline, a thickness or point size edited in a bare spin box held to LIMITS, and a name
  // in a bare line edit opening on the name shown, "Line N" for an unnamed line, all selected; that
  // "Line N" left as it was keeps the line unnamed. None opens in a read-only view. A typed name
  // goes to `renamed`, not into the cell: the commit re-renders the row.
  class LineCellDelegate : public PointRowDelegate {
   public:
    LineCellDelegate(QObject* parent, std::function<bool()> readOnly, std::function<void(int, QString)> renamed)
        : PointRowDelegate(parent, LCOL_COUNT - 1), readOnly(std::move(readOnly)), renamed(std::move(renamed)) {}

    void setEditorData(QWidget* editor, const QModelIndex& idx) const override {
      if (auto* edit = qobject_cast<QLineEdit*>(editor); edit && idx.column() == LCOL_NAME) {
        const QString name = idx.data(NAME_ROLE).toString();
        edit->setText(name.isEmpty() ? standIn(idx.row()) : name);
        edit->selectAll();
        return;
      }
      PointRowDelegate::setEditorData(editor, idx);
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& idx) const override {
      if (auto* edit = qobject_cast<QLineEdit*>(editor); edit && idx.column() == LCOL_NAME) {
        const bool kept = idx.data(NAME_ROLE).toString().isEmpty() && edit->text().trimmed() == standIn(idx.row());
        if (renamed) renamed(idx.row(), kept ? QString() : edit->text());
        return;
      }
      PointRowDelegate::setModelData(editor, model, idx);
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& idx) const override {
      if (readOnly && readOnly()) return nullptr;
      if (idx.column() == LCOL_NAME) {
        auto* edit = new QLineEdit(parent);
        edit->setObjectName(QStringLiteral("linesNameEdit"));
        edit->setFrame(false);
        edit->setMaxLength(model::lineNameMax());
        edit->setPlaceholderText(standIn(idx.row()));
        return edit;
      }
      const bool thick = idx.column() == LCOL_THICK;
      if (!thick && idx.column() != LCOL_SIZE) return nullptr;
      const support::lineLimits::Table& limits = support::lineLimits::table();
      auto* spin = new ExprSpinBox(parent);
      spin->setRange(thick ? limits.thickMin : limits.pointMin, thick ? limits.thickMax : limits.pointMax);
      spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
      spin->setFrame(false);
      return spin;
    }

   private:
    static QString standIn(int row) { return QStringLiteral("Line %1").arg(row + 1); }
    std::function<bool()> readOnly;
    std::function<void(int, QString)> renamed;
  };

}  // namespace stencil::gui
