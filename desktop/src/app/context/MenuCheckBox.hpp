#pragma once
// A check hosted in a context-menu row: the box is the stylesheet's (menus.qss), the tick painted
// here from the menu's own tick art at the device's resolution, as Show Points/Lines draw theirs
// (ThemePainterIcons.cpp). A stylesheet `image:` loads one resolution and smears on Retina.
#include <QCheckBox>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionButton>

#include "../../support/control/swap/controlSwap.hpp"

namespace stencil::gui {

  class MenuCheckBox : public QCheckBox {
   public:
    using QCheckBox::QCheckBox;

   protected:
    void paintEvent(QPaintEvent* event) override {
      QCheckBox::paintEvent(event);
      const QString art = ctl::menuTickArt(palette().color(QPalette::WindowText));
      setProperty(CHECK_TICK_ART_PROPERTY, art);   // the swap's particles are cut from the same tick
      if (!isChecked()) return;
      QStyleOptionButton opt;
      initStyleOption(&opt);
      QPainter p(this);
      ctl::paintCheckTick(p, style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, this), art);
    }
  };

}  // namespace stencil::gui
