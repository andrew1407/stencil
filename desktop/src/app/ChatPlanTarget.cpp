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

  bool ChatPlanTarget::hasImage() const { return w_.canvas_->hasImage(); }
  bool ChatPlanTarget::isVideoInput() const { return !w_.chatVideoPath_.isEmpty(); }
  QSize ChatPlanTarget::effectiveOriginalSize() const {
    return w_.canvas_->effectiveOriginalImage().size();
  }
  QSize ChatPlanTarget::workingSize() const { return w_.canvas_->image().size(); }
  core::PageSize ChatPlanTarget::pageCm() const {
    return naturalPageCm(w_.pageSizeValue(), w_.settings_.customPageWidth,
                         w_.settings_.customPageHeight);
  }
  bool ChatPlanTarget::applyCropRect(const core::CropRect& rect) {
    const core::PageSize page = pageCm();
    w_.canvas_->setPageCm(page.width, page.height);
    w_.canvas_->applyCrop(rect, /*recalc=*/true);
    return true;
  }
  void ChatPlanTarget::rotateQuarter(bool clockwise) { w_.canvas_->rotateImage(clockwise); }
  void ChatPlanTarget::setImageFilter(const QString& mode, const QString& tintHex) {
    if (!tintHex.isEmpty()) w_.applyTintColor(QColor(tintHex));
    w_.applyImageFilter(mode);  // syncs toolbar combo + context radios + persists
  }
  void ChatPlanTarget::setLayoutLines(const core::Lines& lines) {
    w_.canvas_->setLines(lines);
  }
  void ChatPlanTarget::commitLayoutLines(const core::Lines& lines) {
    w_.canvas_->commitLines(lines);
  }
  bool ChatPlanTarget::captureEdit(llm::EditState& out) const {
    out.valid = true;
    out.crop = w_.canvas_->cropRect();
    out.filterMode = w_.canvas_->imageFilter();
    out.filterTint = w_.canvas_->filterColor().name();
    out.lines = w_.canvas_->lines();
    return true;
  }
  void ChatPlanTarget::setFormula(QChar axis, const QString& expr) {
    if (!expr.isEmpty() && w_.allowFormulas_ && !w_.allowFormulas_->isChecked())
      w_.allowFormulas_->setChecked(true);  // shows the inputs + persists
    QLineEdit* edit = axis == QLatin1Char('x') ? w_.formulaX_ : w_.formulaY_;
    if (!edit) return;
    edit->setText(expr);
    // An op-plan applies NOW: the typing-pause debounce is for a human at the keyboard.
    w_.validateAndApplyFormulas();
  }
  // §2 formula enabled — the toolbar checkbox is the source of truth; actAllowFormulas_ follows it.
  void ChatPlanTarget::setFormulasEnabled(bool on) {
    if (w_.allowFormulas_ && w_.allowFormulas_->isChecked() != on)
      w_.allowFormulas_->setChecked(on);  // applies + persists via its handler
  }
  void ChatPlanTarget::setPageFormat(const QString& isoName) {
    const int idx = w_.units_.pageSize->findData(isoName);
    if (idx >= 0) w_.units_.pageSize->setCurrentIndex(idx);  // fires onPageSizeChanged
  }
  // §2 page custom dims: the SAME controls the toolbar drives.
  void ChatPlanTarget::setPageCustom(double widthCm, double heightCm) {
    const double f = w_.unitFormat().factor;
    if (w_.units_.customW) w_.units_.customW->setValue(widthCm * f);
    if (w_.units_.customH) w_.units_.customH->setValue(heightCm * f);
    // The spinboxes round to their display precision; keep the model exact.
    w_.settings_.customPageWidth = widthCm;
    w_.settings_.customPageHeight = heightCm;
    const int idx = w_.units_.pageSize->findData(QStringLiteral("custom"));
    if (idx >= 0) w_.units_.pageSize->setCurrentIndex(idx);
    w_.onPageSizeChanged();  // idempotent when the combo change already fired
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
    w_.createBlankImage(QColor(rgba->r, rgba->g, rgba->b), px.width, px.height);
    return true;
  }
  // §2 undo/redo
  int ChatPlanTarget::stepHistory(bool redo, int steps) {
    int done = 0;
    for (; done < steps; ++done) {
      if (redo ? !w_.canvas_->canRedo() : !w_.canvas_->canUndo()) break;
      if (redo) w_.canvas_->redo();
      else w_.canvas_->undo();
    }
    if (done > 0) w_.refreshActions();
    return done;
  }
  bool ChatPlanTarget::extractFrames(const QVector<int>& indices, QString* err) {
    return w_.chatExtractFrames(indices, err);
  }
  // §10 @frame: the block's own source reloaded AT that frame, so it becomes the working image.
  bool ChatPlanTarget::openSourceFrame(const QString& spec, int frame, QString* err) {
    if (spec.isEmpty()) {
      if (err) *err = QStringLiteral("frame: this block names no source");
      return false;
    }
    QString why;
    if (w_.chatLoadSource(spec, w_.incognito_, &why, frame)) return true;
    if (err) *err = QStringLiteral("frame: %1").arg(why);
    return false;
  }

  void ChatPlanTarget::setTheme(const QString& mode) {
    Settings s = w_.settings_;
    s.themeMode = mode;
    w_.applySettings(s, true);  // the settings-dialog apply path
  }
  void ChatPlanTarget::setAccent(const QString& hex) {
    Settings s = w_.settings_;
    s.accentColor = hex;
    w_.applySettings(s, true);  // same as the logo colour picker
  }
  // §10 accent preset: the preset KEY is what the Settings dropdown / logo cycle store. Unknown name = note+skip.
  void ChatPlanTarget::setAccentPreset(const QString& preset, QString* note) {
    const QString want = preset.trimmed().toLower();
    for (const auto& p : accentPresets()) {
      if (p.key == want) {
        Settings s = w_.settings_;
        s.accentColor = p.key;
        w_.applySettings(s, true);
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
        w_.lineColorValue_ = c;
        w_.updateColorSwatch(w_.lineColorBtn_, c);
        w_.settings_.defaultColor = c.name(QColor::HexRgb);
        w_.onLineStyleControlChanged();
      }
    }
    // §10 widening: pointColor ("" = follow the stroke).
    if (a.pointColorSet) {
      w_.settings_.defaultPointColor = a.pointColor;
      if (w_.pointColorBtn_)
        w_.updateColorSwatch(w_.pointColorBtn_, w_.effectiveDefaultPointColor());
      w_.onLineStyleControlChanged();
    }
    if (!a.drawMode.isEmpty()) {
      w_.canvas_->setDrawMode(a.drawMode == QLatin1String("rect")
                                  ? CanvasWidget::DrawMode::RECT
                                  : CanvasWidget::DrawMode::LINE);
      w_.persistSettings();
    }
    if (a.thickness > 0 && w_.lineThickness_) w_.lineThickness_->setValue(a.thickness);
    if (a.pointSize > 0 && w_.pointSize_) w_.pointSize_->setValue(a.pointSize);
    if (!a.style.isEmpty() && w_.lineStyle_) {
      const int idx = w_.lineStyle_->findData(a.style);
      if (idx >= 0) w_.lineStyle_->setCurrentIndex(idx);
    }
  }
  void ChatPlanTarget::setUnits(const QString& value) { w_.applyUnits(value); }
  void ChatPlanTarget::setViewVisibility(int points, int lines) {
    if (points >= 0 && w_.actShowPoints_) w_.actShowPoints_->setChecked(points == 1);
    if (lines >= 0 && w_.actShowLines_) w_.actShowLines_->setChecked(lines == 1);
  }
  // §10 clear: no confirmation — a modal would stall the turn.
  void ChatPlanTarget::clearImage() { w_.resetToBlankEditor(); }
  // §10 compare: the shared setter the toolbar combo and the View submenu drive.
  bool ChatPlanTarget::setCompare(const QString& mode, double split, QString*) {
    w_.setCompareModeUi(mode);
    if (split > 0) w_.canvas_->setCompareSplit(split);
    return true;
  }
  bool ChatPlanTarget::setZoom(int percent, bool fit, QString*) {
    if (fit) w_.fitToWindow();
    else w_.setZoom(percent / 100.0);
    return true;
  }
  // §10 blankColor: blanks only (note+skip otherwise), keeps every drawn line.
  bool ChatPlanTarget::setBlankColor(const QString& color, QString* note) {
    if (w_.blankColor_.isEmpty() || !w_.canvas_->hasImage()) {
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
    w_.applyBlankColor(c);
    return true;
  }
}  // namespace stencil::gui

