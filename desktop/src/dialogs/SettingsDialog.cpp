#include "../support/SearchCombo.hpp"
#include "../support/motionIcons.hpp"
#include "SettingsDialog.hpp"
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
    Rows r;
    r.host = body.content;
    r.col = body.layout;
    r.mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    r.mono.setPixelSize(12);

    buildAppearanceRows(r, current);
    buildMotionRows(r, current);
    buildDrawingRows(r, current);
    buildPreferenceRows(r, current);

    // The AI assistant's own rows (provider, endpoint, model, key) live in the chat
    // dock's Assistant dialog, as the browser's do — not here (visualsModal.js).

    empty_ = modalEmptyLabel(tr("No matching settings."), r.host);
    empty_->hide();
    r.col->addWidget(empty_);
    r.col->addStretch(1);

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

  // Sections and rows register themselves for the search filter as they are built.
  void SettingsDialog::addSection(Rows& r, const QString& text) {
    QLabel* l = modalSectionLabel(text, r.host, groups_.isEmpty());
    r.col->addWidget(l);
    groups_.push_back({l, {}});
  }

  // Every control lands in the column at the column's size; checkboxes and the
  // assistant button keep their own shape, right-aligned in the same slot.
  void SettingsDialog::addRow(Rows& r, const QString& label, QWidget* field, bool column) {
    if (column) field->setFixedSize(CTRL_W, CTRL_H);
    QWidget* w = modalRow(r.host, label, field, /*grow=*/false);
    r.col->addWidget(w);
    groups_.last().rows.push_back({label, w});
  }

  QComboBox* SettingsDialog::addCombo(Rows& r, const QString& tip) {
    auto* c = new SearchComboBox(r.host, /*searchable=*/false);
    c->setToolTip(tip);
    return c;
  }

  void SettingsDialog::addWell(Rows& r, QPushButton*& btn, QString& hex, const QString& tip,
                               const QString& title) {
    btn = new QPushButton(r.host);
    btn->setFont(r.mono);
    setColorSwatch(btn, QColor(hex), QSize(CTRL_W, CTRL_H), /*withHex=*/true);
    btn->setToolTip(tip);
    connect(btn, &QPushButton::clicked, this,
            [this, &hex, title, b = btn] { pickColorInto(b, hex, title); });
  }

  void SettingsDialog::addCheck(Rows& r, QCheckBox*& box, bool on, const QString& tip) {
    box = new QCheckBox(r.host);
    box->setObjectName(QStringLiteral("captionCheck"));   // its caption is the row label
    box->setChecked(on);
    box->setToolTip(tip);
    connect(box, &QCheckBox::toggled, this, [this] { applyLive(); });
  }

  // App appearance (browser order: Main theme, then Appearance)
  void SettingsDialog::buildAppearanceRows(Rows& r, const Settings& current) {
    addSection(r, tr("App appearance"));

    accent_ = addCombo(r, "Accent color used for highlights across the app");
    // Brand-accent presets (theme.hpp) - violet first/default, the same choices as the browser and
    // extension dropdowns. Each item carries a rounded colour swatch icon.
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
        // A custom (non-preset) accent is set ONLY from the header logo, never here: add a "Custom" entry
        // so the COLLAPSED combo reflects it, but HIDE that row in the popup.
        accent_->addItem(swatch(QColor(current.accentColor)), "Custom", current.accentColor);
        const int customIdx = accent_->count() - 1;
        accent_->setCurrentIndex(customIdx);
        if (auto* view = qobject_cast<QListView*>(accent_->view()))
          view->setRowHidden(customIdx, true);
      } else {
        accent_->setCurrentIndex(0);
      }
    }
    addRow(r, tr("Main theme"), accent_);
    // activated(), not currentIndexChanged(): only a real user pick, not the
    // setCurrentIndex() above (every combo row below follows the same rule).
    connect(accent_, &QComboBox::activated, this, [this] { applyLive(); });

    theme_ = addCombo(r, "Light/dark appearance — System follows the OS scheme");
    // Tri-state theme to match the browser: System (auto) follows the OS scheme.
    theme_->addItem("System (follow the OS)", "system");
    theme_->addItem("Light", "light");
    theme_->addItem("Dark", "dark");
    {
      const int idx = theme_->findData(current.themeMode);
      theme_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addRow(r, tr("Appearance"), theme_);
    connect(theme_, &QComboBox::activated, this, [this] { applyLive(); });
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
