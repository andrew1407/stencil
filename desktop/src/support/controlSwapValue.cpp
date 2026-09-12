#include "controlSwap.hpp"

namespace stencil::gui {

  // Swap `cb`'s displayed value from `from` to `to`. A superseding change deletes the
  // one in flight and starts over, so a burst of picks always ends on the last value.
  void ValueSwapOverlay::play(QComboBox* cb, const QString& from, const QString& to, int ms) {
    if (!cb || ms <= 0) return;
    cancel(cb);
    // Rendered first — hideComboLabel would blank these too.
    const QPixmap out = ctl::comboLabelPixmap(cb, from);
    const QPixmap in = ctl::comboLabelPixmap(cb, to);
    ctl::hideComboLabel(cb, true);
    auto* fx = new ValueSwapOverlay(cb, out, in, ctl::comboFieldRect(cb));
    fx->setGeometry(cb->rect());
    fx->show();
    fx->raise();
    auto* anim = new QVariantAnimation(fx);
    anim->setDuration(ms);
    fx->ms_ = ms;
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    // Linear — the shaping lives in faceSwapFrame's two curves.
    connect(anim, &QVariantAnimation::valueChanged, fx, [fx](const QVariant& v) {
      fx->t_ = v.toDouble();
      fx->update();
    });
    connect(anim, &QVariantAnimation::finished, fx, [fx] {
      if (auto* owner = qobject_cast<QComboBox*>(fx->parentWidget()))
        ctl::hideComboLabel(owner, false);   // the combo paints its own word again
      fx->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }


  // Drop any swap `cb` has in flight and hand it its own label back.
  void ValueSwapOverlay::cancel(QComboBox* cb) {
    const auto live =
        cb->findChildren<QWidget*>(QString::fromLatin1(kValueSwapObjectName),
                                   Qt::FindDirectChildrenOnly);
    for (QWidget* w : live) delete w;   // a deleted animation never emits finished()
    ctl::hideComboLabel(cb, false);
  }


  // True while `cb` is mid-exchange.
  bool ValueSwapOverlay::running(const QComboBox* cb) {
    return cb
           && cb->findChild<QWidget*>(QString::fromLatin1(kValueSwapObjectName),
                                      Qt::FindDirectChildrenOnly) != nullptr;
  }

  void ValueSwapOverlay::paintEvent(QPaintEvent*) {
    if (!styled_) {
      styled_ = true;
      style_ = support::particleStyle();
      accent_ = support::particleAccent();
      shade_ = support::particleShade();
      dark_ = support::particleDark();
    }
    QPainter p(this);
    p.setClipRect(clip_);   // clipped by the edit field, the way the word itself is
    // Sequential, like the odometer this replaces: the outgoing word is most of the way
    // out before the incoming one starts arriving, so two values are never legible at
    // once — only, now, both are sand. Each cloud is composed on its own layer first:
    // a word is the picture wherever its cells are at home and cut away wherever they
    // are not, and the second word's cut-outs must not take the first word's flying
    // grains with them.
    renderCloud(&layerOut_, out_, &cellsOut_,
                std::clamp(t_ / kValueSwapOutShare, 0.0, 1.0), false);
    renderCloud(&layerIn_, in_, &cellsIn_,
                std::clamp((t_ - kValueSwapPivot) / (1.0 - kValueSwapPivot), 0.0, 1.0), true);
    p.drawImage(rect(), layerOut_);
    p.drawImage(rect(), layerIn_);
  }


  // One cloud of a WORD onto `layer`: the label picture, minus the cells that have
  // left it, plus those cells as round grains of their own ink — the twin of
  // DisintegrateOverlay's Fall/Gather at word scale, on the same hashes and the same
  // bend, so the app's sand all behaves alike. `t` is this cloud's own progress;
  // `gather` reads the same journey backwards.
  void ValueSwapOverlay::renderCloud(QImage* layer, const QPixmap& pm, QImage* cells, double t,
                                     bool gather) {
    const qreal dpr = devicePixelRatioF();
    const QSize px(std::max(1, qRound(width() * dpr)), std::max(1, qRound(height() * dpr)));
    if (layer->size() != px) {
      *layer = QImage(px, QImage::Format_ARGB32_Premultiplied);
      layer->setDevicePixelRatio(dpr);
    }
    layer->fill(Qt::transparent);
    if (pm.isNull() || (gather ? t <= 0.0 : t >= 1.0)) return;
    const QRectF box(clip_);
    if (box.width() < 2 || box.height() < 2) return;
    const int cols = std::max(1, qRound(box.width() / kValueSwapCellPx));
    const int rows = std::max(1, qRound(box.height() / kValueSwapCellPx));
    const double cw = box.width() / cols;
    const double ch = box.height() / rows;
    if (cells->width() != cols || cells->height() != rows) {
      // The word's colour per cell, once: the field's slice of the label sheet (which
      // covers the whole control, device-pixel scaled), area-averaged down to the grid.
      const double sx = double(pm.width()) / std::max(1, width());
      const double sy = double(pm.height()) / std::max(1, height());
      const QRect src(qRound(box.x() * sx), qRound(box.y() * sy),
                      std::max(1, qRound(box.width() * sx)), std::max(1, qRound(box.height() * sy)));
      *cells = DisintegrateOverlay::sampleCells(pm.copy(src), cols, rows);
    }
    QPainter p(layer);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    static const QEasingCurve kOut(QEasingCurve::OutQuint);
    struct Grain { QPointF at; double r; QColor c; support::GrainShape shape; double heading; };
    std::vector<Grain> grains;
    std::vector<QRect> cut;
    for (int cy = 0; cy < rows; ++cy) {
      const int y0 = qRound(box.y() + cy * ch);
      const int y1 = cy == rows - 1 ? int(std::ceil(box.bottom())) : qRound(box.y() + (cy + 1) * ch);
      int runStart = -1;
      for (int cx = 0; cx <= cols; ++cx) {
        bool away = false;
        if (cx < cols) {
          const double n = DisintegrateOverlay::cellNoise(cx, cy);
          const double m = DisintegrateOverlay::cellNoise(cx + 41, cy + 17);
          const double q = DisintegrateOverlay::cellNoise(cx + 97, cy + 53);
          // A word is READ left to right, so it comes apart that way — and gathers
          // back the same sweep reversed, the rule every other flight here follows.
          const double along = cols > 1 ? double(cx) / (cols - 1) : 0.0;
          const double delay = (gather ? 1.0 - along : along) * 0.4 + n * 0.1;
          double k = (t - delay) / std::max(0.05, 1.0 - delay);
          // At home — not yet left, or already landed — the cell is the word itself.
          away = gather ? k < 1.0 : k > 0.0;
          if (away) {
            k = std::clamp(k, 0.0, 1.0);
            const double e = kOut.valueForProgress(k);
            const double far = gather ? 1.0 - e : e;
            QColor c = DisintegrateOverlay::cellColour(*cells, cx, cy);
            const double alpha = c.alphaF() * (0.78 + n * 0.22)
                * (gather ? DisintegrateOverlay::gatherAlpha(k) : DisintegrateOverlay::scatterAlpha(k));
            if (far < 1.0 && alpha > 0.02) {
              const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
              const double tx = (m - 0.5) * kValueSwapThrowPx;
              const double ty = (0.3 + n * 0.7) * kValueSwapThrowPx;
              const double w = DisintegrateOverlay::cellNoise(cx + 13, cy + 71);
              Grain g{home + QPointF(far * tx, far * ty) + DisintegrateOverlay::swirlAt(far, tx, ty, q),
                      DisintegrateOverlay::moteRadius(cw, ch, n) * (1.0 - far * (0.6 - n * 0.25)), c,
                      support::grainShape(style_, w), support::headingOf(tx, ty, gather)};
              // Painted from the theme's palette like every cloud — the word's own ink
              // only says how much paint the cell held.
              const support::StyleFrame sf = support::styleFrame(style_, k, far, w, std::hypot(tx, ty), t_ * ms_);
              g.at += QPointF(sf.sx, sf.sy);
              g.r *= sf.scale;
              g.c = support::tintedStop(accent_, shade_,
                                        style_ == support::ParticleStyle::Dust ? support::dustMix(w, false) : sf.mix,
                                        support::tintOf(w), dark_);
              g.c.setAlphaF(std::min(1.0, alpha * sf.glow));
              grains.push_back(g);
            }
          }
        }
        if (away) {
          if (runStart < 0) runStart = cx;
        } else if (runStart >= 0) {
          const int x1 = cx == cols ? int(std::ceil(box.right())) : qRound(box.x() + cx * cw);
          cut.push_back(QRect(QPoint(qRound(box.x() + runStart * cw), y0), QPoint(x1 - 1, y1 - 1)));
          runStart = -1;
        }
      }
    }
    // The word, minus the cells that have left it — a clip on the blit, never a clear
    // (disintegrateOverlay.hpp paintEvent explains why).
    QRegion keep(rect());
    if (!cut.empty()) {
      QRegion gone;
      gone.setRects(cut.data(), int(cut.size()));
      keep -= gone;
    }
    if (!keep.isEmpty()) {
      p.save();
      p.setClipRegion(keep);
      p.drawPixmap(0, 0, pm);
      p.restore();
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    for (const Grain& g : grains) {
      if (g.shape == support::GrainShape::Disc) {
        p.setBrush(g.c);
        p.drawEllipse(g.at, g.r, g.r);
      } else {
        sprites_.draw(p, g.at, g.r, g.c, g.shape, g.heading);
      }
    }
  }

  ValueSwapOverlay::ValueSwapOverlay(QComboBox* cb, const QPixmap& out, const QPixmap& in,
                                     const QRect& clip) : QWidget(cb), out_(out), in_(in), clip_(clip) {
    setObjectName(QString::fromLatin1(kValueSwapObjectName));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
  }
}  // namespace stencil::gui
