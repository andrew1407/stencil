#include "DisintegrateOverlay.hpp"

namespace stencil::gui {

  // Colour comes from the palette by mix and hash, never the cell's own - the cell only said how
  // much paint there was (`alpha`). Styles size by what is LEFT to `dest` (browser moteFrame).
  void DisintegrateOverlay::finishGrain(Mote* out, double alpha, bool glint, double w, int tint,
                                        double p, double away, double tx, double ty,
                                        bool fromFar, const QPointF& dest) const {
    const double len = std::hypot(dest.x() - out->at.x(), dest.y() - out->at.y());
    out->shape = support::grainShape(style, w);
    out->heading = support::headingOf(tx, ty, fromFar);
    if (style == support::ParticleStyle::DUST) {
      out->color = support::tintedStop(accent, shade, support::dustMix(w, glint), tint, dark);
      out->color.setAlphaF(std::clamp(alpha * twinkleAt(glint, t * ms, w), 0.0, 1.0));
      return;
    }
    const support::StyleFrame sf = support::styleFrame(style, p, away, w, len, t * ms);
    out->at += QPointF(sf.sx, sf.sy);
    out->radius *= sf.scale;
    out->color = support::tintedStop(accent, shade, sf.mix, tint, dark);
    out->color.setAlphaF(std::clamp(alpha * sf.glow, 0.0, 1.0));
  }

  void DisintegrateOverlay::paintEvent(QPaintEvent*) {
    if (snap.isNull()) return;
    if (cells.width() != cols || cells.height() != rows) {
      cells = sampleCells(snap, cols, rows);
      // Once per flight: a QColor read per grain per frame was a third of the frame.
      grains.resize(size_t(cols) * rows);
      glints.assign(size_t(cols) * rows, false);
      tints.assign(size_t(cols) * rows, -1);
      for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
          const double n = cellNoise(cx, cy);
          grains[size_t(cy) * cols + cx] = liftedGrain(cx, cy, n);
          glints[size_t(cy) * cols + cx] = isGlint(cx, cy, n);
          tints[size_t(cy) * cols + cx] = support::tintOf(cellNoise(cx + 13, cy + 71));
        }
    }
    QPainter p(this);
    if (paintClip.isValid()) p.setClipRect(paintClip.translated(shift));
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // `shift` is zero for a child layer; an escaped one draws host coords in its own.
    const QRectF box = picture.isValid() ? QRectF(picture.translated(shift))
                                          : QRectF(rect()).adjusted(pad, pad, -pad, -pad);
    if (box.width() <= 0 || box.height() <= 0) return;
    // The snapshot is blitted through a CLIP that leaves out departed cells — never a
    // Source-mode clear, which punched a black hole through the window's backing store.
    const double cw = box.width() / cols;
    const double ch = box.height() / rows;
    const bool surface = sweep == Sweep::SURFACE_IN || sweep == Sweep::SURFACE_OUT;
    motes.clear();
    cut.clear();
    for (int cy = 0; cy < rows; ++cy) {
      // The outer edge rounds up, or a fractional box leaves an uncleared hairline.
      const int y0 = qRound(box.y() + cy * ch);
      const int y1 = cy == rows - 1 ? int(std::ceil(box.bottom())) : qRound(box.y() + (cy + 1) * ch);
      int runStart = -1;
      for (int cx = 0; cx <= cols; ++cx) {
        Mote m;
        const bool away = cx < cols
            && (surface ? surfaceMote(box, cx, cy, cw, ch, &m)
                : sweep == Sweep::ROWS ? rowMote(box, cx, cy, cw, ch, &m)
                                        : fallingMote(box, cx, cy, cw, ch, &m));
        if (away) {
          if (runStart < 0) runStart = cx;
          if (m.radius > 0.25 && m.color.alphaF() > 0.01) motes.push_back(m);
        } else if (runStart >= 0) {
          // QRegion::setRects wants Y-X sorted, non-abutting rects: one per run.
          const int x1 = cx == cols ? int(std::ceil(box.right())) : qRound(box.x() + cx * cw);
          cut.push_back(QRect(QPoint(qRound(box.x() + runStart * cw), y0), QPoint(x1 - 1, y1 - 1)));
          runStart = -1;
        }
      }
    }
    if (!base.isNull()) p.drawPixmap(box, base, QRectF(base.rect()));
    if (surface) {
      // A surface is never cut cell by cell (a blocky staircase): it fades up behind
      // the motes (browser surfaceForm) or cuts out over its first beat (surfaceLeave).
      const double fade = sweep == Sweep::SURFACE_IN
          ? (t < DUST_HOLD ? 0.0 : (t - DUST_HOLD) / (1.0 - DUST_HOLD))
          : std::max(0.0, 1.0 - t / SURFACE_SCATTER_SPLIT);
      if (fade > 0.0) {
        p.setOpacity(fade);
        p.drawPixmap(box, snap, QRectF(snap.rect()));
        p.setOpacity(1.0);
      }
    } else {
      QRegion keep(box.toAlignedRect());
      if (!cut.empty()) {
        QRegion gone;
        gone.setRects(cut.data(), int(cut.size()));
        keep -= gone;
      }
      if (!keep.isEmpty()) {
        p.save();
        p.setClipRegion(keep, Qt::IntersectClip);
        p.drawPixmap(box, snap, QRectF(snap.rect()));
        p.restore();
      }
    }
    // Antialiasing OFF, or the raster engine leaves its 1:1 blit fast path.
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Mote& m : motes) sprites.draw(p, m.at, m.radius, m.color, m.shape, m.heading);
    p.setOpacity(1.0);
  }


  const QColor& DisintegrateOverlay::grainColour(int cx, int cy, double) const {
    return grains[size_t(cy) * cols + cx];
  }


  // Browser speckPainter's RIM and GLINT cells: the edge, and one inner cell in seven.
  bool DisintegrateOverlay::isGlint(int cx, int cy, double n) const {
    return cx == 0 || cy == 0 || cx == cols - 1 || cy == rows - 1 || n > GLINT_HASH;
  }


  QColor DisintegrateOverlay::liftedGrain(int cx, int cy, double n) const {
    QColor c = cellColour(cells, cx, cy);
    if (!ink.isValid() || c.alphaF() <= 0.02) return c;
    if (!isGlint(cx, cy, n)) return c;
    return QColor(qRound(c.red() + (ink.red() - c.red()) * GLINT_MIX),
                  qRound(c.green() + (ink.green() - c.green()) * GLINT_MIX),
                  qRound(c.blue() + (ink.blue() - c.blue()) * GLINT_MIX), c.alpha());
  }
}  // namespace stencil::gui
