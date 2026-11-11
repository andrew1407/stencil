#pragma once
#include "motionPrefs.hpp"

#include <QDialog>
#include <QList>
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
    static QList<QWidget*> aboveChildren(QWidget* host);

    // Downscale-then-upscale: a cheap blur whose cost does not grow with the window.
    static QPixmap blurred(const QPixmap& shot, int radiusPx);

    // Covers `host` and goes when `dlg` closes. Null when the switch is off (side-by-side windows
    // dim nothing), or when there is nothing worth photographing.
    static ModalBackdrop* behind(QDialog* dlg, QWidget* host);
    // Re-photographs the host after a live restyle (or the scrim keeps the palette it opened on), cross-faded in.
    static void retakeOn(QWidget* host, const QList<QWidget*>& skip = {});
    // Retaken once a change behind it has landed; never per frame, which lagged the desktop (user report).
    static void followFor(QWidget* host, int ms);
    // `w` without any backdrop dimming it: a dialog's flight picture, which flew home dark (user report).
    static QPixmap cleanShot(QWidget* w);

    // EVERY app window behind the dialog, its parent's included: a floating chat dock or a second window stays undimmed otherwise.
    static void behindAll(QDialog* dlg);
    // Every backdrop up, fading away (Multiple windows switched on: side-by-side windows dim nothing).
    static void clearAll();
    // The dim and the blur come up and go together, as one fading surface.
    void fadeTo(double to, int ms, bool thenDrop);
    double opacityNow() const { return level; }

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    explicit ModalBackdrop(QWidget* host);
    // The blurred window under its scrim, composed once rather than blended per frame.
    void setPhoto(const QPixmap& window);
    static QList<ModalBackdrop*> on(QWidget* host);
    // A skipped backdrop paints nothing rather than hiding: a hide/show flashed the window undimmed (user report).
    static QPixmap photograph(QWidget* host, const QList<QWidget*>& skip);
    void setShooting(bool on) { shooting = on; setAttribute(Qt::WA_OpaquePaintEvent, !on && opaque); }
    void crossTo(const QPixmap& window);
    void setOpaque(bool on);
    bool darkBelow() const;   // another backdrop here, fully dark for a dialog still up

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
