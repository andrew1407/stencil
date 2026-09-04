#include "../support/searchCombo.hpp"
#include "../support/shimmerOverlay.hpp"
#include "settingsDialog.hpp"
#include "guiHelpers.hpp"
#include "../support/modalChrome.hpp"   // modalSectionLabel
#include "../support/modalReveal.hpp"
#include "theme.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QListView>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
  // The shared modal section caption (browser .vs-section), with this form's own
  // breathing room between sections.
  QLabel* sectionHeader(QWidget* parent, const QString& text) {
    QLabel* l = stencil::gui::modalSectionLabel(text, parent);
    l->setContentsMargins(0, 10, 0, 2);
    return l;
  }
}  // namespace

namespace stencil::gui {

  SettingsDialog::SettingsDialog(const Settings& current, QWidget* parent)
      : QDialog(parent), base_(current), colorHex_(current.defaultColor),
        fillHex_(current.defaultFillColor), selGlowHex_(current.selGlowColor),
        hoverRingHex_(current.hoverRingColor), focusRingHex_(current.focusRingColor) {
    // The app's glass hover sweep on every control here (the browser's rule is
    // app-wide; a Qt window opts its own in). Deferred, so the sweep runs once this
    // constructor has built the content.
    installHoverShimmerLater(this);
    setWindowTitle("Settings");
    setMinimumWidth(320);

    auto* form = new QFormLayout;

    // ── App appearance (browser Default Visuals: "App appearance") ──
    form->addRow(sectionHeader(this, "App appearance"));

    theme_ = new SearchComboBox(this, /*searchable=*/false);
    theme_->setToolTip("Light/dark appearance — System follows the OS scheme");
    // Tri-state theme to match the browser: System (auto) follows the OS scheme.
    theme_->addItem("System (auto)", "system");
    theme_->addItem("Light", "light");
    theme_->addItem("Dark", "dark");
    {
      const int idx = theme_->findData(current.themeMode);
      theme_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    form->addRow("Appearance", theme_);
    // activated(), not currentIndexChanged(): only a real user pick, not the
    // setCurrentIndex() above (every combo row below follows the same rule).
    connect(theme_, &QComboBox::activated, this, [this] { applyLive(); });

    accent_ = new SearchComboBox(this, /*searchable=*/false);
    accent_->setToolTip("Accent color used for highlights across the app");
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
      if (idx >= 0) {
        accent_->setCurrentIndex(idx);
      } else if (current.accentColor.startsWith('#') && QColor(current.accentColor).isValid()) {
        // A custom (non-preset) accent — set ONLY from the header logo (double-click), never chosen
        // here. Add a "Custom" entry so the COLLAPSED combo reflects it, but HIDE that row in the
        // popup so it isn't offered as a choice in the list.
        accent_->addItem(swatch(QColor(current.accentColor)), "Custom", current.accentColor);
        const int customIdx = accent_->count() - 1;
        accent_->setCurrentIndex(customIdx);
        if (auto* view = qobject_cast<QListView*>(accent_->view()))
          view->setRowHidden(customIdx, true);
      } else {
        accent_->setCurrentIndex(0);
      }
    }
    form->addRow("Main theme", accent_);
    connect(accent_, &QComboBox::activated, this, [this] { applyLive(); });

    // ── Drawing defaults (applied to new lines) ──
    form->addRow(sectionHeader(this, "Drawing defaults"));

    color_ = new QPushButton(this);
    setColorSwatch(color_, QColor(colorHex_));
    color_->setToolTip("Default color for newly drawn lines — click to change");
    connect(color_, &QPushButton::clicked, this,
            [this] { pickColorInto(color_, colorHex_, "Default line color"); });
    form->addRow("Line color", color_);

    thickness_ = new QDoubleSpinBox(this);
    thickness_->setRange(1, 20);  // LIMITS.thickMin/thickMax
    thickness_->setValue(current.defaultThickness);
    thickness_->setToolTip("Default stroke thickness for new lines (px)");
    form->addRow("Line thickness", thickness_);
    connect(thickness_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    pointSize_ = new QDoubleSpinBox(this);
    pointSize_->setRange(1, 30);  // LIMITS.pointMin/pointMax
    pointSize_->setValue(current.defaultPointSize);
    pointSize_->setToolTip("Default point size for new lines (px)");
    form->addRow("Point size", pointSize_);
    connect(pointSize_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    style_ = new SearchComboBox(this, /*searchable=*/false);
    style_->addItems({"solid", "dashed", "dotted"});
    style_->setCurrentText(current.defaultStyle);
    style_->setToolTip("Default stroke style for new lines");
    form->addRow("Line style", style_);
    connect(style_, &QComboBox::activated, this, [this] { applyLive(); });

    fillColor_ = new QPushButton(this);
    setColorSwatch(fillColor_, QColor(fillHex_));
    fillColor_->setToolTip("Fill applied to newly locked areas — click to change");
    connect(fillColor_, &QPushButton::clicked, this,
            [this] { pickColorInto(fillColor_, fillHex_, "Area fill color"); });
    form->addRow("Area fill (new locked areas)", fillColor_);

    // ── Drawing behavior ──
    form->addRow(sectionHeader(this, "Drawing behavior"));

    holdDelay_ = new QSpinBox(this);
    holdDelay_->setRange(100, 3000);  // clamp mirrors CanvasWidget::setHoldDrawDelay
    holdDelay_->setSingleStep(50);
    holdDelay_->setSuffix(" ms");
    holdDelay_->setValue(current.holdDrawDelay);
    holdDelay_->setToolTip(
        "Press-and-hold delay before hold-to-draw places a point");
    form->addRow("Hold-to-draw delay", holdDelay_);
    connect(holdDelay_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    // ── Highlight styles ──
    form->addRow(sectionHeader(this, "Highlight styles"));

    selGlow_ = new QPushButton(this);
    setColorSwatch(selGlow_, QColor(selGlowHex_));
    selGlow_->setToolTip("Selected line/point glow — click to change");
    connect(selGlow_, &QPushButton::clicked, this,
            [this] { pickColorInto(selGlow_, selGlowHex_, "Selection glow color"); });
    form->addRow("Selected line/point glow", selGlow_);

    hoverRing_ = new QPushButton(this);
    setColorSwatch(hoverRing_, QColor(hoverRingHex_));
    hoverRing_->setToolTip("Point hover ring — click to change");
    connect(hoverRing_, &QPushButton::clicked, this,
            [this] { pickColorInto(hoverRing_, hoverRingHex_, "Point hover ring color"); });
    form->addRow("Point hover ring", hoverRing_);

    focusRing_ = new QPushButton(this);
    setColorSwatch(focusRing_, QColor(focusRingHex_));
    focusRing_->setToolTip("Focused/clicked point ring — click to change");
    connect(focusRing_, &QPushButton::clicked, this,
            [this] { pickColorInto(focusRing_, focusRingHex_, "Point focus ring color"); });
    form->addRow("Point focus ring", focusRing_);

    // ── App preferences (desktop-only; browser has no home for these) ──
    form->addRow(sectionHeader(this, "App preferences"));

    nativeMenuBar_ = new QCheckBox(this);
    nativeMenuBar_->setChecked(current.nativeMenuBar);
    nativeMenuBar_->setToolTip(
#ifdef Q_OS_WIN
        "Windows has no global menu bar, so this has no effect here.");
    nativeMenuBar_->setEnabled(false);
#else
        "On: the menu bar goes where your desktop puts it (the macOS menu bar, or a "
        "GNOME/Unity app menu). Off: it stays inside the window — use this if the "
        "in-window bar comes up empty on your desktop.\n\nTakes effect on restart. "
        "This dialog is always reachable with Ctrl+, so you can undo it even with "
        "no menus showing.");
#endif
    form->addRow("Use the system menu bar", nativeMenuBar_);
    connect(nativeMenuBar_, &QCheckBox::toggled, this, [this] { applyLive(); });

    autosave_ = new QCheckBox(this);
    autosave_->setChecked(current.autosave);
    autosave_->setToolTip("Automatically save the session as you edit");
    form->addRow("Autosave", autosave_);
    connect(autosave_, &QCheckBox::toggled, this, [this] { applyLive(); });

    // Note: "Auto-connect to servers on open" and "Sync changes to server" both live
    // in the Servers dialog now (connection preferences, as in the browser's modal);
    // syncToServer rides through result() untouched from base_.

    showPoints_ = new QCheckBox(this);
    showPoints_->setChecked(current.showPoints);
    showPoints_->setToolTip("Show points on lines by default");
    form->addRow("Show points", showPoints_);
    connect(showPoints_, &QCheckBox::toggled, this, [this] { applyLive(); });

    showLines_ = new QCheckBox(this);
    showLines_->setChecked(current.showLines);
    showLines_->setToolTip("Show line strokes by default");
    form->addRow("Show lines", showLines_);
    connect(showLines_, &QCheckBox::toggled, this, [this] { applyLive(); });

    page_ = new SearchComboBox(this);
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
    connect(page_, &QComboBox::activated, this, [this] { applyLive(); });

    customW_ = new QDoubleSpinBox(this);
    customW_->setRange(1.0, 500.0);
    customW_->setSingleStep(0.1);
    customW_->setDecimals(1);
    customW_->setValue(current.customPageWidth);
    customW_->setToolTip("Custom page width in cm (used when page size is custom)");
    form->addRow("Custom width (cm)", customW_);
    connect(customW_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    customH_ = new QDoubleSpinBox(this);
    customH_->setRange(1.0, 500.0);
    customH_->setSingleStep(0.1);
    customH_->setDecimals(1);
    customH_->setValue(current.customPageHeight);
    customH_->setToolTip("Custom page height in cm (used when page size is custom)");
    form->addRow("Custom height (cm)", customH_);
    connect(customH_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    // "Open in…" targets (Project ▸ Open In…): where the browser app lives and
    // which Telegram bot to deep-link (empty hides the Telegram option).
    browserUrl_ = new QLineEdit(current.browserBaseUrl, this);
    browserUrl_->setToolTip(
        "Base URL of the Stencil browser app, used by \"Open In… → Browser app\"");
    form->addRow("Browser app URL", browserUrl_);
    connect(browserUrl_, &QLineEdit::editingFinished, this, [this] { applyLive(); });

    botUsername_ = new QLineEdit(current.telegramBotUsername, this);
    botUsername_->setPlaceholderText("e.g. my_stencil_bot (empty = hidden)");
    botUsername_->setToolTip(
        "Telegram bot username (without @) for \"Open In… → Telegram bot\"; "
        "leave empty to hide that option");
    form->addRow("Telegram bot", botUsername_);
    connect(botUsername_, &QLineEdit::editingFinished, this, [this] { applyLive(); });

    // ── AI assistant (llm-contract.md §5) — its own commit/discard dialog ──
    form->addRow(sectionHeader(this, "AI assistant"));
    auto* openAssistant = new QPushButton("Open Assistant Settings…", this);
    openAssistant->setToolTip(
        "Provider, endpoint, model, and API key — its own dialog, saved on its own Save");
    connect(openAssistant, &QPushButton::clicked, this,
            [this] { emit openAssistantSettingsRequested(); });
    form->addRow(openAssistant);

    auto* buttons = makeButtonBox(this, QDialogButtonBox::Close);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
  }

  void SettingsDialog::pickColorInto(QPushButton* btn, QString& hex, const QString& title) {
    // Anchored on the swatch button that was clicked.
    const QColor c = support::pickColorAnimated(QColor(hex), this, title, btn);
    if (!c.isValid()) return;
    hex = c.name().toUpper();
    setColorSwatch(btn, c);
    applyLive();
  }

  void SettingsDialog::applyLive() {
    if (onChange_) onChange_(result());
  }

  Settings SettingsDialog::result() const {
    Settings s = base_;  // keep fields not exposed here (formulas, tooltip, llm*…)
    s.themeMode = theme_->currentData().toString();
    s.accentColor = accent_->currentData().toString();
    s.nativeMenuBar = nativeMenuBar_->isChecked();
    s.autosave = autosave_->isChecked();
    s.showPoints = showPoints_->isChecked();
    s.showLines = showLines_->isChecked();
    s.defaultColor = colorHex_;
    s.defaultThickness = thickness_->value();
    s.defaultPointSize = pointSize_->value();
    s.defaultStyle = style_->currentText();
    s.defaultFillColor = fillHex_;
    s.selGlowColor = selGlowHex_;
    s.hoverRingColor = hoverRingHex_;
    s.focusRingColor = focusRingHex_;
    s.pageSize = page_->currentData().toString();
    s.customPageWidth = customW_->value();
    s.customPageHeight = customH_->value();
    s.holdDrawDelay = holdDelay_->value();
    s.browserBaseUrl = browserUrl_->text().trimmed();
    s.telegramBotUsername = botUsername_->text().trimmed().remove(QLatin1Char('@'));
    return s;
  }

}
