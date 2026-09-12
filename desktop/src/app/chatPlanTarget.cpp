#include "chatPlanTarget.hpp"

#include "mainWindow.hpp"
#include "../support/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "mainWindowHelpers.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "../canvas/canvasWidget.hpp"
#include "../net/connectionStore.hpp"
#include "../net/serverClient.hpp"
#include "../support/displayName.hpp"
#include "../support/notifications.hpp"
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
  void ChatPlanTarget::setFormula(QChar axis, const QString& expr) {
    // Clearing an axis ("" = identity) must not switch formulas ON.
    if (!expr.isEmpty() && w_.allowFormulas_ && !w_.allowFormulas_->isChecked())
      w_.allowFormulas_->setChecked(true);  // shows the inputs + persists
    QLineEdit* edit = axis == QLatin1Char('x') ? w_.formulaX_ : w_.formulaY_;
    if (!edit) return;
    edit->setText(expr);
    // An op-plan applies NOW: the typing-pause debounce is for a human at the
    // keyboard, and the caller expects the formula live when this returns.
    w_.validateAndApplyFormulas();
  }
  // §2 formula enabled:false/true — the allow-formulas toggle itself (the
  // toolbar checkbox is the source of truth; actAllowFormulas_ follows it).
  void ChatPlanTarget::setFormulasEnabled(bool on) {
    if (w_.allowFormulas_ && w_.allowFormulas_->isChecked() != on)
      w_.allowFormulas_->setChecked(on);  // applies + persists via its handler
  }
  void ChatPlanTarget::setPageFormat(const QString& isoName) {
    const int idx = w_.units_.pageSize->findData(isoName);
    if (idx >= 0) w_.units_.pageSize->setCurrentIndex(idx);  // fires onPageSizeChanged
  }
  // §2 page custom dims: the SAME controls the toolbar drives — the custom
  // spinboxes (edited in the active unit) + the "custom" combo entry.
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
    // §2: explicit cm dims override the format (they become the custom page).
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
  // §2 undo/redo: the canvas's own history stack (the toolbar's Undo/Redo).
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
  // §10 accent preset: the accentPresets() apply path (the preset KEY is what
  // the Settings dropdown / logo-click cycle store). Unknown name = note+skip.
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
        // The lineColorBtn_ click handler's body, minus the colour dialog.
        w_.lineColorValue_ = c;
        w_.updateColorSwatch(w_.lineColorBtn_, c);
        w_.settings_.defaultColor = c.name(QColor::HexRgb);
        w_.onLineStyleControlChanged();
      }
    }
    // §10 widening: pointColor ("" = follow the stroke) — the pointColorBtn_
    // handler's body, minus the colour dialog.
    if (a.pointColorSet) {
      w_.settings_.defaultPointColor = a.pointColor;
      if (w_.pointColorBtn_)
        w_.updateColorSwatch(w_.pointColorBtn_, w_.effectiveDefaultPointColor());
      w_.onLineStyleControlChanged();
    }
    // §10 widening: drawMode — the context menu's line<->rect bridge.
    if (!a.drawMode.isEmpty()) {
      w_.canvas_->setDrawMode(a.drawMode == QLatin1String("rect")
                                  ? CanvasWidget::DrawMode::Rect
                                  : CanvasWidget::DrawMode::Line);
      w_.persistSettings();
    }
    // The toolbar inputs — their valueChanged handlers persist + apply.
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
  // §10 clear: the empty "Open an image" canvas the trash button leaves behind.
  // No confirmation — the plan already said so, and a modal would stall the turn.
  void ChatPlanTarget::clearImage() { w_.resetToBlankEditor(); }
  // §10 compare: the shared setter the toolbar combo and the View submenu
  // drive (syncs canvas + both UIs); the divider fraction goes to the canvas.
  bool ChatPlanTarget::setCompare(const QString& mode, double split, QString*) {
    w_.setCompareModeUi(mode);
    if (split > 0) w_.canvas_->setCompareSplit(split);
    return true;
  }
  // §10 zoom: the zoom_ combo / Fit-to-Window paths (view-only).
  bool ChatPlanTarget::setZoom(int percent, bool fit, QString*) {
    if (fit) w_.fitToWindow();
    else w_.setZoom(percent / 100.0);
    return true;
  }
  // §10 blankColor: the nameBar_.blankColorBtn path minus its dialog — blanks only
  // (note+skip otherwise), keeps every drawn line.
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

