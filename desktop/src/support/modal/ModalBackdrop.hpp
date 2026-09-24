#pragma once
#include "motionPrefs.hpp"

#include <QApplication>
#include <QDialog>
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

    // Covers `host` and goes when `dlg` closes. Null when the switch is off, or when there
    // is nothing worth photographing.
    static ModalBackdrop* behind(QDialog* dlg, QWidget* host) {
      if (!modalBackdrop() || !dlg || !host) return nullptr;
      if (host->width() < 8 || host->height() < 8) return nullptr;
      const QPixmap shot = photograph(host, {});
      if (shot.isNull()) return nullptr;
      auto* bd = new ModalBackdrop(host);
      bd->shot = shot;
      bd->show();
      bd->raise();
      for (QWidget* w : aboveChildren(host)) w->raise();
      bd->fadeTo(1.0, FADE_IN_MS, /*thenDrop=*/false);
      // It holds through the dialog's flight back, as the browser's does, and leaves on its own.
      QObject::connect(dlg, &QDialog::finished, bd, [bd] { bd->fadeTo(0.0, FADE_OUT_MS, true); });
      return bd;
    }

    // Re-photographs the host after a live restyle, or the scrim keeps the palette it opened on.
    static void retakeOn(QWidget* host, const QList<QWidget*>& skip = {}) {
      if (!host) return;
      for (QWidget* w : host->findChildren<QWidget*>(QStringLiteral("modalBackdrop"),
                                                     Qt::FindDirectChildrenOnly)) {
        auto* bd = static_cast<ModalBackdrop*>(w);
        const QPixmap shot = photograph(host, skip + QList<QWidget*>{bd});
        if (!shot.isNull()) { bd->shot = shot; bd->update(); }
      }
    }

    // EVERY app window behind the dialog, not just its parent's: the assistant settings
    // parent to the chat dock while it floats, and only that dock would dim otherwise.
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
      anim->setDuration(ms);
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
      QPainter p(this);
      p.setOpacity(level);
      if (!shot.isNull()) p.drawPixmap(rect(), shot);
      p.fillRect(rect(), QColor(0, 0, 0, int(255 * SCRIM_ALPHA)));
    }

   private:
    static QPixmap photograph(QWidget* host, const QList<QWidget*>& skip) {
      QList<QWidget*> hidden;
      for (QWidget* w : skip + aboveChildren(host))
        if (w && w->isVisible()) { w->setVisible(false); hidden.append(w); }
      const QPixmap shot = host->grab();
      for (QWidget* w : hidden) w->setVisible(true);
      return blurred(shot, BLUR_PX);
    }

    explicit ModalBackdrop(QWidget* host) : QWidget(host) {
      setObjectName(QStringLiteral("modalBackdrop"));
      setAttribute(Qt::WA_TransparentForMouseEvents);   // the dialog is modal; this only paints
      setGeometry(host->rect());
    }
    QPixmap shot;
    double level = 0.0;   // 0 = the window as it is, 1 = fully dimmed and blurred
    QPointer<QVariantAnimation> fade;
  };

}  // namespace stencil::support
