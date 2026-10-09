// The selected-line bar's colour wells: the picker each opens (a double-click resets the line
// well, browser dblReset.js 'sel-color') and the 0-255 opacity box beside each, the colour's own
// alpha byte, typed (browser panel/selectionPanel.js .alpha-input, writeColorPair).
#include "SelectedLineBar.hpp"
#include "cssColor.hpp"
#include "../../support/guiHelpers.hpp"
#include "../../support/control/dblReset.hpp"
#include "colorDrag.hpp"
#include "../../support/control/numericInput.hpp"
#include "../../support/modal/modalReveal.hpp"

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

  void SelectedLineBar::setLineColorDefault(std::function<QColor()> source) {
    lineColorDefault = std::move(source);
  }

  void SelectedLineBar::setPointColorDefault(std::function<QColor()> source) {
    pointColorDefault = std::move(source);
  }

  // Early-returns while showLine repopulates (updating), the browser's selectedLineIdx guard.
  // Cancel hands the original back through pickColorAnimated.
  void SelectedLineBar::wireColorWell(QPushButton* well, QColor& current, const char* title,
                                      std::function<void(const QString&, bool)> send,
                                      std::function<QColor()> resetTo) {
    const auto settle = [this, well, &current, send](const QColor& c) {
      current = c;
      setColorSwatch(well, current);
      syncAlpha();   // a picked alpha shows in the well's box
      send(cssName(current), false);
    };
    const auto open = [this, well, &current, title, send, settle] {
      if (updating) return;
      const QColor c = support::pickColorAnimated(
          current, this, title, well, QRect(),
          [well, send](const QColor& p) { setColorSwatch(well, p); send(cssName(p), true); },
          /*withAlpha=*/true);
      if (c.isValid()) settle(c);
    };
    // A well carries its alpha byte, so another well hands over the whole RGBA.
    support::installColorDrag(well, {[&current] { return current; }, settle, /*alpha=*/true,
                                     [this] { return !updating; }});
    if (!resetTo) { connect(well, &QPushButton::clicked, this, open); return; }
    support::wireColorChip(well, open, [this, settle, resetTo] { if (!updating) settle(resetTo()); });
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
