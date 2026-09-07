#pragma once
// The browser's .oi-tab source-tab strip (components.css): flat tabs on a hairline —
// 13px medium labels beside a 15px glyph, 8px/16px padding, muted at rest, accent
// (text AND glyph) when selected with a 2px accent underline. Hover matches the
// browser's generic button:hover: an accent-2 pill fades in UNDER the tab and the
// ink flips to the app-wide on-accent white (accent text over the accent pill was
// unreadable — user report) — plus the two moves QSS cannot express on a QTabBar:
// that hover fade, and the underline SLIDING from the old tab to the new one.
// Everything is painted; no box ever changes, so the dialog cannot reflow.
//
// Colours come from the widget palette buildQPalette() installs (WindowText = text,
// Mid = muted, Highlight = accent), so the strip tracks theme and accent flips free.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include "iconSet.hpp"
#include "modalReveal.hpp"   // support::motionReduced()
#include "shimmerOverlay.hpp"   // the app-wide hover sweep, per tab

#include <QEasingCurve>
#include <QHash>
#include <QHoverEvent>
#include <QPainter>
#include <QTabBar>
#include <QVariantAnimation>

#include <algorithm>

namespace stencil::gui {

  class UnderlineTabBar : public QTabBar {
   public:
    // Browser .oi-tab: padding 8px 16px, 14px glyphs (15 at Qt's optical size), 6px
    // icon-text gap, a 2px underline; the colour fade and the underline slide are this
    // widget's own clocks (QSS colours cannot animate).
    static constexpr int kPadX = 16;
    static constexpr int kPadY = 8;
    static constexpr int kGlyph = 15;
    static constexpr int kGap = 6;
    static constexpr int kUnderline = 2;
    static constexpr int kHoverMs = 150;
    static constexpr int kSlideMs = 220;

    explicit UnderlineTabBar(QWidget* parent = nullptr) : QTabBar(parent) {
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
    void setTabGlyph(int i, const QString& name) {
      glyphs_.insert(i, name);
      update();
    }

   protected:
    QSize tabSizeHint(int index) const override {
      const QFontMetrics fm(tabFont());
      int w = kPadX * 2 + fm.horizontalAdvance(tabText(index));
      if (!glyphs_.value(index).isEmpty()) w += kGlyph + kGap;
      const int h = kPadY * 2 + std::max(fm.height(), int(kGlyph)) + kUnderline;
      return QSize(w, h);
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }

    void showEvent(QShowEvent* e) override {
      QTabBar::showEvent(e);
      if (slide_->state() != QAbstractAnimation::Running)
        underline_ = underlineRect(currentIndex());
    }

    void resizeEvent(QResizeEvent* e) override {
      QTabBar::resizeEvent(e);
      if (slide_->state() != QAbstractAnimation::Running)
        underline_ = underlineRect(currentIndex());
    }

    bool event(QEvent* e) override {
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

    void paintEvent(QPaintEvent*) override {
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

   private:
    QFont tabFont() const {
      QFont f = font();
      f.setPixelSize(13);            // browser .oi-tab: 13px, weight 500
      f.setWeight(QFont::Medium);
      return f;
    }

    static QColor mix(const QColor& a, const QColor& b, double u) {
      const auto ch = [u](int x, int y) { return int(std::lround(x + (y - x) * u)); };
      return QColor(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()));
    }

    // Browser border-bottom: the underline spans the WHOLE tab, padding included.
    QRectF underlineRect(int i) const {
      if (i < 0 || i >= count()) return QRectF();
      const QRect r = tabRect(i);
      return QRectF(r.x(), height() - kUnderline, r.width(), kUnderline);
    }

    // One eased clock per tab, each pulling its colour towards hovered (1) or rest (0);
    // moving between tabs fades the old one out while the new fades in.
    void setHovered(int idx) {
      if (idx == hoverIdx_) return;
      animateHover(hoverIdx_, 0.0);
      hoverIdx_ = idx;
      animateHover(hoverIdx_, 1.0);
      if (idx >= 0) sweep_->sweepBand(tabRect(idx)); else sweep_->cancel();
    }

    void animateHover(int i, double to) {
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

    QHash<int, QString> glyphs_;
    QHash<int, double> hoverVal_;
    QHash<int, QVariantAnimation*> hoverAnims_;
    QVariantAnimation* slide_ = nullptr;
    ShimmerOverlay* sweep_ = nullptr;   // the hovered tab's glass sweep
    QRectF underline_;
    int hoverIdx_ = -1;
  };

}  // namespace stencil::gui
