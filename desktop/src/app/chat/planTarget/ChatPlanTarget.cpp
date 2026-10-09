#include "ChatPlanTarget.hpp"

#include "MainWindow.hpp"
#include "ChatSessionController.hpp"
#include "mainWindowHelpers.hpp"
#include "../../../canvas/CanvasWidget.hpp"
#include "../../../model/imageTurn.hpp"
#include "../../../support/theme/theme.hpp"
#include "colorNames.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>

namespace stencil::gui {

  bool ChatPlanTarget::hasImage() const { return w.canvas->hasImage(); }
  bool ChatPlanTarget::isVideoInput() const { return !w.chatSession->chatVideoPath.isEmpty(); }
  QSize ChatPlanTarget::effectiveOriginalSize() const {
    return model::turnedSize(w.canvas->getOriginalImage().size(), w.canvas->getRotationQuarters());
  }
  QSize ChatPlanTarget::workingSize() const { return w.canvas->getImage().size(); }
  core::PageSize ChatPlanTarget::pageCm() const {
    return naturalPageCm(w.pageSizeValue(), w.settings.customPageWidth,
                         w.settings.customPageHeight);
  }
  bool ChatPlanTarget::applyCropRect(const core::CropRect& rect) {
    const core::PageSize page = pageCm();
    w.canvas->setPageCm(page.width, page.height);
    w.canvas->applyCrop(rect, /*recalc=*/true);
    return true;
  }
  void ChatPlanTarget::rotateQuarter(bool clockwise) { w.canvas->rotateImage(clockwise); }
  // Mode and tint land as ONE undo step, measured against the step on screen.
  void ChatPlanTarget::setImageFilter(const QString& mode, const QString& tintHex) {
    if (!tintHex.isEmpty()) w.applyTintColor(QColor(tintHex), /*asUndoStep=*/false);
    w.applyImageFilter(mode);  // syncs toolbar combo + context radios + persists
  }
  void ChatPlanTarget::setLayoutLines(const core::Lines& lines) {
    w.canvas->setLines(lines);
  }
  void ChatPlanTarget::commitLayoutLines(const core::Lines& lines) {
    w.canvas->commitLines(lines);
  }
  std::optional<core::EditorMemento> ChatPlanTarget::captureEdit() const { return w.canvas->memento(); }
  core::FormulaContext ChatPlanTarget::formulaContext() const { return w.parts.view.formulaContext(); }
  void ChatPlanTarget::setFormula(QChar axis, const QString& expr) {
    if (!expr.isEmpty() && w.tools.allowFormulas && !w.tools.allowFormulas->isChecked())
      w.tools.allowFormulas->setChecked(true);  // shows the inputs + persists
    QLineEdit* edit = axis == QLatin1Char('x') ? w.tools.formulaX : w.tools.formulaY;
    if (!edit) return;
    edit->setText(expr);
    // An op-plan applies NOW: the typing-pause debounce is for a human at the keyboard.
    w.parts.view.validateAndApplyFormulas();
  }
  // §2 formula enabled — the toolbar checkbox is the source of truth; acts.allowFormulas follows it.
  void ChatPlanTarget::setFormulasEnabled(bool on) {
    if (w.tools.allowFormulas && w.tools.allowFormulas->isChecked() != on)
      w.tools.allowFormulas->setChecked(on);  // applies + persists via its handler
  }
  void ChatPlanTarget::setPageFormat(const QString& isoName) {
    const int idx = w.units.pageSize->findData(isoName);
    if (idx >= 0) w.units.pageSize->setCurrentIndex(idx);  // fires onPageSizeChanged
  }
  // §2 page custom dims: the SAME controls the toolbar drives.
  void ChatPlanTarget::setPageCustom(double widthCm, double heightCm) {
    const double f = w.unitFormat().factor;
    if (w.units.customW) w.units.customW->setValue(widthCm * f);
    if (w.units.customH) w.units.customH->setValue(heightCm * f);
    // The spinboxes round to their display precision; keep the model exact.
    w.settings.customPageWidth = widthCm;
    w.settings.customPageHeight = heightCm;
    const int idx = w.units.pageSize->findData(QStringLiteral("custom"));
    if (idx >= 0) w.units.pageSize->setCurrentIndex(idx);
    w.parts.view.onPageSizeChanged();  // idempotent when the combo change already fired
  }
  bool ChatPlanTarget::newBlank(const QString& color, const QString& isoName,
                                double widthCm, double heightCm, QString* err) {
    if (widthCm > 0 && heightCm > 0) setPageCustom(widthCm, heightCm);
    else if (!isoName.isEmpty()) setPageFormat(isoName);
    const auto rgba = core::parseColor(color.toStdString());
    if (!rgba) {
      if (err) *err = QStringLiteral("blank: unknown colour \"%1\"").arg(color);
      return false;
    }
    const core::SizePx px = core::defaultBlankSizePx(pageCm(), 96.0);
    w.parts.sourceOpener.createBlankImage(QColor(rgba->r, rgba->g, rgba->b), px.width, px.height);
    return true;
  }
  // §2 undo/redo
  int ChatPlanTarget::stepHistory(bool redo, int steps) {
    int done = 0;
    for (; done < steps; ++done) {
      if (redo ? !w.canvas->canRedo() : !w.canvas->canUndo()) break;
      if (redo) w.canvas->redo();
      else w.canvas->undo();
    }
    if (done > 0) w.refreshActions();
    return done;
  }
  void ChatPlanTarget::setTheme(const QString& mode) {
    Settings s = w.settings;
    s.themeMode = mode;
    w.applySettings(s, true);  // the settings-dialog apply path
  }
  void ChatPlanTarget::setAccent(const QString& hex) {
    Settings s = w.settings;
    s.accentColor = hex;
    w.applySettings(s, true);  // same as the logo colour picker
  }
  // §10 accent preset: the preset KEY is what the Settings dropdown / logo cycle store. Unknown name = note+skip.
  void ChatPlanTarget::setAccentPreset(const QString& preset, QString* note) {
    const QString want = preset.trimmed().toLower();
    for (const auto& p : accentPresets()) {
      if (p.key == want) {
        Settings s = w.settings;
        s.accentColor = p.key;
        w.applySettings(s, true);
        return;
      }
    }
    if (note) *note = QStringLiteral("unknown accent preset \"%1\"").arg(preset);
  }
  void ChatPlanTarget::setDefaultLineStyle(const llm::Action& a) {
    if (!a.color.isEmpty()) {
      QColor c(a.color);  // hex or CSS name (validated upstream)
      if (!c.isValid())
        if (const auto rgba = core::parseColor(a.color.toStdString()))
          c = QColor(rgba->r, rgba->g, rgba->b);
      if (c.isValid()) {
        // The points keep their colour unless the plan names one (§10 pointColor, below).
        if (w.settings.defaultPointColor.isEmpty()) w.settings.defaultPointColor = w.settings.defaultColor;
        w.tools.lineColorValue = c;
        w.updateColorSwatch(w.tools.lineColorBtn, c);
        w.settings.defaultColor = c.name(QColor::HexRgb);
        w.parts.styleControls.onLineStyleControlChanged();
      }
    }
    // §10 widening: pointColor ("" = follow the stroke).
    if (a.pointColorSet) {
      w.settings.defaultPointColor = a.pointColor;
      if (w.tools.pointColorBtn)
        w.updateColorSwatch(w.tools.pointColorBtn, w.parts.styleControls.effectiveDefaultPointColor());
      w.parts.styleControls.onLineStyleControlChanged();
    }
    if (!a.drawMode.isEmpty()) {
      w.canvas->setDrawMode(a.drawMode == QLatin1String("rect")
                                  ? CanvasWidget::DrawMode::RECT
                                  : CanvasWidget::DrawMode::LINE);
      w.persistSettings();
    }
    if (a.thickness > 0 && w.tools.lineThickness) w.tools.lineThickness->setValue(a.thickness);
    if (a.pointSize > 0 && w.tools.pointSize) w.tools.pointSize->setValue(a.pointSize);
    if (!a.style.isEmpty() && w.tools.lineStyle) {
      const int idx = w.tools.lineStyle->findData(a.style);
      if (idx >= 0) w.tools.lineStyle->setCurrentIndex(idx);
    }
  }
  void ChatPlanTarget::setUnits(const QString& value) { w.parts.view.applyUnits(value); }
  void ChatPlanTarget::setViewVisibility(int points, int lines) {
    if (points >= 0 && w.acts.showPoints) w.acts.showPoints->setChecked(points == 1);
    if (lines >= 0 && w.acts.showLines) w.acts.showLines->setChecked(lines == 1);
  }
  // §10 clear: no confirmation — a modal would stall the turn.
  void ChatPlanTarget::clearImage() { w.parts.projects.resetToBlankEditor(); }
  // §10 compare: the shared setter the toolbar combo and the View submenu drive.
  bool ChatPlanTarget::setCompare(const QString& mode, double split, QString*) {
    w.parts.styleControls.setCompareModeUi(mode);
    if (split > 0) w.canvas->setCompareSplit(split);
    return true;
  }
  bool ChatPlanTarget::setZoom(int percent, bool fit, QString*) {
    if (fit) w.fitToWindow();
    else w.setZoom(percent / 100.0);
    return true;
  }
  // §10 blankColor: blanks only (note+skip otherwise), keeps every drawn line.
  bool ChatPlanTarget::setBlankColor(const QString& color, QString* note) {
    if (w.docSource.blankColor.isEmpty() || !w.canvas->hasImage()) {
      *note = QStringLiteral("only a blank project's background can be recoloured");
      return true;
    }
    QColor c(color);  // hex or CSS name (validated upstream)
    if (!c.isValid())
      if (const auto rgba = core::parseColor(color.toStdString()))
        c = QColor(rgba->r, rgba->g, rgba->b);
    if (!c.isValid()) {
      *note = QStringLiteral("unknown colour \"%1\"").arg(color);
      return true;
    }
    w.parts.projects.applyBlankColor(c);
    return true;
  }
}  // namespace stencil::gui

