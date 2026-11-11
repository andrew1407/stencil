#pragma once
// Click-outside dismissal of a modal and its STENCIL_MODAL_LOG trace: modalDismiss.cpp everywhere,
// modalDismissMac.mm the native half. Reached through modalReveal.hpp, which includes this.
#include <QString>

namespace stencil::support {

  // Click-outside dismissal (browser ui/base.js). Qt hands a modal's blocked windows
  // nothing, so this watches the press before QApplication drops it. Idempotent.
  void installModalDismiss();

  // On macOS a blocked window gets no QEvent at all, so modalDismissMac.mm reads the native press.
  void installModalDismissNative();

  // Written to the file named by STENCIL_MODAL_LOG; stderr is unreadable under LaunchServices.
  // The variable is read once; a caller asks modalDismissLogOn() before it formats a line.
  bool modalDismissLogOn();
  void modalDismissLog(const QString& line);

  inline constexpr const char* NO_OUTSIDE_DISMISS_PROPERTY = "stencilNoOutsideDismiss";

}  // namespace stencil::support
