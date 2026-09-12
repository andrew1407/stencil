#pragma once
// The crop preview's handle metrics, shade and screen fit, private to the cropDialog*.cpp TUs.
#include <QColor>
#include <QWidget>
#include <QScreen>
#include <QGuiApplication>
#include <QRect>
#include <QSize>

namespace stencil::gui {

  // Browser crop-handle: a 14px accent disc inside a 2px white ring (cropModal.js
  // .crop-handle) — drawn as an r=8 ellipse under a 2px pen, so the ring's outer
  // edge lands at r=9 and the disc keeps its 14px.
  inline constexpr int kHandle = 8;
  // The image sits this far inside the widget, so a handle on the image edge draws
  // whole instead of being sliced in half (browser: handles live in the UNclipped stage).
  inline constexpr int kInset = kHandle + 2;
  inline const QColor kCropAccent(0x4d, 0xa3, 0xff);   // #4da3ff, the browser's crop blue
  inline constexpr int kShadeAlpha = 115;                   // rgba(0,0,0,0.45)
  inline constexpr int kMinDispW = 760;  // the preview fit box never shrinks below this…
  inline constexpr int kMinDispH = 540;
  inline constexpr int kMinDialogW = 640;   // the dialog's own floor
  inline constexpr int kScreenMargin = 20;  // …but the window always keeps this much screen around it

  // The screen the dialog lands on — its parent window's (a dialog centres over its
  // parent), else the primary — as LOGICAL px: availableGeometry is device-independent
  // on every platform, and leaves out the menu bar / dock / taskbar.
  inline QRect screenAvail(const QWidget* w) {
    const QWidget* top = w ? w->window() : nullptr;
    if (top && top->parentWidget()) top = top->parentWidget()->window();
    const QScreen* screen = top ? top->screen() : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
  }

  // The browser's preview box (cropModal.js #crop-image-el: max-width calc(96vw - 60px),
  // max-height calc(82vh - 180px)) taken of that screen — never below 760×540, so a
  // small screen keeps a usable box (fitToScreen still caps the window itself).
  inline QSize previewFitBox(const QRect& avail) {
    return QSize(qMax(kMinDispW, qRound(avail.width() * 0.96) - 60),
                 qMax(kMinDispH, qRound(avail.height() * 0.82) - 180));
  }

}  // namespace stencil::gui
