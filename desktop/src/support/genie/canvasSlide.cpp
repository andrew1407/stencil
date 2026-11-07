#include "canvasSlide.hpp"

#include "GenieGhost.hpp"
#include "SurfaceCover.hpp"
#include "../modal/ModalBackdrop.hpp"
#include "../motionPrefs.hpp"
#include "../skinPrefs.hpp"
#include "theme.hpp"

#include <QColor>
#include <QEasingCurve>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <optional>

namespace stencil::support {

  namespace {
    constexpr int WIPE_MS = 413;   // browser animations/dust.css canvasRise / canvasSink (user report)
    // A picture pours in 1.5x a window's open (browser canvasSlide.js CANVAS_ARRIVE_MS).
    constexpr int ARRIVE_MS = genie::OPEN_MS * 3 / 2;
    constexpr const char* ARRIVING_PROPERTY = "stencilCanvasArriving";
    // An arrival over the desk (browser canvasRise); a copy sinking off the cleared view (canvasSink).
    enum class Wipe { RISE, SINK };
    // The desk the canvas sits on, as the theme paints it.
    QColor desk() { return stencil::gui::themePalette(skinDark()).bgCanvas; }

    // Wipes the desk in over `under` from the bottom of what shows (`shownBottom`, under-local) upward, or
    // `picture` away from its top down.
    class Veil : public QWidget {
     public:
      Veil(QWidget* under, const QPixmap& picture, Wipe wipe, std::optional<int> shownBottom = std::nullopt)
          : QWidget(under), picture(picture), ground(desk()), wipe(wipe),
            shownBottom(shownBottom) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(under->rect());
        show();
        raise();
      }
      void play(bool isOpening) {
        auto* anim = new QVariantAnimation(this);
        anim->setStartValue(isOpening ? 0.0 : 1.0);
        anim->setEndValue(isOpening ? 1.0 : 0.0);
        anim->setDuration(motionMs(WIPE_MS));
        anim->setEasingCurve(isOpening ? QEasingCurve::OutCubic : QEasingCurve::InOutSine);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { t = v.toDouble(); update(); });
        connect(anim, &QVariantAnimation::finished, this, &QObject::deleteLater);
        anim->start();
      }

     protected:
      void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        (this->*PAINTERS[static_cast<int>(wipe)])(p);
      }

     private:
      // Browser dust.css canvasRise: the desk is drawn back from the visible bottom edge upward.
      void paintRise(QPainter& p) const {
        const int bottom = shownBottom.value_or(height());
        p.fillRect(QRect(0, 0, width(), static_cast<int>(bottom * (1.0 - t))), ground);
      }
      // canvasSink: the copy goes from its top down, its lower rows the last to leave.
      void paintSink(QPainter& p) const {
        const int cut = static_cast<int>(height() * (1.0 - t));
        const double k = picture.height() / static_cast<double>(qMax(1, height()));   // device px per logical row
        p.drawPixmap(QRectF(0, cut, width(), height() - cut), picture, QRectF(0, cut * k, picture.width(), (height() - cut) * k));
      }
      using Painter = void (Veil::*)(QPainter&) const;
      static constexpr Painter PAINTERS[] = {&Veil::paintRise, &Veil::paintSink};   // by Wipe

      QPixmap picture;
      QColor ground;   // the desk, read once: a frame never rebuilds the palette
      Wipe wipe;
      std::optional<int> shownBottom;
      double t = 0.0;
    };

    MotionMode playable() { return motionReduced() ? MotionMode::NONE : motionMode(); }

    // The middle of the view's bottom edge, GLOBAL: the strip a picture pours out of and back into.
    QRect bottomStrip(QWidget* view) {
      const QRect r(view->mapToGlobal(QPoint(0, 0)), view->size());
      return QRect(r.center().x() - 40, r.bottom() - 2, 80, 2);
    }

    // What of the canvas shows in its view, GLOBAL.
    QRect shownGlobal(QWidget* canvas) {
      const QRect vis = canvas->visibleRegion().boundingRect();
      return QRect(canvas->mapToGlobal(vis.topLeft()), vis.size());
    }
  }  // namespace

  void playCanvasArrival(QWidget* canvas) {
    if (canvas) ModalBackdrop::followFor(canvas->window(), CANVAS_SETTLE_MS);   // any mode, dust too
    const MotionMode mode = playable();
    if (!canvas || (mode != MotionMode::SLIDE && mode != MotionMode::TRANSITION)) return;
    // One arrival per picture: the load and its owner may both ask for it in the same turn.
    if (canvas->property(ARRIVING_PROPERTY).toBool()) return;
    canvas->setProperty(ARRIVING_PROPERTY, true);
    QPointer<QWidget> guard(canvas);
    QTimer::singleShot(0, canvas, [guard, mode] {
      QWidget* c = guard;
      if (c) c->setProperty(ARRIVING_PROPERTY, false);
      QWidget* view = c ? c->parentWidget() : nullptr;
      if (!c || !view || c->width() < 1 || !c->isVisible()) return;
      const QRect vis = c->visibleRegion().boundingRect();
      if (vis.isEmpty()) return;
      if (mode == MotionMode::TRANSITION) {
        auto* v = new Veil(c, QPixmap(), Wipe::RISE, vis.bottom() + 1);
        v->play(true);
        return;
      }
      const QPixmap shot = c->grab(vis);
      genie::SurfaceCover* cover = genie::fillCover(c, desk());   // the desk under the picture in flight
      QPointer<genie::SurfaceCover> coverGuard(cover);
      if (!genie::fly(c->window(), shot, shownGlobal(c), bottomStrip(view), /*isOpening=*/true, /*asWindow=*/false,
                      [coverGuard] { if (coverGuard) coverGuard->deleteLater(); }, ARRIVE_MS))
        cover->deleteLater();
    });
  }

  void playCanvasLeave(QWidget* canvas) {
    if (canvas) ModalBackdrop::followFor(canvas->window(), CANVAS_SETTLE_MS);
    const MotionMode mode = playable();
    if (!canvas || (mode != MotionMode::SLIDE && mode != MotionMode::TRANSITION)) return;
    QWidget* view = canvas->parentWidget();
    if (!view || canvas->width() < 1 || !canvas->isVisible()) return;
    const QRect vis = canvas->visibleRegion().boundingRect();
    if (vis.isEmpty()) return;
    const QPixmap shot = canvas->grab(vis);
    if (mode == MotionMode::SLIDE) {
      genie::fly(canvas->window(), shot, shownGlobal(canvas), bottomStrip(view), /*isOpening=*/false, /*asWindow=*/false, {});
      return;
    }
    // Over the view, not the canvas: the canvas is about to shrink to the idle card.
    auto* copy = new Veil(view, shot, Wipe::SINK);
    copy->setGeometry(QRect(canvas->mapTo(view, vis.topLeft()), vis.size()));
    copy->play(false);
  }

}  // namespace stencil::support
