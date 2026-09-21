#pragma once
#include <QToolButton>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QColor>
#include <QFontMetrics>
#include <QPalette>

// The "Controls" show/hide pill. A stock icon+text QToolButton reserves ~36px for the icon
// slot however small the chevron, leaving a wide gap beside the label. This
// paints the chevron and the label itself, as one centred group, and sizes to just that.
// The base still draws the QSS background / border / hover; its own text stays empty.
// Q_OBJECT-free (virtual overrides only), so no MOC.
namespace stencil::gui {

  class ControlsPill : public QToolButton {
   public:
    explicit ControlsPill(QWidget* parent = nullptr) : QToolButton(parent) {
      setToolButtonStyle(Qt::ToolButtonTextOnly);
    }

    // The visible label ("Controls"). The button's own text stays empty so the base does
    // not draw it under ours; accessibleName carries it for a11y.
    void setLabel(const QString& t) { label = t; setAccessibleName(t); updateGeometry(); update(); }
    // `deg` 0 = chevron-up (rows shown), 180 = down; `ink` the label + chevron colour;
    // `px` the chevron glyph size (PILL_CHEVRON).
    void setChevron(qreal deg, const QColor& ink, int px) {
      chevronDeg = deg; this->ink = ink; chevronPx = px; update();
    }

    QSize sizeHint() const override {
      const QFontMetrics fm(font());
      return QSize(PAD_X * 2 + chevronPx + GAP + fm.horizontalAdvance(label),
                   fm.height() + PAD_Y * 2);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    void paintEvent(QPaintEvent* e) override {
      QToolButton::paintEvent(e);   // QSS background / border / hover (button text is empty)
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      const QColor ink = this->ink.isValid() ? this->ink : palette().color(QPalette::ButtonText);
      const QFontMetrics fm(font());
      const int tw = fm.horizontalAdvance(label);
      const qreal groupW = chevronPx + GAP + tw;
      const qreal x0 = (width() - groupW) / 2.0;   // chevron + label centred as ONE group
      const qreal cy = height() / 2.0;
      if (chevronPx > 0) {
        p.save();
        p.translate(x0 + chevronPx / 2.0, cy);
        p.rotate(chevronDeg);
        const qreal w = chevronPx, h = chevronPx;
        p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF() << QPointF(-w * 0.42, h * 0.20)
                                   << QPointF(0.0, -h * 0.20)
                                   << QPointF(w * 0.42, h * 0.20));
        p.restore();
      }
      p.setPen(ink);
      p.setFont(font());
      p.drawText(QRectF(x0 + chevronPx + GAP, 0, tw, height()),
                 Qt::AlignLeft | Qt::AlignVCenter, label);
    }

   private:
    static constexpr int PAD_X = 12;   // horizontal breathing room each side of the group
    static constexpr int PAD_Y = 3;
    static constexpr int GAP = 7;     // chevron → label
    QString label;
    qreal chevronDeg = 0.0;
    QColor ink;
    int chevronPx = 0;
  };

}  // namespace stencil::gui
