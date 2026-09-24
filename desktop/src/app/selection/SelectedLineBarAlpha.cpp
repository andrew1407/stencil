// The 0-255 opacity box beside each colour well of the selected-line bar: the colour's own
// alpha byte, typed (browser panel/selectionPanel.js .alpha-input, writeColorPair).
#include "SelectedLineBar.hpp"
#include "cssColor.hpp"
#include "../../support/guiHelpers.hpp"
#include "../../support/control/numericInput.hpp"

#include <QPushButton>
#include <QSpinBox>
#include <utility>

namespace stencil::gui {

  QSpinBox* SelectedLineBar::alphaBox(QPushButton* well, QColor& current, const QString& what,
                                      std::function<void(const QString&)> send) {
    auto* box = new ExprSpinBox(well->parentWidget());
    box->setRange(0, 255);
    box->setFixedWidth(56);   // browser .alpha-input
    box->setToolTip(what + " opacity\n0-255, the alpha byte itself: 255 is solid, 0 invisible.");
    connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, well, &current, send = std::move(send)](int v) {
              if (updating) return;
              current.setAlpha(v);
              setColorSwatch(well, current);
              send(cssName(current));
            });
    return box;
  }

  void SelectedLineBar::syncAlpha() {
    const bool was = updating;
    updating = true;
    for (const auto& [box, colour] : {std::pair{lineAlpha, &currentColor},
                                      std::pair{pointAlpha, &currentPointColor},
                                      std::pair{fillAlpha, &currentFill}})
      if (box) box->setValue(colour->alpha());
    updating = was;
  }

}  // namespace stencil::gui
