#pragma once
// One glyph per motion mode, painted from the browser's 16-unit SVG drawings; `ms` is
// how far into the browser's `.mm-*` hover keyframes the glyph is.
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

  // The browser's easings: cubic-bezier(0.16, 1, 0.3, 1) and ease-out.
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

  // The browser's per-mode durations (line/arrow 450, flame 900, drop 1125, specks 825
  // staggered 90ms), all scaled by one factor: desktop plays the hover 1.5x faster.
  inline constexpr double MOTION_ICON_SPEEDUP = 1.5;
  double motionIconMs(const QString& mode);
  // The longest — how long a row's hover keeps repainting.
  constexpr int MOTION_ICON_HOVER_MS = static_cast<int>(1200 / MOTION_ICON_SPEEDUP);
  // Browser js/ui/motionIcons.js: viewBox 0 0 16 16.
  constexpr int MOTION_ICON_PX = 16;

  void paintMotionIcon(QPainter& p, const QRectF& box, const QString& mode, const QColor& colour,
                       double ms = 1e9);

  QIcon motionIconFrame(const QString& mode, const QColor& colour, double ms = 1e9,
                        int px = 16, double dpr = 2.0);
  QIcon motionModeIcon(const QString& mode, const QColor& colour, int px = 16, double dpr = 2.0);

  // Draws the row as usual, then the mode's glyph over its icon slot, animated over
  // `HOVER_MS` from hover. The mode key is the row's item DATA.
  class MotionIconDelegate : public QStyledItemDelegate {
   public:
    static constexpr int HOVER_MS = MOTION_ICON_HOVER_MS;   // the longest glyph's hover
    explicit MotionIconDelegate(QAbstractItemView* view, QObject* parent = nullptr);
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

   protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

   private:
    QAbstractItemView* view;
    int hoverRow = -1;
    QElapsedTimer clock;
    QTimer tick;
  };

  // The combo FACE's glyph: once when the value changes (browser .mm-play) and on hover.
  class MotionIconFace : public QObject {
   public:
    explicit MotionIconFace(QComboBox* combo);
    void play();

    void reink();

   protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

   private:
    void frame();
    QComboBox* combo;
    QColor inked;      // the ink the rows' glyphs were last drawn in
    QElapsedTimer clock;
    QTimer tick;
  };

}  // namespace stencil::support
