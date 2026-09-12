#include "controlSwap.hpp"

namespace stencil::gui {

  // A superseding change deletes the one in flight, so a burst ends on the last value.
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


  void ValueSwapOverlay::cancel(QComboBox* cb) {
    const auto live =
        cb->findChildren<QWidget*>(QString::fromLatin1(VALUE_SWAP_OBJECT_NAME),
                                   Qt::FindDirectChildrenOnly);
    for (QWidget* w : live) delete w;   // a deleted animation never emits finished()
    ctl::hideComboLabel(cb, false);
  }


  bool ValueSwapOverlay::running(const QComboBox* cb) {
    return cb
           && cb->findChild<QWidget*>(QString::fromLatin1(VALUE_SWAP_OBJECT_NAME),
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
    // Sequential: the outgoing word is most of the way out before the incoming starts.
    // Each cloud is composed on its own layer, or the second word's cut-outs would take
    // the first word's flying grains with them.
    renderCloud(&layerOut_, out_, &cellsOut_,
                std::clamp(t_ / VALUE_SWAP_OUT_SHARE, 0.0, 1.0), false);
    renderCloud(&layerIn_, in_, &cellsIn_,
                std::clamp((t_ - VALUE_SWAP_PIVOT) / (1.0 - VALUE_SWAP_PIVOT), 0.0, 1.0), true);
    p.drawImage(rect(), layerOut_);
    p.drawImage(rect(), layerIn_);
  }


  // The twin of DisintegrateOverlay's Fall/Gather at word scale, same hashes and bend.
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
    const int cols = std::max(1, qRound(box.width() / VALUE_SWAP_CELL_PX));
    const int rows = std::max(1, qRound(box.height() / VALUE_SWAP_CELL_PX));
    const double cw = box.width() / cols;
    const double ch = box.height() / rows;
    if (cells->width() != cols || cells->height() != rows) {
      // Once: the field's slice of the label sheet, area-averaged down to the grid.
      const double sx = double(pm.width()) / std::max(1, width());
      const double sy = double(pm.height()) / std::max(1, height());
      const QRect src(qRound(box.x() * sx), qRound(box.y() * sy),
                      std::max(1, qRound(box.width() * sx)), std::max(1, qRound(box.height() * sy)));
      *cells = DisintegrateOverlay::sampleCells(pm.copy(src), cols, rows);
    }
    QPainter p(layer);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    static const QEasingCurve OUT_CURVE(QEasingCurve::OutQuint);
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
          // A word comes apart the way it is read, left to right.
          const double along = cols > 1 ? double(cx) / (cols - 1) : 0.0;
          const double delay = (gather ? 1.0 - along : along) * 0.4 + n * 0.1;
          double k = (t - delay) / std::max(0.05, 1.0 - delay);
          away = gather ? k < 1.0 : k > 0.0;
          if (away) {
            k = std::clamp(k, 0.0, 1.0);
            const double e = OUT_CURVE.valueForProgress(k);
            const double far = gather ? 1.0 - e : e;
            QColor c = DisintegrateOverlay::cellColour(*cells, cx, cy);
            const double alpha = c.alphaF() * (0.78 + n * 0.22)
                * (gather ? DisintegrateOverlay::gatherAlpha(k) : DisintegrateOverlay::scatterAlpha(k));
            if (far < 1.0 && alpha > 0.02) {
              const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
              const double tx = (m - 0.5) * VALUE_SWAP_THROW_PX;
              const double ty = (0.3 + n * 0.7) * VALUE_SWAP_THROW_PX;
              const double w = DisintegrateOverlay::cellNoise(cx + 13, cy + 71);
              Grain g{home + QPointF(far * tx, far * ty) + DisintegrateOverlay::swirlAt(far, tx, ty, q),
                      DisintegrateOverlay::moteRadius(cw, ch, n) * (1.0 - far * (0.6 - n * 0.25)), c,
                      support::grainShape(style_, w), support::headingOf(tx, ty, gather)};
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
    // A clip on the blit, never a clear (disintegrateOverlay.hpp paintEvent).
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
    setObjectName(QString::fromLatin1(VALUE_SWAP_OBJECT_NAME));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
  }
}  // namespace stencil::gui
