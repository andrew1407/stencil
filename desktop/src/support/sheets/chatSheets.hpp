#pragma once
// The chat surfaces' per-widget style sheets (dock, compact composer, cards, toasts), built from
// the live palette. Browser twin: browser/css/components/chat/panel.css.
#include <QColor>
#include <QString>

#include "../theme/theme.hpp"   // Palette

namespace stencil::support {

  // The dock's own chrome under the stock skin (a skin brings qss/webcore/chat.qss instead).
  QString chatDockSheet(const gui::Palette& pal);
  // Keyed on the whole sheet: a card's own local QSS shifts its wrapped label's height.
  QString chatCardStyleSheet(const gui::Palette& pal, bool swapped);
  QString chatMenuPanelSheet();
  QString chatDropCueSheet(const QColor& accent, const QColor& fill);
  QString chatDropCueTextSheet(const QColor& accent);
  QString chatHeaderTitleSheet(const QColor& text);
  QString chatJumpButtonSheet(const gui::Palette& pal);
  QString chatStatusDotSheet(const QString& color);
  QString chatSuggestChipSheet(const gui::Palette& pal);
  QString chatThumbPreviewSheet(const QColor& bg, const QColor& line);
  // `restOpacity` 0..1, baked into the resting chip and border.
  QString chatRowMenuButtonSheet(const QColor& chip, const QColor& border, double restOpacity);
  QString chatPlacementActiveSheet(const QColor& chip);
  QString chatToastSheet(const QString& edge);

}  // namespace stencil::support
