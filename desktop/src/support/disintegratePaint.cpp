#include "disintegrateOverlay.hpp"

namespace stencil::gui {

  // Finish a grain: its shape, heading (`tx, ty` is its throw, `fromFar` a gather) and
  // colour. Every grain is painted from the theme's palette by its mix and its hash,
  // never in the cell's own colour — the cell only said how much paint there was, which
  // `alpha` already carries. Dust twinkles; water and fire take styleFrame's touch, sized
  // by what the grain has LEFT to reach `dest` (its flight's end) rather than by the whole
  // throw — converging on an icon it then lands where dust lands (browser: moteFrame).
  void DisintegrateOverlay::finishGrain(Mote* out, double alpha, bool glint, double w, int tint,
                                        double p, double away, double tx, double ty,
                                        bool fromFar, const QPointF& dest) const {
    const double len = std::hypot(dest.x() - out->at.x(), dest.y() - out->at.y());
    out->shape = support::grainShape(style_, w);
    out->heading = support::headingOf(tx, ty, fromFar);
    if (style_ == support::ParticleStyle::Dust) {
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
      // Every grain's colour, once: the picture never changes under a flight, and
      // reading a QColor back out of the cell image per grain per frame was a third
      // of the frame at 4000 cells.
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
    // Where the picture itself sits — the whole overlay unless `pad` widened it, or a
    // SURFACE placed it somewhere inside a host-sized layer.
    // `shift_` is zero for a child layer; an escaped one draws host coords in its own.
    const QRectF box = picture_.isValid() ? QRectF(picture_.translated(shift_))
                                          : QRectF(rect()).adjusted(pad_, pad_, -pad_, -pad_);
    if (box.width() <= 0 || box.height() <= 0) return;
    // Every cell still at home IS the picture: the snapshot is blitted through a clip
    // that leaves out the cells that have left it, so the front reads as the thing
    // grinding into grains of its own colour rather than a dot screen popping over it.
    // A gathering picture closes the same way, cell by landed cell. A CLIP, never a
    // clear: this is usually a child widget painting into the window's own backing
    // store, and a Source-mode clear there punched a black hole through the window.
    const double cw = box.width() / cols_;
    const double ch = box.height() / rows_;
    const bool surface = sweep_ == Sweep::SurfaceIn || sweep_ == Sweep::SurfaceOut;
    motes_.clear();
    cut_.clear();
    for (int cy = 0; cy < rows_; ++cy) {
      // Cell edges are rounded so neighbours share one; the picture's OUTER edge
      // rounds up, or a fractional box left an uncleared hairline of it down the side.
      const int y0 = qRound(box.y() + cy * ch);
      const int y1 = cy == rows_ - 1 ? int(std::ceil(box.bottom())) : qRound(box.y() + (cy + 1) * ch);
      int runStart = -1;   // the run of departed cells being merged into one rect
      for (int cx = 0; cx <= cols_; ++cx) {
        Mote m;
        const bool away = cx < cols_
            && (surface ? surfaceMote(box, cx, cy, cw, ch, &m)
                : sweep_ == Sweep::Rows ? rowMote(box, cx, cy, cw, ch, &m)
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
    // The state left behind, under the particles — it shows wherever a cell has gone.
    if (!base_.isNull()) p.drawPixmap(box, base_, QRectF(base_.rect()));
    if (surface) {
      // A SURFACE is never shown cell by cell: the browser's motes are the window
      // until they land, and the window itself fades up behind them once they mostly
      // have (surfaceForm: held at 0 to kDustHold, then up) — or, leaving, cuts to
      // nothing over its first beat while the sand is still where it stood
      // (surfaceLeave). Cut out per cell, the front was a blocky staircase of
      // photograph that no browser surface ever shows.
      const double fade = sweep_ == Sweep::SurfaceIn
          ? (t_ < kDustHold ? 0.0 : (t_ - kDustHold) / (1.0 - kDustHold))
          : std::max(0.0, 1.0 - t_ / kSurfaceScatterSplit);
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
    // The grains: blitted from the sprite cache, never rasterised here — antialiasing
    // OFF, or the raster engine leaves its 1:1 fast path (dustKit.hpp MoteSprites).
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Mote& m : motes_) sprites_.draw(p, m.at, m.radius, m.color, m.shape, m.heading);
    p.setOpacity(1.0);
  }


  // A cell's grain colour, from the per-flight table paintEvent builds.
  const QColor& DisintegrateOverlay::grainColour(int cx, int cy, double) const {
    return grains_[size_t(cy) * cols_ + cx];
  }


  // The browser speckPainter's RIM and GLINT cells: the picture's edge, and one inner
  // cell in seven — lifted further towards the ink, and the ones that twinkle.
  bool DisintegrateOverlay::isGlint(int cx, int cy, double n) const {
    return cx == 0 || cy == 0 || cx == cols_ - 1 || cy == rows_ - 1 || n > kGlintHash;
  }


  // A cell's colour, lifted further towards the ink where the browser's speckPainter
  // paints a RIM or a GLINT.
  QColor DisintegrateOverlay::liftedGrain(int cx, int cy, double n) const {
    QColor c = cellColour(cells_, cx, cy);
    if (!ink_.isValid() || c.alphaF() <= 0.02) return c;
    if (!isGlint(cx, cy, n)) return c;
    return QColor(qRound(c.red() + (ink_.red() - c.red()) * kGlintMix),
                  qRound(c.green() + (ink_.green() - c.green()) * kGlintMix),
                  qRound(c.blue() + (ink_.blue() - c.blue()) * kGlintMix), c.alpha());
  }
}  // namespace stencil::gui
