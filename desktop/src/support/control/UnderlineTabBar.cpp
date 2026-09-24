#include "UnderlineTabBar.hpp"
#include "../skinPrefs.hpp"

namespace stencil::gui {

  UnderlineTabBar::UnderlineTabBar(QWidget* parent) : QTabBar(parent) {
    setDrawBase(false);
    setExpanding(false);
    setElideMode(Qt::ElideNone);
    setUsesScrollButtons(false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    // Browser tabs are <button>s and wear the shared shimmer; here the sweep runs per
    // hovered TAB over the strip's own overlay (external-band mode).
    sweep = new ShimmerOverlay(this, nullptr, /*externalBands=*/true);
    slide = new QVariantAnimation(this);
    slide->setDuration(SLIDE_MS);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(slide, &QVariantAnimation::valueChanged, this,
                     [this](const QVariant& v) {
                       underline = v.toRectF();
                       update();
                     });
    QObject::connect(this, &QTabBar::currentChanged, this, [this](int idx) {
      const QRectF to = underlineRect(idx);
      // First selection, a hidden bar, reduced motion: land.
      if (underline.isNull() || !isVisible() || support::motionReduced()) {
        slide->stop();
        underline = to;
        update();
        return;
      }
      slide->stop();
      slide->setStartValue(underline);
      slide->setEndValue(to);
      slide->start();
    });
  }

  // Named, so every state repaints it in ITS colour (browser currentColor tinting).
  void UnderlineTabBar::setTabGlyph(int i, const QString& name) {
    glyphs.insert(i, name);
    update();
  }

  QSize UnderlineTabBar::tabSizeHint(int index) const {
    const QFontMetrics fm(tabFont());
    int w = PAD_X * 2 + fm.horizontalAdvance(tabText(index));
    if (!glyphs.value(index).isEmpty()) w += GLYPH + GAP;
    const int h = PAD_Y * 2 + std::max(fm.height(), int(GLYPH)) + UNDERLINE;
    return QSize(w, h);
  }

  void UnderlineTabBar::showEvent(QShowEvent* e) {
    QTabBar::showEvent(e);
    if (slide->state() != QAbstractAnimation::Running)
      underline = underlineRect(currentIndex());
  }

  void UnderlineTabBar::resizeEvent(QResizeEvent* e) {
    QTabBar::resizeEvent(e);
    if (slide->state() != QAbstractAnimation::Running)
      underline = underlineRect(currentIndex());
  }

  bool UnderlineTabBar::event(QEvent* e) {
    switch (e->type()) {
      case QEvent::HoverEnter:
      case QEvent::HoverMove:
        setHovered(tabAt(static_cast<QHoverEvent*>(e)->position().toPoint()));
        break;
      case QEvent::HoverLeave:
        setHovered(-1);
        break;
      default:
        break;
    }
    return QTabBar::event(e);
  }

  // Browser webcore .oi-tab: raised, open at the bottom, the picked one lighter and 2px taller.
  void UnderlineTabBar::paintSkinTabs(QPainter& p) {
    const support::SkinBevel b = support::skinBevel();
    QFont f = tabFont();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    for (int i = 0; i < count(); ++i) {
      const bool on = i == currentIndex();
      const QRect r = tabRect(i).adjusted(0, on ? 0 : 2, -2, 0);
      p.fillRect(r, on ? b.light : b.face);
      p.fillRect(QRect(r.left(), r.top(), r.width(), 2), b.hilight);
      p.fillRect(QRect(r.left(), r.top(), 2, r.height()), b.hilight);
      p.fillRect(QRect(r.right() - 1, r.top(), 2, r.height()), b.dark);
      int x = r.x() + PAD_X;
      if (!glyphs.value(i).isEmpty()) {
        p.drawPixmap(x, r.y() + (r.height() - GLYPH) / 2, themedIcon(glyphs.value(i), b.ink, GLYPH).pixmap(GLYPH, GLYPH));
        x += GLYPH + GAP;
      }
      p.setPen(b.ink);
      p.drawText(QRect(x, r.y(), r.right() - x, r.height()), Qt::AlignLeft | Qt::AlignVCenter, tabText(i));
    }
    p.fillRect(QRect(0, height() - 1, width(), 1), b.shadow);
  }

  void UnderlineTabBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (support::isWebcore()) return paintSkinTabs(p);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor muted = palette().color(QPalette::Mid);
    const QColor accent = palette().color(QPalette::Highlight);
    // Link carries the accent-2 hover shade (buildQPalette); the ink is the on-accent white.
    const QColor accent2 = palette().color(QPalette::Link);
    p.setFont(tabFont());
    for (int i = 0; i < count(); ++i) {
      const QRect r = tabRect(i);
      const double u = hoverVal.value(i, 0.0);
      if (u > 0.001) {
        QColor pill = accent2;
        pill.setAlphaF(u);
        p.setPen(Qt::NoPen);
        p.setBrush(pill);
        p.drawRoundedRect(QRectF(r), 4, 4);
      }
      const QColor base = i == currentIndex() ? accent : muted;
      const QColor c = mix(base, QColor(Qt::white), u);
      int x = r.x() + PAD_X;
      const int contentH = r.height() - UNDERLINE;
      const QString glyph = glyphs.value(i);
      if (!glyph.isEmpty()) {
        const int gy = r.y() + (contentH - GLYPH) / 2;
        // Cross-fade the two endpoint rasters: themedIcon caches each colour forever, and
        // a hover fade minted ~one per tick.
        p.drawPixmap(x, gy, themedIcon(glyph, base, GLYPH).pixmap(GLYPH, GLYPH));
        if (u > 0.001) {
          p.setOpacity(u);
          p.drawPixmap(x, gy,
                       themedIcon(glyph, QColor(Qt::white), GLYPH).pixmap(GLYPH, GLYPH));
          p.setOpacity(1.0);
        }
        x += GLYPH + GAP;
      }
      p.setPen(c);
      p.drawText(QRect(x, r.y(), r.right() - x + 1, contentH),
                 Qt::AlignLeft | Qt::AlignVCenter, tabText(i));
    }
    // Rounded tips: the browser's underline is a border-bottom on a 4px-radius button.
    const QRectF u = slide->state() == QAbstractAnimation::Running
                         ? underline
                         : underlineRect(currentIndex());
    if (!u.isNull()) {
      p.setPen(Qt::NoPen);
      p.setBrush(accent);
      p.drawRoundedRect(u, 1, 1);
    }
  }

