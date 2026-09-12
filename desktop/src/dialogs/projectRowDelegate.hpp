#pragma once

// Golden outline around server rows (browser .project-remote) plus the per-row "⋯" kebab.

#include "projectsRowChrome.hpp"

#include <QAbstractItemView>
#include <QHash>
#include <QPointer>
#include <QRect>
#include <QStyledItemDelegate>
#include <QTimer>

namespace stencil::gui {

  class ProjectRowDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    // Viewport coords; the name-hover stroke and inline-rename hit tests read it.
    QRect nameRectFor(int row) const { return nameRects_.value(row); }
    // Capped to the viewport so long labels ELIDE instead of forcing a horizontal scrollbar.
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    // Browser .project-row:hover translateX(3px) over 0.14s; a painted row has no widget to transition,
    // so a per-row progress is ticked below.
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override;

    // The exact decoration rect the style laid out (checkbox excluded); the magnify hit test and the
    // preview dust's origin both anchor here so they can never disagree with the paint.
    QRect iconRectFor(int row) const { return iconRects_.value(row); }
    // Only its own hover styles the chip; the sweep is the shared ShimmerOverlay, not painted here.
    void setKebabHover(int row) { kebabRow_ = row; }
    QRect kebabChipFor(const QRect& rowRect) const { return kebabChip(rowRect); }

    // One painter opacity for the whole row, so nothing can fade out of step.
    void paintFaded(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;

   private:
    mutable QHash<int, QRect> iconRects_;
    int kebabRow_ = -1;
    void recordIconRect(const QStyleOptionViewItem& o, const QModelIndex& idx) const;

    static constexpr double kSlidePx = 3.0;
    static constexpr int kSlideMs = 140;
    // Keyed by ROW: transient hover state, so no QPersistentModelIndex per row per paint.
    mutable QHash<int, double> slide_;
    mutable int slideHover_ = -1;
    mutable QPointer<QTimer> slideTick_;
    mutable QPointer<QAbstractItemView> slideView_;

    double hoverSlideDx(const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
    void startSlideTick() const;

    // Browser .reveal-item in css/animations.css.
    void paintRevealed(QPainter* p, const QStyleOptionViewItem& opt,
                       const QModelIndex& idx) const;

    static constexpr int kThumbTextGap = 12;
    mutable QHash<int, QRect> nameRects_;

    void paintRow(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
  };

}  // namespace stencil::gui
