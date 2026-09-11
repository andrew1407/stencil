#pragma once

// Paints a rounded golden outline around server (shared) rows — the desktop analogue
// of the browser's `.project-remote` border — plus a vertical "⋯" kebab on every
// real row (the browser modal's per-row "more actions" button).

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
    // Where each row's name was last painted (viewport coords) — the dialog's
    // name-hover stroke and inline-rename hit tests read it.
    QRect nameRectFor(int row) const { return nameRects_.value(row); }
    // Cap the row width to the viewport so long "<name> — <url>" labels ELIDE instead of
    // forcing a horizontal scrollbar (which clipped the row at narrow window widths).
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    // Hover slide (browser .project-row:hover: translateX(3px) over 0.14s ease,
    // temp rows excluded): the whole painted row eases right while hovered and
    // eases back as the pointer moves on — driven by a small per-row progress
    // ticked below, since a painted row has no widget to transition.
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override;

    // Where the THUMBNAIL was painted (viewport coords) — the exact decoration
    // rect the style laid out, checkbox excluded. The magnify hover hit test
    // and the preview dust's origin both anchor here, so they can never
    // disagree with the paint (they did, sourcing the dust from the checkbox
    // and flickering the preview).
    QRect iconRectFor(int row) const { return iconRects_.value(row); }
    // Which row's "⋯" the cursor is actually on — only its own hover styles it, not the
    // row's. The sweep over it is the app's shared ShimmerOverlay, not painted here.
    void setKebabHover(int row) { kebabRow_ = row; }
    QRect kebabChipFor(const QRect& rowRect) const { return kebabChip(rowRect); }

    // The whole row — background, text, badges — rides one painter opacity, so
    // nothing can fade out of step.
    void paintFaded(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;

   private:
    mutable QHash<int, QRect> iconRects_;
    // The row whose "⋯" the cursor is on (-1 = none).
    int kebabRow_ = -1;
    // `o` is already initStyleOption'd by paint().
    void recordIconRect(const QStyleOptionViewItem& o, const QModelIndex& idx) const;

    static constexpr double kSlidePx = 3.0;   // browser translateX(3px)
    static constexpr int kSlideMs = 140;      // browser transform 0.14s
    // Keyed by ROW — transient hover state, so a rebuild's brief re-keying is
    // harmless, and no QPersistentModelIndex is registered per row per paint.
    mutable QHash<int, double> slide_;    // per-row progress 0..1
    mutable int slideHover_ = -1;         // the row under the pointer
    mutable QPointer<QTimer> slideTick_;
    mutable QPointer<QAbstractItemView> slideView_;

    // Track the hovered row from the style state Qt hands every paint, keep the
    // 60fps tick alive only while something is mid-slide, and hand back this
    // row's current offset.
    double hoverSlideDx(const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
    void startSlideTick() const;

    // The scroll-edge reveal (unchanged): rows dissolve toward the list's top/bottom
    // edges as it scrolls (browser parity: .reveal-item in css/animations.css).
    void paintRevealed(QPainter* p, const QStyleOptionViewItem& opt,
                       const QModelIndex& idx) const;

    // Breathing room between a row's thumbnail and its name: at this icon size the style's
    // own gap leaves the text sitting against the picture.
    static constexpr int kThumbTextGap = 12;
    mutable QHash<int, QRect> nameRects_;

    // `opt` arrives already initStyleOption'd by paint() — no second init here.
    void paintRow(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
  };

}  // namespace stencil::gui
