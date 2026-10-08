#pragma once
// The image rotate's motion: a click-through child of the scroll viewport that turns a snapshot of
// the refitted canvas upright from the old box while the real canvas is veiled; `dir` 0 is a flip,
// turned over edge-on. Browser twins: js/ui/motion/quarterTurn.js, js/ui/motion/mirrorFlip.js.
#include "dustKit.hpp"   // support::bezierY / frameIntervalMs
#include "motionPrefs.hpp"
#include "quarterTurn.hpp"

#include <QElapsedTimer>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <algorithm>

namespace stencil::gui {

  class QuarterTurnOverlay : public QWidget {
   public:
    // The canvas's box in viewport coordinates, measured before the rotate.
    static QRect boxIn(QWidget* canvas, QWidget* viewport) {
      return QRect(canvas->mapTo(viewport, QPoint(0, 0)), canvas->size());
    }

    // Call after the rotate and its refit, with the box boxIn() measured before them.
    static void play(QWidget* canvas, QWidget* viewport, const QRect& from, int dir) {
      if (!canvas || !viewport || from.isEmpty() || support::motionReduced()) return;
      for (QObject* o : viewport->children())
        if (auto* old = dynamic_cast<QuarterTurnOverlay*>(o)) old->finish();
      new QuarterTurnOverlay(canvas, viewport, from, dir);
    }

   private:
    QuarterTurnOverlay(QWidget* canvas, QWidget* viewport, const QRect& from, int dir)
        : QWidget(viewport), canvas(canvas), from(from), dir(dir), snapshot(canvas->grab()) {
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setGeometry(viewport->rect());
      veil = new QGraphicsOpacityEffect(canvas);
      veil->setOpacity(0.0);
      canvas->setGraphicsEffect(veil);
      clockStart.start();
      ticker = new QTimer(this);
      ticker->setTimerType(Qt::PreciseTimer);
      ticker->setInterval(support::frameIntervalMs(this));
      QObject::connect(ticker, &QTimer::timeout, this, [this] { tick(); });
      show();
      raise();
      ticker->start();
    }

    void tick() {
      const quarterTurn::Clock& c = quarterTurn::clock();
      const double p = std::min(1.0, clockStart.elapsed() / double(c.ms));
      t = support::bezierY(p, c.bezier[0], c.bezier[1], c.bezier[2], c.bezier[3]);
      update();
      if (p >= 1.0) finish();
    }

    void finish() {
      if (done) return;
      done = true;
      ticker->stop();
      if (canvas && veil && canvas->graphicsEffect() == veil) canvas->setGraphicsEffect(nullptr);
      hide();
      deleteLater();
    }

    void paintEvent(QPaintEvent*) override {
      if (!canvas) return;
      const QRect to = boxIn(canvas, parentWidget());
      const quarterTurn::Frame f = quarterTurn::frameAt(from, to, dir, t);
      QPainter p(this);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      if (dir == 0) {
        p.translate(QRectF(to).center());
        p.scale(2 * t - 1, 1);   // scaleX -1 is the old picture exactly: it turns over to face the user
      } else {
        p.translate(f.center);
        p.rotate(f.degrees);
        p.scale(f.scale, f.scale);
      }
      p.drawPixmap(QRectF(-to.width() / 2.0, -to.height() / 2.0, to.width(), to.height()), snapshot,
                   QRectF(snapshot.rect()));
    }

    QPointer<QWidget> canvas;
    QPointer<QGraphicsOpacityEffect> veil;
    QTimer* ticker = nullptr;
    QElapsedTimer clockStart;
    QRect from;
    int dir = 1;
    double t = 0;
    bool done = false;
    QPixmap snapshot;
  };

}  // namespace stencil::gui
