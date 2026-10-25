#pragma once
#include <QSize>

// A colour well: the 32x16 chip in its framed button (browser .vs-color / <input type="color">).
class QAbstractButton;
class QColor;

namespace stencil::gui {

  // `withHex` writes the hex beside the chip (browser .vs-color parity).
  void setColorSwatch(QAbstractButton* btn, const QColor& color,
                      const QSize& size = QSize(46, 26), bool withHex = false);

}