  QFont UnderlineTabBar::tabFont() const {
    QFont f = font();
    f.setPixelSize(13);            // browser .oi-tab: 13px, weight 500
    f.setWeight(QFont::Medium);
    return f;
  }

  QColor UnderlineTabBar::mix(const QColor& a, const QColor& b, double u) {
    const auto ch = [u](int x, int y) { return int(std::lround(x + (y - x) * u)); };
    return QColor(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()));
  }

  // Browser border-bottom: the underline spans the WHOLE tab, padding included.
  QRectF UnderlineTabBar::underlineRect(int i) const {
    if (i < 0 || i >= count()) return QRectF();
    const QRect r = tabRect(i);
    return QRectF(r.x(), height() - UNDERLINE, r.width(), UNDERLINE);
  }

  // One eased clock per tab, towards hovered (1) or rest (0).
  void UnderlineTabBar::setHovered(int idx) {
    if (idx == hoverIdx) return;
    animateHover(hoverIdx, 0.0);
    hoverIdx = idx;
    animateHover(hoverIdx, 1.0);
    if (idx >= 0) sweep->sweepBand(tabRect(idx)); else sweep->cancel();
  }

  void UnderlineTabBar::animateHover(int i, double to) {
    if (i < 0) return;
    if (support::motionReduced()) {
      hoverVal[i] = to;
      update();
      return;
    }
    QVariantAnimation*& a = hoverAnims[i];
    if (!a) {
      a = new QVariantAnimation(this);
      a->setDuration(HOVER_MS);
      a->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(a, &QVariantAnimation::valueChanged, this,
                       [this, i](const QVariant& v) {
                         hoverVal[i] = v.toDouble();
                         update();
                       });
    }
    a->stop();
    a->setStartValue(hoverVal.value(i, 0.0));
    a->setEndValue(to);
    a->start();
  }

}  // namespace stencil::gui
