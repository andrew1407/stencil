#pragma once
#include <QToolButton>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QIcon>
#include <QFontMetrics>

// The empty-state "Open Image" button (browser #load-image-btn), painting icon + label
// itself: the stock ToolButtonTextBesideIcon slot cramped the glyph. Q_OBJECT-free.
namespace stencil::gui {

  class OpenImageButton : public QToolButton {
   public:
    explicit OpenImageButton(QWidget* parent = nullptr) : QToolButton(parent) {}

    QSize sizeHint() const override {
      const QFontMetrics fm(font());
      const int iw = iconSize().width();
      const int tw = fm.horizontalAdvance(text());
      return QSize(PAD_X * 2 + iw + GAP + tw,
                   qMax(iconSize().height(), fm.height()) + PAD_Y * 2);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    void paintEvent(QPaintEvent*) override {
      QStyleOptionToolButton opt;
      initStyleOption(&opt);
      QStylePainter sp(this);
      // Label stripped so the base does not lay the icon+text out with its own spacing.
      QStyleOptionToolButton bg = opt;
      bg.text.clear();
      bg.icon = QIcon();
      bg.iconSize = QSize(0, 0);
      sp.drawComplexControl(QStyle::CC_ToolButton, bg);

      const QFontMetrics fm(font());
      const int iw = iconSize().width(), ih = iconSize().height();
      const int tw = fm.horizontalAdvance(text());
      const qreal groupW = iw + GAP + tw;
      const qreal x0 = (width() - groupW) / 2.0;   // icon + label centred as ONE group
      const qreal cy = height() / 2.0;
      const QRect iconRect(qRound(x0), qRound(cy - ih / 2.0), iw, ih);
      const QIcon::Mode mode = !isEnabled() ? QIcon::Disabled
                             : (opt.state & QStyle::State_MouseOver ? QIcon::Active : QIcon::Normal);
      icon().paint(&sp, iconRect, Qt::AlignCenter, mode, QIcon::Off);
      // Always accent-filled: `QToolButton[toolFill="accent"] { color: … }` put the ink in the palette.
      sp.setPen(opt.palette.buttonText().color());
      sp.setFont(font());
      sp.drawText(QRectF(x0 + iw + GAP, 0, tw, height()),
                  Qt::AlignLeft | Qt::AlignVCenter, text());
    }

   private:
    static constexpr int PAD_X = 11;   // breathing room each side of the group
    static constexpr int PAD_Y = 5;
    static constexpr int GAP = 11;    // icon → label (stock reserve was ~4px; +7, as asked)
  };

}  // namespace stencil::gui
