#pragma once
#include <QDialogButtonBox>
#include <QString>

// Qt-coupled helpers shared across the dialogs/widgets; never core/ (GUI-free).
class QAbstractButton;
class QColor;
class QComboBox;
class QDialog;
class QWidget;

namespace stencil::gui {

  // DontUseNativeDialog, so it gets the reveal flight and centering a native panel
  // cannot. Empty string on Cancel, as getSaveFileName().
  QString showSaveDialog(QWidget* parent, const QString& title,
                         const QString& suggested, const QString& filter);

  QDialogButtonBox* makeButtonBox(QDialog* parent,
                                  QDialogButtonBox::StandardButtons buttons);

  // No platform icon; defaults to No.
  bool confirmYesNo(QWidget* parent, const QString& title, const QString& text);

  // Theme-INDEPENDENT: the re-open chevron overlays the canvas image, not a themed surface.
  QString panelToggleQss();

  // Browser: `#toggle-controls .ic` in animations.css. A second call supersedes an
  // in-flight spin; the animation dies with the button.
  void spinIcon(QAbstractButton* btn, const QString& name, const QColor& color, int size,
                qreal fromDeg, qreal toDeg, int ms);

  // `px` is LOGICAL pixels; the PNG is rasterised at the device pixel ratio for Retina.
  // `dpr` 0 = ask qApp (test seam: offscreen is always 1x). Empty for an unknown glyph.
  QString inlineIconHtml(const QString& name, const QColor& color, int px,
                         const QString& style = QString(), qreal dpr = 0);

  // Compact icon+label popups; browser twin: .project-menu / .chat-row-menu.
  void fitMenuWidth(class QMenu& menu);
  void compactIconMenu(class QMenu& menu);

  // `withHex` writes the hex beside the chip (browser .vs-color parity).
  void setColorSwatch(QAbstractButton* btn, const QColor& color,
                      const QSize& size = QSize(46, 26), bool withHex = false);

  // Labels "<name> (<w> × <h> <unit>)", ≤ 2 decimals trimmed (the browser dropdown's
  // contract); item DATA is the canonical "custom"/"A4". Re-invoking re-renders labels only.
  void fillPageSizeCombo(QComboBox* combo, bool includeCustom,
                         const QString& units = QStringLiteral("cm"));

}
