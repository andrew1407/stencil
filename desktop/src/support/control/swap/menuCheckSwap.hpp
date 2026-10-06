#pragma once
// A checkable menu row's mark forms and scatters like a checkbox's (controlSwap.hpp
// swapCheckIndicator; browser control/swap.js swapCheckGlyph): only the pixels the toggle changed
// take part, so the row's label and highlight never move.
#include <QList>

#include <functional>

class QAction;
class QMenu;

namespace stencil::gui {

  // Runs `apply` (the toggle), then dusts each of `rows` whose look it changed while `menu` is open.
  void toggleMenuRowsWithDust(QMenu* menu, const QList<QAction*>& rows,
                              const std::function<void()>& apply);

}  // namespace stencil::gui
