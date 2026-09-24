#include "controlReveal.hpp"

namespace stencil::gui {


  // DisintegrateOverlay::dustGrid on this family's cell size and ceiling.
  void ctl::revealGrid(const QSize& size, int* cols, int* rows) {
    DisintegrateOverlay::dustGrid(size, CONTROL_REVEAL_CELL_PX, CONTROL_REVEAL_MAX_CELLS,
                                  cols, rows);
  }


  // The overlay self-deletes on landing; destroyed() clears the handle, so it never dangles.
  void ctl::trackRevealFx(QWidget* w, DisintegrateOverlay* fx) {
    fx->setProperty("stencilRevealOwner", QVariant::fromValue<QObject*>(w));
    fx->bindToSurface(w);
    w->setProperty(REVEAL_FX_PROPERTY, QVariant::fromValue<QObject*>(fx));
    QObject::connect(fx, &QObject::destroyed, w,
                     [w] { w->setProperty(REVEAL_FX_PROPERTY, QVariant()); });
  }


  // The layout's own cap, parked while a slide holds maximumWidth at 0.
  int ctl::parkMaxWidth(QWidget* w) {
    const QVariant had = w->property(REVEAL_MAX_WIDTH_PROPERTY);
    const int natural = had.isValid() ? had.toInt() : w->maximumWidth();
    w->setProperty(REVEAL_MAX_WIDTH_PROPERTY, natural);
    return natural;
  }

  void ctl::handBackMaxWidth(QWidget* w, int natural) {
    w->setMaximumWidth(natural);
    w->setProperty(REVEAL_MAX_WIDTH_PROPERTY, QVariant());
  }


  // Deleting a running animation emits no finished(), so a cancelled slide never runs its handler.
  void ctl::trackSlide(QWidget* w, QObject* anim, bool opening) {
    anim->setProperty(REVEAL_OPENING_PROPERTY, opening);
    w->setProperty(REVEAL_SLIDE_PROPERTY, QVariant::fromValue<QObject*>(anim));
    QObject::connect(anim, &QObject::destroyed, w,
                     [w] { w->setProperty(REVEAL_SLIDE_PROPERTY, QVariant()); });
  }


  void ctl::settleReveal(QWidget* w) {
    if (!w) return;
    delete w->property(REVEAL_FX_PROPERTY).value<QObject*>();  // destroyed() clears the handle
    delete w->property(REVEAL_SLIDE_PROPERTY).value<QObject*>();
    if (w->graphicsEffect()) w->setGraphicsEffect(nullptr);
    const QVariant parked = w->property(REVEAL_MAX_WIDTH_PROPERTY);
    if (parked.isValid()) handBackMaxWidth(w, parked.toInt());
  }


  // Only the CHILDREN are rendered, onto a cleared surface: QWidget::grab() paints the
  // window background under them, and a group photographed on a toolbar flew as a dark slab.
  QPixmap ctl::groupShot(QWidget* w) {
    if (!w || w->width() < 1 || w->height() < 1) return QPixmap();
    const qreal dpr = w->devicePixelRatioF();
    QPixmap pm(qRound(w->width() * dpr), qRound(w->height() * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    w->render(&pm, QPoint(), QRegion(), QWidget::DrawChildren);
    return pm;
  }

  DisintegrateOverlay* ctl::flyReveal(QWidget* w, const QPixmap& pm, const QRect& at,
                                      bool gather, int ms) {
    QWidget* host = w->window();
    if (!host || pm.isNull() || at.width() < 4 || at.height() < 4) return nullptr;
    int cols = 1, rows = 1;
    revealGrid(at.size(), &cols, &rows);
    DisintegrateOverlay* fx = DisintegrateOverlay::overPixmaps(
        pm, QPixmap(), at, host,
        gather ? DisintegrateOverlay::Sweep::GATHER : DisintegrateOverlay::Sweep::FALL,
        cols, rows, ms, CONTROL_REVEAL_SPREAD, CONTROL_REVEAL_PAD_PX,
        QString::fromLatin1(CONTROL_REVEAL_OBJECT_NAME));
    if (fx) {
      trackRevealFx(w, fx);
      fx->setFollow(w);   // a sibling's slot opening in the same turn moves this one
    }
    return fx;
  }


  // Anonymous SPECKS in the control's own colours (browser surface/motion.js speckPainter / markPaint):
  // tiles cut from a glyph are nearly all transparent. MOTE_INK 42%, MOTE_RIM_INK 66% on the rim.
  QPixmap ctl::markSpecks(const QSize& size, int cols, int rows, qreal dpr, const QColor& bg,
                          const QColor& ink) {
    QPixmap sheet(qMax(1, qRound(size.width() * dpr)), qMax(1, qRound(size.height() * dpr)));
    sheet.setDevicePixelRatio(dpr);
    sheet.fill(Qt::transparent);
    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    const auto mixed = [&](double inkShare) {
      return QColor(qRound(bg.red() + (ink.red() - bg.red()) * inkShare),
                    qRound(bg.green() + (ink.green() - bg.green()) * inkShare),
                    qRound(bg.blue() + (ink.blue() - bg.blue()) * inkShare));
    };
    const QColor fill = mixed(0.42), rim = mixed(0.66);
    const double cw = double(size.width()) / cols;
    const double ch = double(size.height()) / rows;
    for (int cy = 0; cy < rows; ++cy)
      for (int cx = 0; cx < cols; ++cx) {
        const double n = DisintegrateOverlay::cellNoise(cx, cy);
        QColor c = (cx == 0 || cy == 0 || cx == cols - 1 || cy == rows - 1) ? rim : fill;
        c.setAlphaF(0.78 + n * 0.22);   // browser: never below 0.78
        const double grain = std::min({cw, ch, 7.0});   // SURFACE_SPECK_PX cap
        const double px = grain * (0.62 + n * 0.5);
        p.setBrush(c);
        p.drawEllipse(QRectF(cx * cw + (cw - px) / 2, cy * ch + (ch - px) / 2, px, px));
      }
    return sheet;
  }
}  // namespace stencil::gui
