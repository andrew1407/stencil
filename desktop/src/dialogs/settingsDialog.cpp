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
#include <QVBoxLayout>

namespace stencil::gui {

  SettingsDialog::SettingsDialog(const Settings& current, QWidget* parent)
      : QDialog(parent), base_(current), colorHex_(current.defaultColor) {
    setWindowTitle("Settings");
    setMinimumWidth(320);

    auto* form = new QFormLayout;

    theme_ = new QComboBox(this);
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
    form->addRow("Autosave", autosave_);

    showPoints_ = new QCheckBox(this);
    showPoints_->setChecked(current.showPoints);
    form->addRow("Show points", showPoints_);

    showLines_ = new QCheckBox(this);
    showLines_->setChecked(current.showLines);
    form->addRow("Show lines", showLines_);

    color_ = new QPushButton(colorHex_, this);
    color_->setStyleSheet(QString("background:%1").arg(colorHex_));
    connect(color_, &QPushButton::clicked, this, &SettingsDialog::pickColor);
    form->addRow("Default color", color_);

    thickness_ = new QDoubleSpinBox(this);
    thickness_->setRange(1, 20);  // LIMITS.thickMin/thickMax
    thickness_->setValue(current.defaultThickness);
    form->addRow("Default thickness", thickness_);

    markerSize_ = new QDoubleSpinBox(this);
    markerSize_->setRange(1, 30);  // LIMITS.markerMin/markerMax
    markerSize_->setValue(current.defaultMarkerSize);
    form->addRow("Default marker size", markerSize_);

    style_ = new QComboBox(this);
    style_->addItems({"solid", "dashed", "dotted"});
    style_->setCurrentText(current.defaultStyle);
    form->addRow("Default style", style_);

    page_ = new QComboBox(this);
    page_->addItems({"A3", "A4", "custom"});  // S10
    page_->setCurrentText(current.pageSize);
    form->addRow("Page size", page_);

    customW_ = new QDoubleSpinBox(this);
    customW_->setRange(1.0, 500.0);
    customW_->setSingleStep(0.1);
    customW_->setDecimals(1);
    customW_->setValue(current.customPageWidth);
    form->addRow("Custom width (cm)", customW_);

    customH_ = new QDoubleSpinBox(this);
    customH_->setRange(1.0, 500.0);
    customH_->setSingleStep(0.1);
    customH_->setDecimals(1);
    customH_->setValue(current.customPageHeight);
    form->addRow("Custom height (cm)", customH_);

    auto* buttons =
        makeButtonBox(this, QDialogButtonBox::Save | QDialogButtonBox::Cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
  }

  void SettingsDialog::pickColor() {
    const QColor c = QColorDialog::getColor(QColor(colorHex_), this,
                                            "Default line color");
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
    s.showPoints = showPoints_->isChecked();
    s.showLines = showLines_->isChecked();
    s.defaultColor = colorHex_;
    s.defaultThickness = thickness_->value();
    s.defaultMarkerSize = markerSize_->value();
    s.defaultStyle = style_->currentText();
    s.pageSize = page_->currentText();
    s.customPageWidth = customW_->value();
    s.customPageHeight = customH_->value();
    return s;
  }

}
