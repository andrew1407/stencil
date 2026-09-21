#include "ChatPlanTarget.hpp"

#include "MainWindow.hpp"
#include "../support/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "mainWindowHelpers.hpp"
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "../canvas/CanvasWidget.hpp"
#include "../net/connectionStore.hpp"
#include "../net/ServerClient.hpp"
#include "../support/displayName.hpp"
#include "../support/Notifications.hpp"
#include "../support/theme.hpp"
#include "colorNames.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QJsonObject>
#include <QLineEdit>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>

namespace stencil::gui {

  bool ChatPlanTarget::hasImage() const { return w.canvas->hasImage(); }
  bool ChatPlanTarget::isVideoInput() const { return !w.chatVideoPath.isEmpty(); }
  QSize ChatPlanTarget::effectiveOriginalSize() const {
    return w.canvas->effectiveOriginalImage().size();
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
  void ChatPlanTarget::setImageFilter(const QString& mode, const QString& tintHex) {
    if (!tintHex.isEmpty()) w.applyTintColor(QColor(tintHex));
    w.applyImageFilter(mode);  // syncs toolbar combo + context radios + persists
  }
  void ChatPlanTarget::setLayoutLines(const core::Lines& lines) {
    w.canvas->setLines(lines);
  }
  void ChatPlanTarget::commitLayoutLines(const core::Lines& lines) {
    w.canvas->commitLines(lines);
  }
  bool ChatPlanTarget::captureEdit(llm::EditState& out) const {
    out.valid = true;
    out.crop = w.canvas->getCropRect();
    out.filterMode = w.canvas->getImageFilter();
    out.filterTint = w.canvas->getFilterColor().name();
    out.lines = w.canvas->getLines();
    return true;
  }
  void ChatPlanTarget::setFormula(QChar axis, const QString& expr) {
    if (!expr.isEmpty() && w.allowFormulas && !w.allowFormulas->isChecked())
      w.allowFormulas->setChecked(true);  // shows the inputs + persists
    QLineEdit* edit = axis == QLatin1Char('x') ? w.formulaX : w.formulaY;
    if (!edit) return;
    edit->setText(expr);
    // An op-plan applies NOW: the typing-pause debounce is for a human at the keyboard.
    w.validateAndApplyFormulas();
  }
  // §2 formula enabled — the toolbar checkbox is the source of truth; actAllowFormulas follows it.
  void ChatPlanTarget::setFormulasEnabled(bool on) {
    if (w.allowFormulas && w.allowFormulas->isChecked() != on)
      w.allowFormulas->setChecked(on);  // applies + persists via its handler
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
    w.onPageSizeChanged();  // idempotent when the combo change already fired
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
    w.createBlankImage(QColor(rgba->r, rgba->g, rgba->b), px.width, px.height);
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
  bool ChatPlanTarget::extractFrames(const QVector<int>& indices, QString* err) {
    return w.chatExtractFrames(indices, err);
  }
  // §10 @frame: the block's own source reloaded AT that frame, so it becomes the working image.
  bool ChatPlanTarget::openSourceFrame(const QString& spec, int frame, QString* err) {
    if (spec.isEmpty()) {
      if (err) *err = QStringLiteral("frame: this block names no source");
      return false;
    }
    QString why;
    if (w.chatLoadSource(spec, w.incognito, &why, frame)) return true;
    if (err) *err = QStringLiteral("frame: %1").arg(why);
    return false;
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
        w.lineColorValue = c;
        w.updateColorSwatch(w.lineColorBtn, c);
        w.settings.defaultColor = c.name(QColor::HexRgb);
        w.onLineStyleControlChanged();
      }
    }
    // §10 widening: pointColor ("" = follow the stroke).
    if (a.pointColorSet) {
      w.settings.defaultPointColor = a.pointColor;
      if (w.pointColorBtn)
        w.updateColorSwatch(w.pointColorBtn, w.effectiveDefaultPointColor());
      w.onLineStyleControlChanged();
    }
    if (!a.drawMode.isEmpty()) {
      w.canvas->setDrawMode(a.drawMode == QLatin1String("rect")
                                  ? CanvasWidget::DrawMode::RECT
                                  : CanvasWidget::DrawMode::LINE);
      w.persistSettings();
    }
    if (a.thickness > 0 && w.lineThickness) w.lineThickness->setValue(a.thickness);
    if (a.pointSize > 0 && w.pointSize) w.pointSize->setValue(a.pointSize);
    if (!a.style.isEmpty() && w.lineStyle) {
      const int idx = w.lineStyle->findData(a.style);
      if (idx >= 0) w.lineStyle->setCurrentIndex(idx);
    }
  }
  void ChatPlanTarget::setUnits(const QString& value) { w.applyUnits(value); }
  void ChatPlanTarget::setViewVisibility(int points, int lines) {
    if (points >= 0 && w.actShowPoints) w.actShowPoints->setChecked(points == 1);
    if (lines >= 0 && w.actShowLines) w.actShowLines->setChecked(lines == 1);
  }
  // §10 clear: no confirmation — a modal would stall the turn.
  void ChatPlanTarget::clearImage() { w.resetToBlankEditor(); }
  // §10 compare: the shared setter the toolbar combo and the View submenu drive.
  bool ChatPlanTarget::setCompare(const QString& mode, double split, QString*) {
    w.setCompareModeUi(mode);
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
    if (w.blankColor.isEmpty() || !w.canvas->hasImage()) {
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
    w.applyBlankColor(c);
    return true;
  }
}  // namespace stencil::gui

