#include "underlineTabBar.hpp"

namespace stencil::gui {

  UnderlineTabBar::UnderlineTabBar(QWidget* parent) : QTabBar(parent) {
    setDrawBase(false);
    setExpanding(false);
    setElideMode(Qt::ElideNone);
    setUsesScrollButtons(false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    // The browser's tabs are <button>s, so they wear the shared glass shimmer on
    // hover like every other control (css/layout.css ui-shimmer); here the sweep is
    // driven per hovered TAB over the strip's own overlay (external-band mode).
    sweep_ = new ShimmerOverlay(this, nullptr, /*externalBands=*/true);
    slide_ = new QVariantAnimation(this);
    slide_->setDuration(kSlideMs);
    slide_->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(slide_, &QVariantAnimation::valueChanged, this,
                     [this](const QVariant& v) {
                       underline_ = v.toRectF();
                       update();
                     });
    QObject::connect(this, &QTabBar::currentChanged, this, [this](int idx) {
      const QRectF to = underlineRect(idx);
      // First selection (nothing to slide from), a hidden bar, reduced motion: land.
      if (underline_.isNull() || !isVisible() || support::motionReduced()) {
        slide_->stop();
        underline_ = to;
        update();
        return;
      }
      slide_->stop();
      slide_->setStartValue(underline_);
      slide_->setEndValue(to);
      slide_->start();
    });
  }

  // The iconSet glyph drawn before tab `i`'s text — named, so every state repaints
  // it in ITS colour (the browser tints icon and label together via currentColor).
  void UnderlineTabBar::setTabGlyph(int i, const QString& name) {
    glyphs_.insert(i, name);
    update();
  }

  QSize UnderlineTabBar::tabSizeHint(int index) const {
    const QFontMetrics fm(tabFont());
    int w = kPadX * 2 + fm.horizontalAdvance(tabText(index));
    if (!glyphs_.value(index).isEmpty()) w += kGlyph + kGap;
    const int h = kPadY * 2 + std::max(fm.height(), int(kGlyph)) + kUnderline;
    return QSize(w, h);
  }

  void UnderlineTabBar::showEvent(QShowEvent* e) {
    QTabBar::showEvent(e);
    if (slide_->state() != QAbstractAnimation::Running)
      underline_ = underlineRect(currentIndex());
  }

  void UnderlineTabBar::resizeEvent(QResizeEvent* e) {
    QTabBar::resizeEvent(e);
    if (slide_->state() != QAbstractAnimation::Running)
      underline_ = underlineRect(currentIndex());
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

  void UnderlineTabBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor muted = palette().color(QPalette::Mid);
    const QColor accent = palette().color(QPalette::Highlight);
    // Link carries the accent-2 hover shade (buildQPalette), the browser's
    // button:hover fill; the ink over it is the on-accent white every filled
    // button uses.
    const QColor accent2 = palette().color(QPalette::Link);
    p.setFont(tabFont());
    for (int i = 0; i < count(); ++i) {
      const QRect r = tabRect(i);
      const double u = hoverVal_.value(i, 0.0);
      if (u > 0.001) {
        QColor pill = accent2;
        pill.setAlphaF(u);
        p.setPen(Qt::NoPen);
        p.setBrush(pill);
        p.drawRoundedRect(QRectF(r), 4, 4);
      }
      const QColor base = i == currentIndex() ? accent : muted;
      const QColor c = mix(base, QColor(Qt::white), u);
      int x = r.x() + kPadX;
      const int contentH = r.height() - kUnderline;
      const QString glyph = glyphs_.value(i);
      if (!glyph.isEmpty()) {
        const int gy = r.y() + (contentH - kGlyph) / 2;
        // Cross-fade the two endpoint rasters under painter opacity instead of
        // rasterising a fresh intermediate shade every frame — themedIcon caches
        // each unique colour forever, and a hover fade minted ~one per tick.
        p.drawPixmap(x, gy, themedIcon(glyph, base, kGlyph).pixmap(kGlyph, kGlyph));
        if (u > 0.001) {
          p.setOpacity(u);
          p.drawPixmap(x, gy,
                       themedIcon(glyph, QColor(Qt::white), kGlyph).pixmap(kGlyph, kGlyph));
          p.setOpacity(1.0);
        }
        x += kGlyph + kGap;
      }
      p.setPen(c);
      p.drawText(QRect(x, r.y(), r.right() - x + 1, contentH),
                 Qt::AlignLeft | Qt::AlignVCenter, tabText(i));
    }
    // The accent underline, wherever its slide has it right now. Rounded tips —
    // the browser's underline is a border-bottom on a 4px-radius button, so its
    // ends curve rather than cut off square.
    const QRectF u = slide_->state() == QAbstractAnimation::Running
                         ? underline_
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
    return QRectF(r.x(), height() - kUnderline, r.width(), kUnderline);
  }

  // One eased clock per tab, each pulling its colour towards hovered (1) or rest (0);
  // moving between tabs fades the old one out while the new fades in.
  void UnderlineTabBar::setHovered(int idx) {
    if (idx == hoverIdx_) return;
    animateHover(hoverIdx_, 0.0);
    hoverIdx_ = idx;
    animateHover(hoverIdx_, 1.0);
    if (idx >= 0) sweep_->sweepBand(tabRect(idx)); else sweep_->cancel();
  }

  void UnderlineTabBar::animateHover(int i, double to) {
    if (i < 0) return;
    if (support::motionReduced()) {
      hoverVal_[i] = to;
      update();
      return;
    }
    QVariantAnimation*& a = hoverAnims_[i];
    if (!a) {
      a = new QVariantAnimation(this);
      a->setDuration(kHoverMs);
      a->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(a, &QVariantAnimation::valueChanged, this,
                       [this, i](const QVariant& v) {
                         hoverVal_[i] = v.toDouble();
                         update();
                       });
    }
    a->stop();
    a->setStartValue(hoverVal_.value(i, 0.0));
    a->setEndValue(to);
    a->start();
  }

}  // namespace stencil::gui
