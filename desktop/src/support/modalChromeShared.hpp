#pragma once
// The modal shell's shared metrics and flight claim, private to the modalChrome*.cpp TUs.
#include <QHBoxLayout>
#include "modalReveal.hpp"
#include <QDialog>
#include <QWidget>

namespace stencil::gui {

  // Browser .settings-header / -body / -footer padding (14px 18px / 14px 18px / 12px 18px).
  inline constexpr int PAD_X = 18;
  inline constexpr int HEADER_PAD_Y = 12;
  inline constexpr int BODY_PAD_Y = 14;
  inline constexpr int FOOTER_PAD_Y = 12;
  // Below this the hint takes its own line above the buttons (browser: `flex: 1 1 110px`).
  inline constexpr int FOOTER_HINT_MIN_W = 110;

  // Every visible non-hint widget's minimum plus the gaps — what the reservation and the wrap size against.
  inline int footerButtonsWidth(const QHBoxLayout* row, const QWidget* hint, int* count = nullptr) {
    int need = 0, items = 0;
    for (int i = 0; i < row->count(); ++i) {
      QWidget* w = row->itemAt(i)->widget();
      if (!w || w == hint || w->isHidden()) continue;
      need += w->minimumSizeHint().width();
      ++items;
    }
    if (count) *count = items;
    return need + row->spacing() * qMax(0, items - 1);
  }

  // Claim the flight (support/modalReveal.hpp) so the watcher leaves it alone, aiming the CLOSE at `closeRect`.
  inline void armFlight(QDialog& dlg, const FlightAnchors& flight) {
    if (!flight.openRect.isValid() && !flight.closeRect.isValid()) return;
    const QRect from = flight.openRect.isValid() ? flight.openRect
                                                 : support::gestureAnchorRect();
    support::revealDialog(dlg, nullptr, from, flight.closeRect);
  }

}  // namespace stencil::gui

