#pragma once
#include <QDialogButtonBox>
#include <QString>

// Small Qt-coupled scaffolding helpers shared across the GUI dialogs/widgets.
// Qt-only by design — must NOT live in core/ (which is GUI-free + compiled to
// WebAssembly). Verified by the stencil_gui build, not doctest.
class QAbstractButton;
class QColor;
class QComboBox;
class QDialog;

namespace stencil::gui {

  // Create a standard QDialogButtonBox parented to `parent` and wire its
  // accepted()->accept() / rejected()->reject() to the dialog. Replaces the
  // identical 3-line pattern in settings/shortcuts/info dialogs.
  QDialogButtonBox* makeButtonBox(QDialog* parent,
                                  QDialogButtonBox::StandardButtons buttons);

  // Shared stylesheet for the points-panel toggle chevrons — the header "collapse" chevron and the
  // floating "re-open" chevron — so the two read as ONE consistent button (rounded square, subtle
  // fill + border, hover lift), just mirrored/moved. Keeps both surfaces from drifting apart.
  QString panelToggleQss();

  // Paint a flat 20×20 color chip as `btn`'s icon so the swatch reads as its
  // current color (the browser uses <input type=color>). No-op on a null button.
  // Shared by mainWindow::updateColorSwatch + selectionPanel::setSwatchColor.
  void setColorSwatch(QAbstractButton* btn, const QColor& color);

  // Fill `combo` with the page-format options every selector shares: "Custom…"
  // first (when includeCustom), then the full core::pageFormatNames() series
  // (A0..A10, B0..B10, C0..C10). Labels render "<name> (<w> × <h> <unit>)" in
  // the display unit `units` ("cm" default | "in"), values rounded to at most
  // 2 decimals with trailing zeros trimmed (the label contract shared with the
  // browser dropdown). The item DATA carries the canonical value ("custom" /
  // "A4") — callers read/write via currentData/findData, never the label.
  // Re-invoking on an already-filled combo only re-renders the labels in place
  // (selection + data untouched) — used when the display unit changes. Shared
  // by mainWindow (toolbar), settingsDialog, and linksDialog (quick crop).
  void fillPageSizeCombo(QComboBox* combo, bool includeCustom,
                         const QString& units = QStringLiteral("cm"));

}
