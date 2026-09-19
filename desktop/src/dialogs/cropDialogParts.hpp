#pragma once
// The crop preview's handle metrics, shade and screen fit, private to the CropDialog*.cpp TUs.
#include "../support/theme.hpp"
#include <QColor>
#include <QPalette>
#include <QWidget>
#include <QScreen>
#include <QGuiApplication>
#include <QRect>
#include <QSize>

namespace stencil::gui {

  // Browser crop-handle: a 14px accent disc inside a 2px white ring (cropModal.js .crop-handle) -
  // an r=8 ellipse under a 2px pen, so the ring's outer edge lands at r=9 and the disc keeps 14px.
  inline constexpr int HANDLE = 8;
  // The image sits this far inside the widget, so a handle on the image edge draws
  // whole instead of being sliced in half (browser: handles live in the UNclipped stage).
  inline constexpr int INSET = HANDLE + 2;
  // The crop box wears the app accent, like every other surface's (browser cropModal.js --accent-2,
  // extension crop.css .crop-box); QPalette::Highlight is the live accent, shaded as --accent-2 is.
  inline QColor cropAccent(const QWidget* w) {
    const QPalette pal = w ? w->palette() : QPalette();
    return accentShade(pal.color(QPalette::Highlight), pal.color(QPalette::Window).lightness() < 128);
  }
  inline constexpr int SHADE_ALPHA = 115;                   // rgba(0,0,0,0.45)
  // The box's flight on an Album/Portrait flip (browser twin: motion/rectTween.js).
  inline constexpr int CROP_TWEEN_MS = 380;
  inline constexpr int MIN_DISP_W = 760;  // the preview fit box never shrinks below this…
  inline constexpr int MIN_DISP_H = 540;
  inline constexpr int MIN_DIALOG_W = 640;   // the dialog's own floor
  inline constexpr int SCREEN_MARGIN = 20;  // …but the window always keeps this much screen around it

  // The screen the dialog lands on (its parent window's, else the primary) as LOGICAL px:
  // availableGeometry is device-independent and leaves out the menu bar / dock / taskbar.
  inline QRect screenAvail(const QWidget* w) {
    const QWidget* top = w ? w->window() : nullptr;
    if (top && top->parentWidget()) top = top->parentWidget()->window();
    const QScreen* screen = top ? top->screen() : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
  }

  // The browser's preview box (cropModal.js #crop-image-el: max-width calc(96vw - 60px), max-height
  // calc(82vh - 180px)) of that screen - never below 760x540 (fitToScreen still caps the window).
  inline QSize previewFitBox(const QRect& avail) {
    return QSize(qMax(MIN_DISP_W, qRound(avail.width() * 0.96) - 60),
                 qMax(MIN_DISP_H, qRound(avail.height() * 0.82) - 180));
  }

}  // namespace stencil::gui
