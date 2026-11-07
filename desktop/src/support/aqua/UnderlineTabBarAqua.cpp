// UnderlineTabBar under the aqua skin: a glass pill per tab, the open one blue gel, in the skin's
// own capsule stops (gradient.hpp). Browser twin: css/aqua/menus.css .oi-tab.
#include "../control/UnderlineTabBar.hpp"
#include "../skinPrefs.hpp"
#include "theme.hpp"
#include "iconSet.hpp"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

namespace stencil::gui {

  void UnderlineTabBar::paintAquaTabs(QPainter& p) {
    const stencil::gui::Palette pal = themePalette(support::skinDark());
    const support::SkinGlass& glass = support::skinGlass();
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f = tabFont();
    f.setWeight(QFont::Medium);   // browser .oi-tab: 500, the glyph before the word
    p.setFont(f);
    const QFontMetrics fm(f);
    for (int i = 0; i < count(); ++i) {
      const bool on = i == currentIndex();
      const QRectF r = QRectF(tabRect(i)).adjusted(1.5, 3.5, -1.5, -3.5);
      QLinearGradient g(r.topLeft(), r.bottomLeft());
      g.setStops(on ? glass.gelStops : glass.glassStops);
      p.setPen(QPen(on ? glass.gelEdge : glass.edge, 1));
      p.setBrush(g);
      p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
      const QColor ink = on ? glass.onGel : pal.textMain;
      const QString glyph = glyphs.value(i);
      const int lead = glyph.isEmpty() ? 0 : GLYPH + GAP;
      const qreal x = r.center().x() - (lead + fm.horizontalAdvance(tabText(i))) / 2.0;
      if (!glyph.isEmpty())
        p.drawPixmap(QPointF(x, r.center().y() - GLYPH / 2.0), themedIcon(glyph, ink, GLYPH).pixmap(GLYPH, GLYPH));
      p.setPen(ink);
      p.drawText(QRectF(x + lead, r.top(), r.right() - x - lead, r.height()), Qt::AlignLeft | Qt::AlignVCenter, tabText(i));
    }
  }

}  // namespace stencil::gui
