#pragma once
#include <QDialogButtonBox>

// Small Qt-coupled scaffolding helpers shared across the GUI dialogs/widgets.
// Qt-only by design — must NOT live in core/ (which is GUI-free + compiled to
// WebAssembly). Verified by the stencil_gui build, not doctest.
class QAbstractButton;
class QColor;
class QDialog;

namespace stencil::gui {

  // Create a standard QDialogButtonBox parented to `parent` and wire its
  // accepted()->accept() / rejected()->reject() to the dialog. Replaces the
  // identical 3-line pattern in settings/shortcuts/info dialogs.
  QDialogButtonBox* makeButtonBox(QDialog* parent,
                                  QDialogButtonBox::StandardButtons buttons);

  // Paint a flat 20×20 color chip as `btn`'s icon so the swatch reads as its
  // current color (the browser uses <input type=color>). No-op on a null button.
  // Shared by mainWindow::updateColorSwatch + selectionPanel::setSwatchColor.
  void setColorSwatch(QAbstractButton* btn, const QColor& color);

}
