#pragma once
// The links dialog's row shim and preview box, private to the linksDialog*.cpp TUs.
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

namespace stencil::gui {

  // One editable link row: a line edit plus "open in browser" (↗) and clear (✕).
  inline QHBoxLayout* linkRow(QLineEdit* edit, QPushButton* openBtn, QPushButton* clearBtn) {
    auto* row = new QHBoxLayout;
    row->addWidget(edit, 1);
    row->addWidget(openBtn);
    row->addWidget(clearBtn);
    return row;
  }

  inline constexpr int PREVIEW_MAX_W = 440;  // preview is scaled to fit this box,
  inline constexpr int PREVIEW_MAX_H = 300;  // keeping aspect ratio (browser parity).

}  // namespace stencil::gui
