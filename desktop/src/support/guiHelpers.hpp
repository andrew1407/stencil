#pragma once
#include <QDialogButtonBox>
#include <QString>

// Small Qt-coupled scaffolding helpers shared across the GUI dialogs/widgets.
// Qt-only by design — must NOT live in core/ (which is GUI-free + compiled to
// WebAssembly). Verified by the stencil build, not doctest.
class QAbstractButton;
class QColor;
class QComboBox;
class QDialog;
class QWidget;

namespace stencil::gui {

  // A real save panel, not the QFileDialog::getSaveFileName() convenience — that
  // convenience runs the OS's own native panel, which can be neither dusted nor
  // centered by us (modalReveal.cpp's DialogRevealFilter skips a still-native
  // QFileDialog for exactly that reason). DontUseNativeDialog makes this one
  // Qt-rendered content like every other dialog, so it gets the same reveal flight
  // and the same parent-centered placement. Empty string on Cancel, same as
  // getSaveFileName(). Shared by dataExportController's saveImageFile and
  // mainWindow's saveProjectFileAs.
  QString showSaveDialog(QWidget* parent, const QString& title,
                         const QString& suggested, const QString& filter);

  // Create a standard QDialogButtonBox parented to `parent` and wire its
  // accepted()->accept() / rejected()->reject() to the dialog. Replaces the
  // identical 3-line pattern in settings/shortcuts/info dialogs.
  QDialogButtonBox* makeButtonBox(QDialog* parent,
                                  QDialogButtonBox::StandardButtons buttons);

  // Yes/No confirmation with NO platform icon — the compact "just the question and
  // two buttons" shape the app's other modals use, instead of QMessageBox::question's
  // oversized ? glyph. Defaults to No. True when the user confirmed.
  bool confirmYesNo(QWidget* parent, const QString& title, const QString& text);

  // Stylesheet for the floating "re-open panel" chevron: a rounded square with a subtle fill +
  // border and a hover lift. Deliberately theme-INDEPENDENT — it overlays the CANVAS, not a
  // themed surface, so it has to read against whatever image is under it. Its twin, the panel
  // header's collapse chevron, sits on the panel and is themed in theme.cpp instead.
  QString panelToggleQss();

  // Turn the named glyph from `fromDeg` to `toDeg` over `ms` as `btn`'s icon, re-rendering
  // it each frame. The collapse chevrons spin half a turn with their panel instead of
  // blinking to the opposite glyph (browser: `#toggle-controls .ic` in animations.css).
  // A second call supersedes an in-flight spin; the animation dies with the button.
  void spinIcon(QAbstractButton* btn, const QString& name, const QColor& color, int size,
                qreal fromDeg, qreal toDeg, int ms);

  // An <img> element carrying the named iconSet glyph, tinted `color` and shown at
  // `px` LOGICAL pixels — for the rich-text QLabels that mix a glyph into a line of
  // text (the image-size line's incognito tag, the toasts). The PNG is rasterised at
  // the device pixel ratio and displayed at `px`, so it stays crisp on Retina;
  // `dpr` (0 = ask qApp) is the same test seam iconSet::themedIcon takes, since an
  // offscreen screen is always 1x. `style` rides on the element (e.g. vertical-align).
  // Empty string for an unknown glyph — a typo degrades to "no icon", never markup
  // pointing at nothing.
  QString inlineIconHtml(const QString& name, const QColor& color, int px,
                         const QString& style = QString(), qreal dpr = 0);

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
