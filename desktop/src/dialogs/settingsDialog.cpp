#include "../support/searchCombo.hpp"
#include "../support/motionIcons.hpp"
#include "settingsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"   // the browser modal shell + .vs-row rows
#include "../support/modalReveal.hpp"
#include "theme.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QListView>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // The browser's ONE control column (components.css --vs-ctrl-w / --vs-ctrl-h).
    constexpr int CTRL_W = 180;
    constexpr int CTRL_H = 30;
    // The browser's VIS_DEFAULTS (visualsModal.js) — what Reset All restores.
    constexpr const char* DEF_COLOR = "#FFFF00";
    constexpr double DEF_THICKNESS = 2;
    constexpr double DEF_POINT_SIZE = 4;
    constexpr const char* DEF_STYLE = "solid";
    constexpr const char* DEF_FILL = "#ffffff";
    constexpr const char* DEF_SEL_GLOW = "#ffc800";
    constexpr const char* DEF_HOVER_RING = "#7c3aed";
    constexpr const char* DEF_FOCUS_RING = "#7c3aed";
    constexpr int DEF_HOLD_DELAY = 500;
    constexpr const char* DEF_ACCENT = "violet";
    // ui/motionPrefs.js DEFAULT_DRAWING_ANIMATIONS / DEFAULT_MOTION_MODE.
    constexpr bool DEF_DRAW_ANIM = true;
    constexpr const char* DEF_MOTION_MODE = "particles";
  }  // namespace

  SettingsDialog::SettingsDialog(const Settings& current, QWidget* parent)
      : QDialog(parent), base_(current), colorHex_(current.defaultColor),
        fillHex_(current.defaultFillColor), selGlowHex_(current.selGlowColor),
        hoverRingHex_(current.hoverRingColor), focusRingHex_(current.focusRingColor) {
    setWindowTitle("Visuals & Settings");
    ModalChrome chrome = installModalChrome(this, "palette", tr("Visuals & Settings"));
    search_ = addModalSearchBar(chrome, tr("Search settings…"));
    search_->setToolTip("Filter the settings by name");
    ModalScrollBody body = makeModalScrollBody(chrome);
    QWidget* host = body.content;
    QVBoxLayout* col = body.layout;

    // Sections and rows register themselves for the search filter as they are built.
    const auto section = [&](const QString& text) {
      QLabel* l = modalSectionLabel(text, host, groups_.isEmpty());
      col->addWidget(l);
      groups_.push_back({l, {}});
    };
    // Every control lands in the column at the column's size; checkboxes and the
    // assistant button keep their own shape, right-aligned in the same slot.
    const auto row = [&](const QString& label, QWidget* field, bool column = true) {
      if (column) field->setFixedSize(CTRL_W, CTRL_H);
      QWidget* r = modalRow(host, label, field, /*grow=*/false);
      col->addWidget(r);
      groups_.last().rows.push_back({label, r});
    };
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(12);
    const auto combo = [&](const QString& tip) {
      auto* c = new SearchComboBox(host, /*searchable=*/false);
      c->setToolTip(tip);
      return c;
    };
    const auto well = [&](QPushButton*& btn, QString& hex, const QString& tip,
                          const QString& title) {
      btn = new QPushButton(host);
      btn->setFont(mono);
      setColorSwatch(btn, QColor(hex), QSize(CTRL_W, CTRL_H), /*withHex=*/true);
      btn->setToolTip(tip);
      connect(btn, &QPushButton::clicked, this,
              [this, &hex, title, b = btn] { pickColorInto(b, hex, title); });
    };
    const auto check = [&](QCheckBox*& box, bool on, const QString& tip) {
      box = new QCheckBox(host);
      box->setChecked(on);
      box->setToolTip(tip);
      connect(box, &QCheckBox::toggled, this, [this] { applyLive(); });
    };

    // App appearance (browser order: Main theme, then Appearance)
    section(tr("App appearance"));

    accent_ = combo("Accent color used for highlights across the app");
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
    row(tr("Main theme"), accent_);
    // activated(), not currentIndexChanged(): only a real user pick, not the
    // setCurrentIndex() above (every combo row below follows the same rule).
    connect(accent_, &QComboBox::activated, this, [this] { applyLive(); });

    theme_ = combo("Light/dark appearance — System follows the OS scheme");
    // Tri-state theme to match the browser: System (auto) follows the OS scheme.
    theme_->addItem("System (follow the OS)", "system");
    theme_->addItem("Light", "light");
    theme_->addItem("Dark", "dark");
    {
      const int idx = theme_->findData(current.themeMode);
      theme_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    row(tr("Appearance"), theme_);
    connect(theme_, &QComboBox::activated, this, [this] { applyLive(); });

    // Motion (browser visualsModal.js "Motion", same two rows in the same order)
    // Live-applied like everything else here, so the dialog's OWN closing flight is
    // already the mode you just picked.
    section(tr("Motion"));

    check(drawAnim_, current.drawingAnimations,
          "On: a new point flies to where you put it, popping and rippling as it lands.\n"
          "Off: every point goes straight down.");
    row(tr("Drawing animation"), drawAnim_, /*column=*/false);

    motionMode_ = combo(QString());   // no tooltip — the browser's dropdown has none (the glyphs say it)
    motionMode_->setObjectName(QStringLiteral("motionModeCombo"));
    // The browser's MOTION_MODE_LABELS, in its order (ui/motionPrefs.js).
    motionMode_->addItem("Dust", "particles");
    motionMode_->addItem("Water", "water");
    motionMode_->addItem("Fire", "fire");
    motionMode_->addItem("Sliding", "slide");
    motionMode_->addItem("None", "none");
    // Each mode's glyph (support/motionIcons.hpp — the browser's motionIcons.js): on the
    // trigger at rest, and on the popup rows animated as they are hovered.
    {
      // In the text colour, like the labels (never the accent — user decision).
      const QColor ink = palette().color(QPalette::Text);
      for (int i = 0; i < motionMode_->count(); ++i)
        motionMode_->setItemIcon(i, support::motionModeIcon(motionMode_->itemData(i).toString(), ink));
      auto* mm = static_cast<SearchComboBox*>(motionMode_);   // combo() builds SearchComboBoxes
      mm->setListDelegate(new support::MotionIconDelegate(mm->popupList(), mm));
      new support::MotionIconFace(mm);   // the face's glyph plays on change and on hover
    }
    {
      const int idx = motionMode_->findData(current.motionMode);
      motionMode_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    row(tr("Interface animation"), motionMode_);
    connect(motionMode_, &QComboBox::activated, this, [this] { applyLive(); });

    // Drawing defaults (applied to new lines)
    section(tr("Drawing defaults (applied to new lines)"));

    well(color_, colorHex_, "Default color for newly drawn lines — click to change",
         "Default line color");
    row(tr("Line color"), color_);

    thickness_ = new QDoubleSpinBox(host);
    thickness_->setRange(1, 20);  // LIMITS.thickMin/thickMax
    thickness_->setDecimals(0);   // the browser field is an integer
    thickness_->setValue(current.defaultThickness);
    thickness_->setToolTip("Default stroke thickness for new lines (px)");
    row(tr("Line thickness"), thickness_);
    connect(thickness_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    pointSize_ = new QDoubleSpinBox(host);
    pointSize_->setRange(1, 30);  // LIMITS.pointMin/pointMax
    pointSize_->setDecimals(0);
    pointSize_->setValue(current.defaultPointSize);
    pointSize_->setToolTip("Default point size for new lines (px)");
    row(tr("Point size"), pointSize_);
    connect(pointSize_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    style_ = combo("Default stroke style for new lines");
    style_->addItem("Solid", "solid");
    style_->addItem("Dashed", "dashed");
    style_->addItem("Dotted", "dotted");
    {
      const int idx = style_->findData(current.defaultStyle);
      style_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    row(tr("Line style"), style_);
    connect(style_, &QComboBox::activated, this, [this] { applyLive(); });

    well(fillColor_, fillHex_, "Fill applied to newly locked areas — click to change",
         "Area fill color");
    row(tr("Area fill (new locked areas)"), fillColor_);

    section(tr("Drawing behavior"));

    holdDelay_ = new QSpinBox(host);
    holdDelay_->setRange(100, 3000);  // clamp mirrors CanvasWidget::setHoldDrawDelay
    holdDelay_->setSingleStep(50);
    holdDelay_->setValue(current.holdDrawDelay);
    holdDelay_->setToolTip("Press-and-hold delay before hold-to-draw places a point");
    row(tr("Hold-to-draw delay (ms)"), holdDelay_);
    connect(holdDelay_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    section(tr("Highlight styles"));

    well(selGlow_, selGlowHex_, "Selected line/point glow — click to change",
         "Selection glow color");
    row(tr("Selected line/point glow"), selGlow_);
    well(hoverRing_, hoverRingHex_, "Point hover ring — click to change",
         "Point hover ring color");
    row(tr("Point hover ring"), hoverRing_);
    well(focusRing_, focusRingHex_, "Focused/clicked point ring — click to change",
         "Point focus ring color");
    row(tr("Point focus ring"), focusRing_);

    // App preferences (desktop-only; browser has no home for these)
    section(tr("App preferences"));

    check(nativeMenuBar_, current.nativeMenuBar,
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
    row(tr("Use the system menu bar"), nativeMenuBar_, /*column=*/false);

    check(autosave_, current.autosave, "Automatically save the session as you edit");
    row(tr("Autosave"), autosave_, /*column=*/false);

    // Note: "Auto-connect to servers on open" and "Sync changes to server" both live
    // in the Servers dialog now (connection preferences, as in the browser's modal);
    // syncToServer rides through result() untouched from base_.

    check(showPoints_, current.showPoints, "Show points on lines by default");
    row(tr("Show points"), showPoints_, /*column=*/false);
    check(showLines_, current.showLines, "Show line strokes by default");
    row(tr("Show lines"), showLines_, /*column=*/false);

    page_ = new SearchComboBox(host);
    // Same options as the toolbar combo: Custom… + the full ISO A/B/C
    // series, labels with physical sizes in the user's display unit, item data
    // = the canonical name (read back via currentData in result()).
    fillPageSizeCombo(page_, /*includeCustom=*/true, current.units);
    {
      const int idx = page_->findData(current.pageSize);
      page_->setCurrentIndex(idx >= 0 ? idx : page_->findData("A3"));
    }
    page_->setToolTip("Default page format for cm/inch measurements");
    row(tr("Page size"), page_);
    connect(page_, &QComboBox::activated, this, [this] { applyLive(); });

    customW_ = new QDoubleSpinBox(host);
    customW_->setRange(1.0, 500.0);
    customW_->setSingleStep(0.1);
    customW_->setDecimals(1);
    customW_->setValue(current.customPageWidth);
    customW_->setToolTip("Custom page width in cm (used when page size is custom)");
    row(tr("Custom width (cm)"), customW_);
    connect(customW_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    customH_ = new QDoubleSpinBox(host);
    customH_->setRange(1.0, 500.0);
    customH_->setSingleStep(0.1);
    customH_->setDecimals(1);
    customH_->setValue(current.customPageHeight);
    customH_->setToolTip("Custom page height in cm (used when page size is custom)");
    row(tr("Custom height (cm)"), customH_);
    connect(customH_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    // "Open in…" targets (Project ▸ Open In…): where the browser app lives and
    // which Telegram bot to deep-link (empty hides the Telegram option).
    browserUrl_ = new QLineEdit(current.browserBaseUrl, host);
    browserUrl_->setToolTip(
        "Base URL of the Stencil browser app, used by \"Open In… → Browser app\"");
    row(tr("Browser app URL"), browserUrl_);
    connect(browserUrl_, &QLineEdit::editingFinished, this, [this] { applyLive(); });

    botUsername_ = new QLineEdit(current.telegramBotUsername, host);
    botUsername_->setPlaceholderText("e.g. my_stencil_bot (empty = hidden)");
    botUsername_->setToolTip(
        "Telegram bot username (without @) for \"Open In… → Telegram bot\"; "
        "leave empty to hide that option");
    row(tr("Telegram bot"), botUsername_);
    connect(botUsername_, &QLineEdit::editingFinished, this, [this] { applyLive(); });

    // The AI assistant's own rows (provider, endpoint, model, key) live in the chat
    // dock's Assistant dialog, as the browser's do — not here (visualsModal.js).

    empty_ = modalEmptyLabel(tr("No matching settings."), host);
    empty_->hide();
    col->addWidget(empty_);
    col->addStretch(1);

    // Footer (browser .settings-footer): the live-apply hint beside Reset All.
    QHBoxLayout* footer =
        addModalFooter(chrome, tr("Changes apply live and are saved automatically."));
    auto* resetAll = new QPushButton(tr("Reset All"), this);
    makeModalCta(resetAll, "rotate-ccw");   // browser #vs-reset: the accent-filled CTA
    resetAll->setToolTip("Restore the default visuals (theme accent, line, highlight defaults)");
    resetAll->setAutoDefault(false);
    connect(resetAll, &QPushButton::clicked, this, &SettingsDialog::resetVisuals);
    footer->addWidget(resetAll);

    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString& q) { applyFilter(q); });
    // The browser's max-height: 82vh shell; the sections scroll inside it.
    sizeModalTall(this, MODAL_WIDTH);
  }

  void SettingsDialog::resetVisuals() {
    const struct { QPushButton* btn; QString* hex; const char* def; } wells[] = {
        {color_, &colorHex_, DEF_COLOR},         {fillColor_, &fillHex_, DEF_FILL},
        {selGlow_, &selGlowHex_, DEF_SEL_GLOW},   {hoverRing_, &hoverRingHex_, DEF_HOVER_RING},
        {focusRing_, &focusRingHex_, DEF_FOCUS_RING},
    };
    for (const auto& w : wells) {
      *w.hex = QString::fromLatin1(w.def);
      setColorSwatch(w.btn, QColor(*w.hex), QSize(CTRL_W, CTRL_H), /*withHex=*/true);
    }
    thickness_->setValue(DEF_THICKNESS);
    pointSize_->setValue(DEF_POINT_SIZE);
    holdDelay_->setValue(DEF_HOLD_DELAY);
    style_->setCurrentIndex(qMax(0, style_->findData(DEF_STYLE)));
    accent_->setCurrentIndex(qMax(0, accent_->findData(DEF_ACCENT)));
    drawAnim_->setChecked(DEF_DRAW_ANIM);
    motionMode_->setCurrentIndex(qMax(0, motionMode_->findData(QLatin1String(DEF_MOTION_MODE))));
    applyLive();
    emit visualsReset();
  }

  void SettingsDialog::pickColorInto(QPushButton* btn, QString& hex, const QString& title) {
    // Anchored on the swatch button that was clicked.
    const QColor c = support::pickColorAnimated(QColor(hex), this, title, btn);
    if (!c.isValid()) return;
    hex = c.name().toUpper();
    setColorSwatch(btn, c, QSize(CTRL_W, CTRL_H), /*withHex=*/true);
    applyLive();
  }
}

