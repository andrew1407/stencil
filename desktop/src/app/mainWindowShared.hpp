#pragma once

// Constants and helpers shared by the MainWindow*.cpp partials; bodies live in
// mainWindowShared.cpp.

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <functional>

class QJsonObject;
class QObject;

namespace stencil::gui {

  class DisintegrateOverlay;

  // saveState/restoreState version — bump when the toolbar rows change, or a saved layout pins the
  // old row breaks.
  inline constexpr int TOOLBAR_LAYOUT_VERSION = 6;   // v6: the four tool rows folded into one wrapping run

  // How long the chat takes to leave; the compact popover waits this out (chatPanel.js closeMs
  // parity).
  inline constexpr int CHAT_SLIDE_IN_MS = 510;
  // Leaving is 1.5x quicker than arriving: a panel you dismissed should be gone.
  inline constexpr int CHAT_SLIDE_OUT_MS = 340;
  inline constexpr int WINDOW_DISMISS_MS = 240;
  // Shared with selectionPanel TOGGLE_BOX/TOGGLE_GLYPH.
  inline constexpr int PANEL_TOGGLE_BOX = 24;
  inline constexpr int PANEL_TOGGLE_GLYPH = 15;
  // (45 − 24) / 2 centres the box in the collapsed band (browser: the coord panel's collapsed
  // rail).
  inline constexpr int PANEL_TOGGLE_INSET = 10;
  inline constexpr int PANEL_TOGGLE_TOP = 5;     // and its drop below the toolbar edge
  // Clears the floating chevron (PANEL_TOGGLE_BOX + PANEL_TOGGLE_INSET) plus the scrollbar width
  // (theme.cpp) while the panel is collapsed.
  inline constexpr int CANVAS_RIGHT_MARGIN_COLLAPSED = 45;
  // Paced off the browser's fold (--fold-ms / --fold-out-ms, css/animations.css); collapsing is
  // the slower half.
  inline constexpr int FOLD_MS = 420;
  inline constexpr int FOLD_OUT_MS = 630;
  inline constexpr int FOLD_DUST_IN_MS = 450;
  inline constexpr int FOLD_DUST_OUT_MS = 585;   // browser FOLD_DUST_OUT_MS, same 1.5x ratio
  // Mirrors the browser's COMMIT_DEBOUNCE_MS (browser/js/ui/control/numericInput.js).
  inline constexpr int FORMULA_COMMIT_MS = 1200;

  // GET with a 10s deadline; `ctx` owns the manager — destroying it severs the reply, so `done`
  // never runs on a dangling caller.
  void fetchUrlBytesAsync(QObject* ctx, const QString& url, std::function<void(QByteArray)> done);

  // Legacy blackAndWhite → "bw"; absent yields "none" + the default tint.
  void parseLayoutFilter(const QJsonObject& layout, const QString& defTint,
                         QString& filter, QString& tint);

  // Re-raise the dust overlay one turn after each frame: the dock-layout pass restacks children
  // after the tick. One pending raise at a time.
  std::function<void(int)> pinAndRaiseDust(std::function<void(int)> pin,
                                           const QPointer<gui::DisintegrateOverlay>& fx);

}  // namespace stencil::gui
