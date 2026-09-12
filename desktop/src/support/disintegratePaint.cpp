#include "disintegrateOverlay.hpp"

namespace stencil::gui {

  // Colour comes from the palette by mix and hash, never the cell's own — the cell only
  // said how much paint there was (`alpha`). Styles size by what is LEFT to `dest`
  // (browser moteFrame).
  void DisintegrateOverlay::finishGrain(Mote* out, double alpha, bool glint, double w, int tint,
                                        double p, double away, double tx, double ty,
                                        bool fromFar, const QPointF& dest) const {
    const double len = std::hypot(dest.x() - out->at.x(), dest.y() - out->at.y());
    out->shape = support::grainShape(style_, w);
    out->heading = support::headingOf(tx, ty, fromFar);
    if (style_ == support::ParticleStyle::DUST) {
      out->color = support::tintedStop(accent_, shade_, support::dustMix(w, glint), tint, dark_);
      out->color.setAlphaF(std::clamp(alpha * twinkleAt(glint, t_ * ms_, w), 0.0, 1.0));
      return;
    }
    const support::StyleFrame sf = support::styleFrame(style_, p, away, w, len, t_ * ms_);
    out->at += QPointF(sf.sx, sf.sy);
    out->radius *= sf.scale;
    out->color = support::tintedStop(accent_, shade_, sf.mix, tint, dark_);
    out->color.setAlphaF(std::clamp(alpha * sf.glow, 0.0, 1.0));
  }

  void DisintegrateOverlay::paintEvent(QPaintEvent*) {
    if (snap_.isNull()) return;
    if (cells_.width() != cols_ || cells_.height() != rows_) {
      cells_ = sampleCells(snap_, cols_, rows_);
      // Once per flight: a QColor read per grain per frame was a third of the frame.
      grains_.resize(size_t(cols_) * rows_);
      glints_.assign(size_t(cols_) * rows_, false);
      tints_.assign(size_t(cols_) * rows_, -1);
      for (int cy = 0; cy < rows_; ++cy)
        for (int cx = 0; cx < cols_; ++cx) {
          const double n = cellNoise(cx, cy);
          grains_[size_t(cy) * cols_ + cx] = liftedGrain(cx, cy, n);
          glints_[size_t(cy) * cols_ + cx] = isGlint(cx, cy, n);
          tints_[size_t(cy) * cols_ + cx] = support::tintOf(cellNoise(cx + 13, cy + 71));
        }
    }
    QPainter p(this);
    if (paintClip_.isValid()) p.setClipRect(paintClip_.translated(shift_));
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // `shift_` is zero for a child layer; an escaped one draws host coords in its own.
    const QRectF box = picture_.isValid() ? QRectF(picture_.translated(shift_))
                                          : QRectF(rect()).adjusted(pad_, pad_, -pad_, -pad_);
    if (box.width() <= 0 || box.height() <= 0) return;
    // The snapshot is blitted through a CLIP that leaves out departed cells — never a
    // Source-mode clear, which punched a black hole through the window's backing store.
    const double cw = box.width() / cols_;
    const double ch = box.height() / rows_;
    const bool surface = sweep_ == Sweep::SURFACE_IN || sweep_ == Sweep::SURFACE_OUT;
    motes_.clear();
    cut_.clear();
    for (int cy = 0; cy < rows_; ++cy) {
      // The outer edge rounds up, or a fractional box leaves an uncleared hairline.
      const int y0 = qRound(box.y() + cy * ch);
      const int y1 = cy == rows_ - 1 ? int(std::ceil(box.bottom())) : qRound(box.y() + (cy + 1) * ch);
      int runStart = -1;
      for (int cx = 0; cx <= cols_; ++cx) {
        Mote m;
        const bool away = cx < cols_
            && (surface ? surfaceMote(box, cx, cy, cw, ch, &m)
                : sweep_ == Sweep::ROWS ? rowMote(box, cx, cy, cw, ch, &m)
                                        : fallingMote(box, cx, cy, cw, ch, &m));
        if (away) {
          if (runStart < 0) runStart = cx;
          if (m.radius > 0.25 && m.color.alphaF() > 0.01) motes_.push_back(m);
        } else if (runStart >= 0) {
          // QRegion::setRects wants Y-X sorted, non-abutting rects: one per run.
          const int x1 = cx == cols_ ? int(std::ceil(box.right())) : qRound(box.x() + cx * cw);
          cut_.push_back(QRect(QPoint(qRound(box.x() + runStart * cw), y0), QPoint(x1 - 1, y1 - 1)));
          runStart = -1;
        }
      }
    }
    if (!base_.isNull()) p.drawPixmap(box, base_, QRectF(base_.rect()));
    if (surface) {
      // A surface is never cut cell by cell (a blocky staircase): it fades up behind
      // the motes (browser surfaceForm) or cuts out over its first beat (surfaceLeave).
      const double fade = sweep_ == Sweep::SURFACE_IN
          ? (t_ < DUST_HOLD ? 0.0 : (t_ - DUST_HOLD) / (1.0 - DUST_HOLD))
          : std::max(0.0, 1.0 - t_ / SURFACE_SCATTER_SPLIT);
      if (fade > 0.0) {
        p.setOpacity(fade);
        p.drawPixmap(box, snap_, QRectF(snap_.rect()));
        p.setOpacity(1.0);
      }
    } else {
      QRegion keep(box.toAlignedRect());
      if (!cut_.empty()) {
        QRegion gone;
        gone.setRects(cut_.data(), int(cut_.size()));
        keep -= gone;
      }
      if (!keep.isEmpty()) {
        p.save();
        p.setClipRegion(keep, Qt::IntersectClip);
        p.drawPixmap(box, snap_, QRectF(snap_.rect()));
        p.restore();
      }
    }
    // Antialiasing OFF, or the raster engine leaves its 1:1 blit fast path.
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Mote& m : motes_) sprites_.draw(p, m.at, m.radius, m.color, m.shape, m.heading);
    p.setOpacity(1.0);
  }


  const QColor& DisintegrateOverlay::grainColour(int cx, int cy, double) const {
    return grains_[size_t(cy) * cols_ + cx];
  }


  // Browser speckPainter's RIM and GLINT cells: the edge, and one inner cell in seven.
  bool DisintegrateOverlay::isGlint(int cx, int cy, double n) const {
    return cx == 0 || cy == 0 || cx == cols_ - 1 || cy == rows_ - 1 || n > GLINT_HASH;
  }


  QColor DisintegrateOverlay::liftedGrain(int cx, int cy, double n) const {
    QColor c = cellColour(cells_, cx, cy);
    if (!ink_.isValid() || c.alphaF() <= 0.02) return c;
    if (!isGlint(cx, cy, n)) return c;
    return QColor(qRound(c.red() + (ink_.red() - c.red()) * GLINT_MIX),
                  qRound(c.green() + (ink_.green() - c.green()) * GLINT_MIX),
                  qRound(c.blue() + (ink_.blue() - c.blue()) * GLINT_MIX), c.alpha());
  }
}  // namespace stencil::gui
