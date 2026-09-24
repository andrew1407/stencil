#include "../../support/control/dblReset.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include "../../support/icon/motionIcons.hpp"
#include "SettingsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../support/modal/modalChrome.hpp"   // the browser modal shell + .vs-row rows
#include "../../support/modal/modalReveal.hpp"
#include "../../support/motionPrefs.hpp"
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
    // The browser's VIS_DEFAULTS (modal.js) — what Reset All restores.
    constexpr const char* DEF_COLOR = "#FFFF00";
    constexpr double DEF_THICKNESS = 2;
    constexpr double DEF_POINT_SIZE = 4;
    constexpr const char* DEF_STYLE = "solid";
    constexpr const char* DEF_FILL = "#ffffff";
    constexpr const char* DEF_SEL_GLOW = "#ffc800";
    constexpr const char* DEF_HOVER_RING = DEFAULT_ACCENT_HEX;
    constexpr const char* DEF_FOCUS_RING = DEFAULT_ACCENT_HEX;
    constexpr int DEF_HOLD_DELAY = 500;
    constexpr const char* DEF_ACCENT = DEFAULT_ACCENT_KEY;
    // ui/prefs.js DEFAULT_DRAWING_ANIMATIONS / DEFAULT_MOTION_MODE.
    constexpr bool DEF_DRAW_ANIM = true;
    constexpr const char* DEF_MOTION_MODE = "particles";
  }  // namespace

  SettingsDialog::SettingsDialog(const Settings& current, QWidget* parent)
      : QDialog(parent), base(current), colorHex(current.defaultColor),
        fillHex(current.defaultFillColor), selGlowHex(current.selGlowColor),
        hoverRingHex(current.hoverRingColor), focusRingHex(current.focusRingColor) {
    setWindowTitle("Visuals & Settings");
    ModalChrome chrome = installModalChrome(this, "palette", tr("Visuals & Settings"));
    search = addModalSearchBar(chrome, tr("Search settings…"));
    search->setToolTip("Filter the settings by name");
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
    // dock's Assistant dialog, as the browser's do — not here (modal.js).

    empty = modalEmptyLabel(tr("No matching settings."), r.host);
    empty->hide();
    r.col->addWidget(empty);
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
    // What a double-click puts each row back to (support/control/dblReset.hpp).
    const Settings d;
    for (const auto& [w, v] : std::initializer_list<std::pair<QWidget*, QVariant>>{
             {theme, d.themeMode}, {accent, QString(DEF_ACCENT)}, {nativeMenuBar, d.nativeMenuBar},
             {autosave, d.autosave}, {showPoints, d.showPoints}, {showLines, d.showLines},
             {style, QString(DEF_STYLE)}, {page, d.pageSize}, {drawAnim, DEF_DRAW_ANIM},
             {modalBackdrop, d.modalBackdrop}, {motionMode, QString(DEF_MOTION_MODE)}})
      support::setResetDefault(w, v);

    connect(search, &QLineEdit::textChanged, this,
            [this](const QString& q) { applyFilter(q); });
    // The browser's max-height: 82vh shell; the sections scroll inside it.
    sizeModalTall(this, MODAL_WIDTH);
  }

  // Sections and rows register themselves for the search filter as they are built.
  void SettingsDialog::addSection(Rows& r, const QString& text) {
    QLabel* l = modalSectionLabel(text, r.host, groups.isEmpty());
    r.col->addWidget(l);
    groups.push_back({l, {}});
  }

  // Every control lands in the column at the column's size; checkboxes and the
  // assistant button keep their own shape, right-aligned in the same slot.
  void SettingsDialog::addRow(Rows& r, const QString& label, QWidget* field, bool column) {
    if (column) field->setFixedSize(CTRL_W, CTRL_H);
    QWidget* w = modalRow(r.host, label, field, /*grow=*/false);
    r.col->addWidget(w);
    groups.last().rows.push_back({label, w});
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

    accent = addCombo(r, "Accent color used for highlights across the app");
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
      accent->addItem(swatch(QColor(a.hex)), a.label, a.key);
    {
      const int idx = accent->findData(current.accentColor);
      if (idx >= 0) {
        accent->setCurrentIndex(idx);
      } else if (current.accentColor.startsWith('#') && QColor(current.accentColor).isValid()) {
        // A custom (non-preset) accent is set ONLY from the header logo, never here: add a "Custom" entry
        // so the COLLAPSED combo reflects it, but HIDE that row in the popup.
        accent->addItem(swatch(QColor(current.accentColor)), "Custom", current.accentColor);
        const int customIdx = accent->count() - 1;
        accent->setCurrentIndex(customIdx);
        if (auto* view = qobject_cast<QListView*>(accent->view()))
          view->setRowHidden(customIdx, true);
      } else {
        accent->setCurrentIndex(0);
      }
    }
    addRow(r, tr("Main theme"), accent);
    // activated(), not currentIndexChanged(): only a real user pick, not the
    // setCurrentIndex() above (every combo row below follows the same rule).
    connect(accent, &QComboBox::activated, this, [this] { applyLive(); });

    theme = addCombo(r, "Light/dark appearance — System follows the OS scheme");
    // Tri-state theme to match the browser: System (auto) follows the OS scheme.
    theme->addItem("System (follow the OS)", "system");
    theme->addItem("Light", "light");
    theme->addItem("Dark", "dark");
    {
      const int idx = theme->findData(current.themeMode);
      theme->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addRow(r, tr("Appearance"), theme);
    connect(theme, &QComboBox::activated, this, [this] { applyLive(); });
  }

  void SettingsDialog::resetVisuals() {
    const struct { QPushButton* btn; QString* hex; const char* def; } wells[] = {
        {color, &colorHex, DEF_COLOR},         {fillColor, &fillHex, DEF_FILL},
        {selGlow, &selGlowHex, DEF_SEL_GLOW},   {hoverRing, &hoverRingHex, DEF_HOVER_RING},
        {focusRing, &focusRingHex, DEF_FOCUS_RING},
    };
    for (const auto& w : wells) {
      *w.hex = QString::fromLatin1(w.def);
      setColorSwatch(w.btn, QColor(*w.hex), QSize(CTRL_W, CTRL_H), /*withHex=*/true);
    }
    thickness->setValue(DEF_THICKNESS);
    pointSize->setValue(DEF_POINT_SIZE);
    holdDelay->setValue(DEF_HOLD_DELAY);
    style->setCurrentIndex(qMax(0, style->findData(DEF_STYLE)));
    accent->setCurrentIndex(qMax(0, accent->findData(DEF_ACCENT)));
    drawAnim->setChecked(DEF_DRAW_ANIM);
    motionMode->setCurrentIndex(qMax(0, motionMode->findData(QLatin1String(DEF_MOTION_MODE))));
    motionTouched = true;   // a reset is the user's own pick: it ends a skin's session override
    support::clearMotionOverride();
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
