#include "settingsDialog.hpp"
#include "guiHelpers.hpp"
#include "theme.hpp"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace stencil::gui {

  SettingsDialog::SettingsDialog(const Settings& current, QWidget* parent)
      : QDialog(parent), base_(current), colorHex_(current.defaultColor) {
    setWindowTitle("Settings");
    setMinimumWidth(320);

    auto* form = new QFormLayout;

    theme_ = new QComboBox(this);
    theme_->setToolTip("Light/dark appearance — System follows the OS scheme");
    // Tri-state theme to match the browser: System (auto) follows the OS scheme.
    theme_->addItem("System (auto)", "system");
    theme_->addItem("Light", "light");
    theme_->addItem("Dark", "dark");
    {
      const int idx = theme_->findData(current.themeMode);
      theme_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    form->addRow("Theme", theme_);

    accent_ = new QComboBox(this);
    accent_->setToolTip("Accent colour used for highlights across the app");
    // Brand-accent presets (theme.hpp) — violet first/default. Same choices as
    // the browser/extension main-theme dropdowns. Each item carries a rounded
    // colour swatch icon so the actual colour shows next to the name.
    const auto swatch = [](const QColor& c) {
      QPixmap pm(16, 16);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(QPen(QColor(0, 0, 0, 70), 1));
      p.setBrush(c);
      p.drawRoundedRect(1, 1, 13, 13, 3, 3);
      p.end();
      return QIcon(pm);
    };
    for (const AccentPreset& a : accentPresets())
      accent_->addItem(swatch(QColor(a.hex)), a.label, a.key);
    {
      const int idx = accent_->findData(current.accentColor);
      accent_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    form->addRow("Main theme", accent_);

    autosave_ = new QCheckBox(this);
    autosave_->setChecked(current.autosave);
    autosave_->setToolTip("Automatically save the session as you edit");
    form->addRow("Autosave", autosave_);

    syncToServer_ = new QCheckBox(this);
    syncToServer_->setChecked(current.syncToServer);
    syncToServer_->setToolTip(
        "When off, edits to a fetched server project stay in this session only — "
        "never pushed to the server or saved locally (export or 'Make local copy' to keep them).");
    form->addRow("Sync changes to server", syncToServer_);

    // Note: "Auto-connect to servers on open" now lives in the Servers dialog
    // (it's a connection preference, persisted via net::connectionStore).

    showPoints_ = new QCheckBox(this);
    showPoints_->setChecked(current.showPoints);
    showPoints_->setToolTip("Show point markers on lines by default");
    form->addRow("Show points", showPoints_);

    showLines_ = new QCheckBox(this);
    showLines_->setChecked(current.showLines);
    showLines_->setToolTip("Show line strokes by default");
    form->addRow("Show lines", showLines_);

    color_ = new QPushButton(colorHex_, this);
    color_->setStyleSheet(QString("background:%1").arg(colorHex_));
    color_->setToolTip("Default colour for newly drawn lines — click to change");
    connect(color_, &QPushButton::clicked, this, &SettingsDialog::pickColor);
    form->addRow("Default color", color_);

    thickness_ = new QDoubleSpinBox(this);
    thickness_->setRange(1, 20);  // LIMITS.thickMin/thickMax
    thickness_->setValue(current.defaultThickness);
    thickness_->setToolTip("Default stroke thickness for new lines (px)");
    form->addRow("Default thickness", thickness_);

    markerSize_ = new QDoubleSpinBox(this);
    markerSize_->setRange(1, 30);  // LIMITS.markerMin/markerMax
    markerSize_->setValue(current.defaultMarkerSize);
    markerSize_->setToolTip("Default point marker size for new lines (px)");
    form->addRow("Default marker size", markerSize_);

    style_ = new QComboBox(this);
    style_->addItems({"solid", "dashed", "dotted"});
    style_->setCurrentText(current.defaultStyle);
    style_->setToolTip("Default stroke style for new lines");
    form->addRow("Default style", style_);

    page_ = new QComboBox(this);
    // S10 — same options as the toolbar combo: Custom… + the full ISO A/B/C
    // series, labels with physical sizes in the user's display unit, item data
    // = the canonical name (read back via currentData in result()).
    fillPageSizeCombo(page_, /*includeCustom=*/true, current.units);
    {
      const int idx = page_->findData(current.pageSize);
      page_->setCurrentIndex(idx >= 0 ? idx : page_->findData("A3"));
    }
    page_->setToolTip("Default page format for cm/inch measurements");
    form->addRow("Page size", page_);

    customW_ = new QDoubleSpinBox(this);
    customW_->setRange(1.0, 500.0);
    customW_->setSingleStep(0.1);
    customW_->setDecimals(1);
    customW_->setValue(current.customPageWidth);
    customW_->setToolTip("Custom page width in cm (used when page size is custom)");
    form->addRow("Custom width (cm)", customW_);

    customH_ = new QDoubleSpinBox(this);
    customH_->setRange(1.0, 500.0);
    customH_->setSingleStep(0.1);
    customH_->setDecimals(1);
    customH_->setValue(current.customPageHeight);
    customH_->setToolTip("Custom page height in cm (used when page size is custom)");
    form->addRow("Custom height (cm)", customH_);

    holdDelay_ = new QSpinBox(this);
    holdDelay_->setRange(100, 3000);  // clamp mirrors CanvasWidget::setHoldDrawDelay
    holdDelay_->setSingleStep(50);
    holdDelay_->setSuffix(" ms");
    holdDelay_->setValue(current.holdDrawDelay);
    holdDelay_->setToolTip(
        "Press-and-hold delay before hold-to-draw places a point");
    form->addRow("Hold-to-draw delay", holdDelay_);

    auto* buttons =
        makeButtonBox(this, QDialogButtonBox::Save | QDialogButtonBox::Cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
  }

  void SettingsDialog::pickColor() {
    const QColor c = QColorDialog::getColor(QColor(colorHex_), this, "Default line color",
                                            QColorDialog::DontUseNativeDialog);
    if (!c.isValid()) return;
    colorHex_ = c.name().toUpper();
    color_->setText(colorHex_);
    color_->setStyleSheet(QString("background:%1").arg(colorHex_));
  }

  Settings SettingsDialog::result() const {
    Settings s = base_;  // keep fields not exposed here (formulas, tooltip…)
    s.themeMode = theme_->currentData().toString();
    s.accentColor = accent_->currentData().toString();
    s.autosave = autosave_->isChecked();
    s.syncToServer = syncToServer_->isChecked();
    s.showPoints = showPoints_->isChecked();
    s.showLines = showLines_->isChecked();
    s.defaultColor = colorHex_;
    s.defaultThickness = thickness_->value();
    s.defaultMarkerSize = markerSize_->value();
    s.defaultStyle = style_->currentText();
    s.pageSize = page_->currentData().toString();
    s.customPageWidth = customW_->value();
    s.customPageHeight = customH_->value();
    s.holdDrawDelay = holdDelay_->value();
    return s;
  }

}
