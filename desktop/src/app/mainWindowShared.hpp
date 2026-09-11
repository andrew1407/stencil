#pragma once

// Constants and helpers shared by the MainWindow translation units — mainWindow.cpp and
// the mainWindow*.cpp partials split out of it. Declarations only: the bodies live in
// mainWindowShared.cpp, so no includer compiles them.

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <functional>

class QJsonObject;
class QObject;

namespace stencil::gui {

  class DisintegrateOverlay;

  // saveState/restoreState version — bumped when the toolbar rows change, so a state
  // saved against the old set is discarded instead of restoring the old row breaks.
  // WITHOUT this bump a layout saved by an older build is accepted verbatim, pinning
  // the old row extents — which left a new section past the end of a restored row,
  // invisible behind the toolbar's "»".
  inline constexpr int kToolbarLayoutVersion = 6;   // v6: the four tool rows folded into one wrapping run

  // How long the chat takes to LEAVE: the dock's edge slide plus a floating window's
  // flight into the icon. The compact popover waits this out so the two never overlap
  // (chatPanel.js closeMs parity).
  inline constexpr int kChatSlideOutMs = 340;
  inline constexpr int kWindowDismissMs = 240;
  // The panel's two toggle chevrons read as one button, so they share a box (selectionPanel
  // kToggleBox/kToggleGlyph).
  inline constexpr int kPanelToggleBox = 24;
  inline constexpr int kPanelToggleGlyph = 15;
  // Gap between the floating chevron and the window edge: centres the 24px box in
  // the 45px collapsed band ((45 − 24) / 2) instead of hugging the edge — browser
  // parity: the coord panel's 36px collapsed rail centres its button.
  inline constexpr int kPanelToggleInset = 10;
  inline constexpr int kPanelToggleTop = 5;     // and its drop below the toolbar edge
  // Canvas right margin while the panel is COLLAPSED: kCentralSideMargin alone left the
  // floating re-open chevron sitting on top of the canvas's own scrollbar, since the
  // canvas has no dock gap to borrow room from then. Wide enough to clear the chevron
  // (kPanelToggleBox + kPanelToggleInset) plus the scrollbar's own width (theme.cpp).
  inline constexpr int kCanvasRightMarginCollapsed = 45;
  // The toolbar/panel extent slides, and the dust flights that ride them — paced off
  // the browser's own fold (--fold-ms / --fold-out-ms, css/animations.css).
  // COLLAPSING is deliberately the slower half: a fold has no icon to shrink into.
  inline constexpr int kFoldMs = 420;
  inline constexpr int kFoldOutMs = 630;
  inline constexpr int kFoldDustInMs = 450;
  inline constexpr int kFoldDustOutMs = 585;   // browser FOLD_DUST_OUT_MS, same 1.5x ratio
  // Idle pause after which the f(x,y) fields commit themselves. Mirrors the browser's
  // COMMIT_DEBOUNCE_MS (browser/js/ui/numericInput.js), which its formula pair shares.
  inline constexpr int kFormulaCommitMs = 1200;

  // GET an http(s) URL's bytes (no auth) with a 10s deadline, delivering them to `done`
  // on the event loop (empty on any refusal/failure/timeout). `ctx` owns the transient
  // manager — if `ctx` is destroyed mid-fetch the reply is severed, and `done` never
  // runs on a dangling caller (a safe no-op).
  void fetchUrlBytesAsync(QObject* ctx, const QString& url, std::function<void(QByteArray)> done);

  // Read a layout's saved filter/tint (legacy blackAndWhite → "bw"); an absent or empty
  // layout yields "none" + the default tint.
  void parseLayoutFilter(const QJsonObject& layout, const QString& defTint,
                         QString& filter, QString& tint);

  // Wrap an extent-slide tick so the dust overlay is re-raised one turn AFTER each
  // frame: every resize is a QMainWindow dock-layout pass that restacks children
  // after the tick returns, so an immediate raise() loses that race. One pending
  // deferred raise at a time, not a fresh singleShot per tick.
  std::function<void(int)> pinAndRaiseDust(std::function<void(int)> pin,
                                           const QPointer<gui::DisintegrateOverlay>& fx);

}  // namespace stencil::gui
