#pragma once
// The modal shell's shared metrics and flight claim, private to the modalChrome*.cpp TUs.
#include <QHBoxLayout>
#include "modalReveal.hpp"
#include <QDialog>
#include <QWidget>

namespace stencil::gui {

  // Browser .settings-header / .settings-body / .settings-footer padding
  // (components.css: 14px 18px / 14px 18px / 12px 18px).
  inline constexpr int kPadX = 18;
  inline constexpr int kHeaderPadY = 12;
  inline constexpr int kBodyPadY = 14;
  inline constexpr int kFooterPadY = 12;
  // The narrowest the footer hint will share a row: below it the hint takes its own
  // line above the buttons (FooterWrap) — left-aligned, the buttons packed right under
  // it — rather than shrinking into a column of one-word lines. Browser twin: the
  // hint's `flex: 1 1 110px` basis under `justify-content: flex-end`.
  inline constexpr int kFooterHintMinW = 110;

  // The buttons' share of a footer row — every visible non-hint widget's minimum plus
  // the gaps between them: the metric the window's width reservation, the wrap and a
  // `width:auto` dialog all size against.
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

  // Claim the dialog's flight (support/modalReveal.hpp) so the app-wide watcher leaves it
  // alone, keeping its default origin — the press that raised it — but aiming the CLOSE
  // wherever the caller asked. Only worth claiming when there IS somewhere else to aim.
  inline void armFlight(QDialog& dlg, const FlightAnchors& flight) {
    if (!flight.openRect.isValid() && !flight.closeRect.isValid()) return;
    const QRect from = flight.openRect.isValid() ? flight.openRect
                                                 : support::gestureAnchorRect();
    support::revealDialog(dlg, nullptr, from, flight.closeRect);
  }

}  // namespace stencil::gui

