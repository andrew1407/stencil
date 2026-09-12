#pragma once
// The browser's .oi-tab source-tab strip (components.css): flat tabs on a hairline —
// 13px medium labels beside a 15px glyph, 8px/16px padding, muted at rest, accent (text
// AND glyph) when selected with a 2px accent underline. Hover matches the browser's
// generic button:hover: an accent-2 pill fades in UNDER the tab and the ink flips to the
// on-accent white (accent text over the accent pill is unreadable). That fade and the
// underline SLIDING between tabs are the two moves QSS cannot express on a QTabBar.
// Everything is painted; no box ever changes, so the dialog cannot reflow. Colours come
// from the palette buildQPalette() installs, so the strip tracks theme and accent free.
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

    explicit UnderlineTabBar(QWidget* parent = nullptr);

    void setTabGlyph(int i, const QString& name);

   protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }

    void showEvent(QShowEvent* e) override;

    void resizeEvent(QResizeEvent* e) override;

    bool event(QEvent* e) override;

    void paintEvent(QPaintEvent*) override;

   private:
    QFont tabFont() const;

    static QColor mix(const QColor& a, const QColor& b, double u);

    QRectF underlineRect(int i) const;

    void setHovered(int idx);

    void animateHover(int i, double to);

    QHash<int, QString> glyphs_;
    QHash<int, double> hoverVal_;
    QHash<int, QVariantAnimation*> hoverAnims_;
    QVariantAnimation* slide_ = nullptr;
    ShimmerOverlay* sweep_ = nullptr;   // the hovered tab's glass sweep
    QRectF underline_;
    int hoverIdx_ = -1;
  };

}  // namespace stencil::gui
