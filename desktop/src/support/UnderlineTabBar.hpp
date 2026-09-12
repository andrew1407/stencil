#pragma once
// The browser's .oi-tab strip (components.css). Everything is painted — the hover pill
// fade and the underline SLIDE are moves QSS cannot express on a QTabBar. Q_OBJECT-free.
#include "iconSet.hpp"
#include "modalReveal.hpp"   // support::motionReduced()
#include "ShimmerOverlay.hpp"   // the app-wide hover sweep, per tab

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
    // Browser .oi-tab: padding 8px 16px, 14px glyphs (15 at Qt's optical size), 6px gap, 2px underline.
    static constexpr int PAD_X = 16;
    static constexpr int PAD_Y = 8;
    static constexpr int GLYPH = 15;
    static constexpr int GAP = 6;
    static constexpr int UNDERLINE = 2;
    static constexpr int HOVER_MS = 150;
    static constexpr int SLIDE_MS = 220;

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
