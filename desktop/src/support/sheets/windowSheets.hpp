#pragma once
// The editor window's per-widget style sheets: toolbar labels and chips, the project name field,
// the colour swatches, the context menu's toggles and the canvas tooltip. The app-wide sheet is
// resources/qss/app/; these are the pieces one widget wears on its own.
#include <QColor>
#include <QString>

#include "../theme/theme.hpp"   // Palette

namespace stencil::support {

  QString statusHintSheet();
  // `pt` = the 9.5px section caption in points at the label's dpi.
  QString sectionLabelSheet(double pt);
  QString logoButtonSheet();
  QString formulaErrorSheet();
  QString compareLabelSheet();
  QString coordStatusSheet();
  QString multiSelectLabelSheet();
  QString viewToggleSheet();
  QString imageSizeInfoSheet(const QString& ink);
  QString contextToggleSheet(const QColor& text, const QString& checkPath, const QString& dotPath);
  QString projectNameEditingSheet(const QString& fg, const QColor& accent);
  QString projectNameRestingSheet(const QString& fg, const QColor& accent);
  // `labelled`: a captioned chip keeps 6px side padding; `skin` squares its corners.
  QString colorSwatchSheet(const gui::Palette& pal, bool labelled, bool skin);
  QString faceLeftPadSheet(int px);
  QString canvasTooltipSheet();

}  // namespace stencil::support
