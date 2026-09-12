#pragma once
// One glyph per interface-motion mode, painted with QPainter from the same 16-unit
// drawings the browser's SVGs carry, for the Settings dialog's combo rows and trigger.
// The `ms` argument is how far into the browser's `.mm-*` hover keyframes the glyph is,
// evaluated by hand; the delegate below plays them on the combo's popup list.
#include <QAbstractItemView>
#include <QComboBox>
#include <QApplication>
#include <QStyle>
#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QHoverEvent>
#include <QIcon>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace stencil::support {

  // The browser's easings, by name: cubic-bezier(0.16, 1, 0.3, 1) and ease-out.
  inline double mmOut(double t) {   // (0.16, 1, 0.3, 1) — sampled by bisection, like dustKit
    double lo = 0, hi = 1, u = t;
    for (int i = 0; i < 20; i++) {
      u = 0.5 * (lo + hi);
      const double x = 3 * (1 - u) * (1 - u) * u * 0.16 + 3 * (1 - u) * u * u * 0.3 + u * u * u;
      if (x < t) lo = u; else hi = u;
    }
    return 3 * (1 - u) * (1 - u) * u * 1.0 + 3 * (1 - u) * u * u * 1.0 + u * u * u;
  }
  inline double mmEaseOut(double t) { return 1 - (1 - t) * (1 - t); }
  inline double mmLerp(double a, double b, double t) { return a + (b - a) * t; }

  // How long each glyph's hover plays (the browser's animation durations): the line and
  // the arrow 450ms; the flame 900; the drop 1125 (user decision: 1.5x slower again);
  // the specks 825 each with the five staggered 90ms apart.
  // Desktop plays the hover 1.5x faster than the browser twin (user decision): the same
  // per-mode lengths, scaled by one factor so the rows and the trigger stay in step.
  inline constexpr double kMotionIconSpeedup = 1.5;
  double motionIconMs(const QString& mode);
  // The longest of them — how long a row's hover keeps repainting (also 1.5x shorter now).
  constexpr int kMotionIconHoverMs = static_cast<int>(1200 / kMotionIconSpeedup);
  // The browser's glyph box (js/ui/motionIcons.js: viewBox 0 0 16 16 at width/height 16).
  constexpr int kMotionIconPx = 16;

  void paintMotionIcon(QPainter& p, const QRectF& box, const QString& mode, const QColor& colour,
                       double ms = 1e9);

  QIcon motionIconFrame(const QString& mode, const QColor& colour, double ms = 1e9,
                        int px = 16, double dpr = 2.0);
  QIcon motionModeIcon(const QString& mode, const QColor& colour, int px = 16, double dpr = 2.0);

  // Row delegate for the combo's popup list: draws the row as usual, then the mode's
  // glyph over its icon slot, animated over `kHoverMs` from the moment the row is
  // hovered (the browser's hover keyframes). The mode key is the row's item DATA.
  class MotionIconDelegate : public QStyledItemDelegate {
   public:
    static constexpr int kHoverMs = kMotionIconHoverMs;   // the longest glyph's hover
    explicit MotionIconDelegate(QAbstractItemView* view, QObject* parent = nullptr);
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

   protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

   private:
    QAbstractItemView* view_;
    int hoverRow_ = -1;
    QElapsedTimer clock_;
    QTimer tick_;
  };

  // The combo FACE's glyph, played like the rows': once when the value changes (the
  // browser's .mm-play) and whenever the trigger is hovered. Repaints the current item's
  // icon frame by frame, then leaves the rest frame in place.
  class MotionIconFace : public QObject {
   public:
    explicit MotionIconFace(QComboBox* combo);
    void play();

    void reink();

   protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

   private:
    void frame();
    QComboBox* combo_;
    QColor inked_;      // the ink the rows' glyphs were last drawn in
    QElapsedTimer clock_;
    QTimer tick_;
  };

}  // namespace stencil::support
