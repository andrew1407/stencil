#pragma once
#include "motionPrefs.hpp"

#include <QApplication>
#include <QDialog>
#include <QTimer>
#include <QList>
#include <QEasingCurve>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

namespace stencil::support {

  // The browser's `.app-modal-overlay` scrim plus its `backdrop-filter: blur()`, which Qt has none
  // of: the window behind is photographed as the dialog opens, blurred, painted under the scrim.
  class ModalBackdrop : public QWidget {
   public:
    // The blur the snapshot gets (browser blur(3px)) and the scrim over it (rgba(0,0,0,0.5)).
    static constexpr int BLUR_PX = 3;
    static constexpr double SCRIM_ALPHA = 0.5;
    // Browser animations/overlays.css: overlayFadeIn/overlayFadeOut, both on CSS `ease`.
    static constexpr int FADE_IN_MS = 220;
    static constexpr int FADE_OUT_MS = 340;
    // Set on a host child that rides OVER the scrim, unblurred (the browser's #notify-balloon at
    // z-index 100002, above the overlay's 100001): out of the photograph, then raised back.
    static constexpr const char* ABOVE_PROPERTY = "stencilAboveBackdrop";
    static QList<QWidget*> aboveChildren(QWidget* host) {
      QList<QWidget*> out;
      if (!host) return out;
      for (QWidget* w : host->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        if (w->isVisible() && w->property(ABOVE_PROPERTY).toBool()) out.append(w);
      return out;
    }

    // Downscale-then-upscale: a cheap blur whose cost does not grow with the window.
    static QPixmap blurred(const QPixmap& shot, int radiusPx) {
      if (shot.isNull() || radiusPx < 1) return shot;
      const qreal dpr = shot.devicePixelRatio() > 0 ? shot.devicePixelRatio() : 1.0;
      const int div = radiusPx * 2;   // two 1/(2r) passes read softer than one of 1/r
      const QSize small(qMax(2, int(shot.width() / dpr) / div),
                        qMax(2, int(shot.height() / dpr) / div));
      QPixmap out = shot.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .scaled(shot.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      out.setDevicePixelRatio(dpr);
      return out;
    }

    // Covers `host` and goes when `dlg` closes. Null when the switch is off (side-by-side windows
    // dim nothing), or when there is nothing worth photographing.
    static ModalBackdrop* behind(QDialog* dlg, QWidget* host) {
      if (!modalBackdrop() || !dlg || !host) return nullptr;
      if (host->width() < 8 || host->height() < 8) return nullptr;
      // One per dialog; and a backdrop still up (one fading out behind a window just closed) stays
      // out of the photograph, or the darkness doubles (two 50% scrims read as 75%).
      QList<QWidget*> up = host->findChildren<QWidget*>(QStringLiteral("modalBackdrop"), Qt::FindDirectChildrenOnly);
      for (QWidget* w : up)
        if (static_cast<ModalBackdrop*>(w)->owner == dlg) return static_cast<ModalBackdrop*>(w);
      const QPixmap raw = photograph(host, up);
      if (raw.isNull()) return nullptr;
      auto* bd = new ModalBackdrop(host);
      bd->owner = dlg;
      bd->setPhoto(raw);
      bd->show();
      bd->raise();
      for (QWidget* w : aboveChildren(host)) w->raise();
      // Over a backdrop already up it starts dark: fading from its undimmed photo flashed grey (user report).
      bool dimmed = false;
      for (QWidget* w : up) dimmed = dimmed || static_cast<ModalBackdrop*>(w)->level >= 0.99;
      bd->fadeTo(1.0, dimmed ? 0 : FADE_IN_MS, /*thenDrop=*/false);
      // Fades with the dialog's flight back; accepted, or with the window changed behind it (a picture a stacked dialog
      // sent), a bare scrim over the LIVE window, so the arrival plays in view rather than under a photo (user report).
      QObject::connect(dlg, &QDialog::finished, bd, [bd](int result) {
        if (result == QDialog::Accepted || bd->stale) {
          bd->live = true;
          bd->setOpaque(false);
        }
        bd->fadeTo(0.0, FADE_OUT_MS, true);
      });
      return bd;
    }
    // Re-photographs the host after a live restyle (or the scrim keeps the palette it opened on), cross-faded in.
    static void retakeOn(QWidget* host, const QList<QWidget*>& skip = {}) {
      for (ModalBackdrop* bd : on(host)) {
        const QPixmap raw = photograph(host, skip + QList<QWidget*>{bd});
        if (!raw.isNull()) bd->crossTo(raw);
      }
    }
    // Retaken once a change behind it has landed; never per frame, which lagged the desktop (user report).
    static void followFor(QWidget* host, int ms) {
      for (ModalBackdrop* bd : on(host)) bd->stale = true;
      QTimer::singleShot(motionReduced() ? 0 : motionMs(ms), host, [h = QPointer<QWidget>(host)] { if (h) retakeOn(h); });
    }
    // `w` without any backdrop dimming it: a dialog's flight picture, which flew home dark (user report).
    static QPixmap cleanShot(QWidget* w) {
      const QList<ModalBackdrop*> over = on(w);
      return w ? photograph(w, QList<QWidget*>(over.cbegin(), over.cend())) : QPixmap();
    }

    // EVERY app window behind the dialog, its parent's included: a floating chat dock or a second window stays undimmed otherwise.
    static void behindAll(QDialog* dlg) {
      if (!dlg) return;
      for (QWidget* w : QApplication::topLevelWidgets()) {
        const Qt::WindowType type = w->windowType();
        if (!w->isVisible() || w == dlg->window() || type == Qt::Popup ||
            type == Qt::ToolTip || type == Qt::SplashScreen)
          continue;
        behind(dlg, w);
      }
    }
    // Every backdrop up, fading away (Multiple windows switched on: side-by-side windows dim nothing).
    static void clearAll() {
      for (QWidget* top : QApplication::topLevelWidgets()) for (ModalBackdrop* bd : on(top)) bd->fadeTo(0.0, FADE_OUT_MS, true);
    }
    // The dim and the blur come up and go together, as one fading surface.
    void fadeTo(double to, int ms, bool thenDrop) {
      if (fade) fade->stop();
      if (motionReduced() || ms <= 0) {
        level = to;
        update();
        if (thenDrop) { hide(); deleteLater(); }
        return;
      }
      auto* anim = new QVariantAnimation(this);
      fade = anim;
      anim->setDuration(motionMs(ms));
      anim->setStartValue(level);
      anim->setEndValue(to);
      anim->setEasingCurve(QEasingCurve::InOutQuad);   // CSS `ease`
      QObject::connect(anim, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { level = v.toDouble(); update(); });
      if (thenDrop)
        QObject::connect(anim, &QAbstractAnimation::finished, this, [this] { hide(); deleteLater(); });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    double opacityNow() const { return level; }
   protected:
    void paintEvent(QPaintEvent*) override {
      if (shooting) return;   // being photographed: what lies under it is the picture
      QPainter p(this);
      if (live) { p.fillRect(rect(), QColor(0, 0, 0, int(255 * SCRIM_ALPHA * level))); return; }
      // Opaque over the window it photographed, so no frame of its fade (or of a flight over it)
      // repaints that window's widgets beneath it, which dropped frames mid-flight.
      if (opaque && level < 1.0 && !raw.isNull()) p.drawPixmap(rect(), raw);
      // One layer fading as a whole, as the browser's overlay does: a fade frame is one blend, at rest a blit.
      p.setOpacity(level);
      if (mix < 1.0 && !was.isNull()) {
        p.drawPixmap(rect(), was);
        p.setOpacity(level * mix);
      }
      p.drawPixmap(rect(), dimmed);
    }

   private:
    // The blurred window under its scrim, composed once rather than blended per frame.
    void setPhoto(const QPixmap& window) {
      raw = window;
      dimmed = blurred(window, BLUR_PX);
      QPainter p(&dimmed);
      p.fillRect(QRectF(QPointF(0, 0), dimmed.deviceIndependentSize()), QColor(0, 0, 0, static_cast<int>(255 * SCRIM_ALPHA)));
    }

    static QList<ModalBackdrop*> on(QWidget* host) {
      QList<ModalBackdrop*> out;
      for (QWidget* w : host ? host->findChildren<QWidget*>(QStringLiteral("modalBackdrop"), Qt::FindDirectChildrenOnly) : QList<QWidget*>()) out.append(static_cast<ModalBackdrop*>(w));
      return out;
    }
    // A skipped backdrop paints nothing rather than hiding: a hide/show flashed the window undimmed (user report).
    static QPixmap photograph(QWidget* host, const QList<QWidget*>& skip) {
      QList<QWidget*> hidden;
      QList<ModalBackdrop*> quiet;
      for (QWidget* w : skip + aboveChildren(host)) {
        if (!w || !w->isVisible()) continue;
        if (w->objectName() == QLatin1String("modalBackdrop")) {
          quiet.append(static_cast<ModalBackdrop*>(w));
        } else {
          w->setVisible(false);
          hidden.append(w);
        }
      }
      for (ModalBackdrop* bd : quiet) bd->setShooting(true);
      const QPixmap shot = host->grab();
      for (QWidget* w : hidden) w->setVisible(true);
      for (ModalBackdrop* bd : quiet) bd->setShooting(false);
      return shot;
    }

    void setShooting(bool on) { shooting = on; setAttribute(Qt::WA_OpaquePaintEvent, !on && opaque); }
    void crossTo(const QPixmap& window) {
      was = dimmed;
      setPhoto(window);
      mix = 1.0;
      if (motionReduced() || was.isNull()) { update(); return; }
      if (cross) cross->stop();
      cross = new QVariantAnimation(this);   // a retake mid-cross starts over from the picture now shown
      cross->setDuration(motionMs(CROSS_MS));
      QObject::connect(cross, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { mix = v.toDouble(); update(); });
      cross->setStartValue(0.0);
      cross->setEndValue(1.0);
      cross->start(QAbstractAnimation::DeleteWhenStopped);
    }
    void setOpaque(bool on) {
      opaque = on;
      setAttribute(Qt::WA_OpaquePaintEvent, on);
      update();
    }

    explicit ModalBackdrop(QWidget* host) : QWidget(host) {
      setObjectName(QStringLiteral("modalBackdrop"));
      setAttribute(Qt::WA_TransparentForMouseEvents);   // the dialog is modal; this only paints
      setGeometry(host->rect());
      setAttribute(Qt::WA_OpaquePaintEvent, true);
    }
    QPixmap raw;    // the window as photographed
    QPixmap dimmed;   // …blurred, under the scrim
    bool opaque = true;
    QPointer<QDialog> owner;   // the dialog it dims for
    double level = 0.0;   // 0 = the window as it is, 1 = fully dimmed and blurred
    QPointer<QVariantAnimation> fade, cross;   // the dim, and a retake's cross-fade
    static constexpr int CROSS_MS = 180;
    QPixmap was;        // the picture a retake cross-fades from
    double mix = 1.0;   // 0 = `was`, 1 = the new picture
    bool shooting = false;   // being photographed: it paints nothing
    bool live = false;       // accepted: a bare scrim over the live window
    bool stale = false;      // the window changed behind it since its photograph
  };

}  // namespace stencil::support
