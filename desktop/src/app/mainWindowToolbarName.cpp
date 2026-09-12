#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "iconSet.hpp"
#include "logoHoverFx.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "searchCombo.hpp"
#include "controlsPill.hpp"
#include "openImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"   // section buttons come and go as sand
#include "../support/iconMotion.hpp"
#include "../support/shimmerOverlay.hpp"
#include "../support/wrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

// MainWindow's toolbar assembly: the header row, the three tool rows and their
// sections, and the style/formula wiring. Split from mainWindow.cpp; same class,
// definitions only.

namespace stencil::gui {

  // Project name field + inline-rename ✓/✗ (mirrors the browser topbar). The field shows the
  // active project's name and renames it inline, validated live: ✓ is enabled only for a changed,
  // valid (non-empty, ≤80, unique) name, with the reason on its tooltip when disabled. Enter = ✓,
  // Escape / click-away = ✗. Lives in the always-visible header row beside the "Controls" pill.
  void MainWindow::buildProjectNameGroup(QToolBar* tbName) {
    // (no "Project:" caption — the field alone reads as the project name, browser parity)
    // ONE container for the field + its affordances (browser .project-name-field
    // parity): hover is the container's own gap-free rect, so sweeping between the
    // field and the ✎/🎨 buttons can never flicker the reveal (which replayed the
    // dust and re-armed the tooltip). RowCard (connectDialog) pattern.
    nameBar_.group = new QWidget(this);
    auto* nameLay = new QHBoxLayout(nameBar_.group);
    nameLay->setContentsMargins(0, 0, 0, 0);
    // Air between the name and its two chips: at 4 they sat right against the field's
    // edge. Browser twin: .project-name-field's `gap`.
    nameLay->setSpacing(8);
    nameBar_.field = new QLineEdit(nameBar_.group);
    nameBar_.field->setObjectName("projectNameField");   // theme.cpp: no hover ring on a title
    nameBar_.field->setPlaceholderText("No project");
    nameBar_.field->setToolTip(QString());   // no tooltip on the name field (the ✎ button has its own)
    nameBar_.field->setMinimumWidth(150);
    nameBar_.field->setMaximumWidth(300);
    // A QLineEdit is horizontally Expanding by default — in a toolbar that stretches it across the
    // whole row and shoves the ✎/🎨 far to the right. Make it content-sized so the name + icons
    // pack together on the left (a trailing spacer below absorbs the rest of the row).
    nameBar_.field->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    nameBar_.field->setEnabled(false);
    nameBar_.field->setReadOnly(true);  // browser-like: read-only until edit mode (✎ / double-click)
    nameLay->addWidget(nameBar_.field);
    // "?" status hint on the never-collapsing header row — a LABEL (hover readout,
    // browser #hints-btn), round outline, added after ✎/🎨 (browser order).
    statusHint_ = new QLabel(this);
    statusHint_->setObjectName("statusHint");
    statusHint_->setText(QStringLiteral("?"));
    statusHint_->setAlignment(Qt::AlignCenter);
    statusHint_->setFixedSize(18, 18);
    statusHint_->setFocusPolicy(Qt::NoFocus);
    statusHint_->setAttribute(Qt::WA_TransparentForMouseEvents, false);   // hover still shows the tip
    statusHint_->setStyleSheet(
        "QLabel#statusHint{color:rgba(154,160,168,0.75);font-size:11px;font-weight:600;"
        "border:1px solid rgba(154,160,168,0.45);border-radius:9px;background:transparent;}");
    // ✎ rename + 🎨 colour affordances beside the name (hover-revealed, enabled only with
    // an active project). Fixed 26px boxes, or the hover reveal grows the header row.
    // Each is a CHIP, not a bare glyph — the browser paints both on --bg-info inside a
    // --border-main outline at rest. QSS half: QToolButton[nameAffordance] in theme.cpp.
    const auto sizeToRow = [](QToolButton* b) {
      // The app's two hover treatments, explicitly: this group is built after the sweep that
      // installs the shimmer across the toolbar rows, so these four were the only controls in
      // the bar without it. The icon-motion filter is app-wide and needs no
      // hand — a themedIcon glyph is all it asks for.
      installHoverShimmer(b);
      // The browser's box with a glyph to match — at 26/15 the pair read as small, faint
      // marks beside the name.
      b->setFixedSize(kNameChipBox, kNameChipBox);
      b->setIconSize(QSize(kNameChipGlyph, kNameChipGlyph));
      b->setProperty("nameAffordance", true);
    };
    nameBar_.edit = new QToolButton(nameBar_.group);
    nameBar_.edit->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(nameBar_.edit);
    nameBar_.edit->setAutoRaise(true);
    nameBar_.edit->setToolTip("Rename project");
    setTipHotkey(nameBar_.edit, actRenameProject_);   // browser #project-name-edit: its chord as the keycap
    nameBar_.edit->setEnabled(false);
    nameLay->addWidget(nameBar_.edit);
    connect(nameBar_.edit, &QToolButton::clicked, this, [this] { enterNameEdit(); });
    nameBar_.colorBtn = new QToolButton(nameBar_.group);
    nameBar_.colorBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(nameBar_.colorBtn);
    nameBar_.colorBtn->setAutoRaise(true);
    nameBar_.colorBtn->setToolTip("Project color — paints the project name");
    nameBar_.colorBtn->setEnabled(false);
    nameLay->addWidget(nameBar_.colorBtn);
    // Click opens a small menu (browser parity): "Choose colour…" + "Use theme default colour".
    // The menu runs its own loop and fully closes before we open the picker (deferred), so no stray
    // grab dismisses the dialog. Right-click still resets straight to the theme default.
    connect(nameBar_.colorBtn, &QToolButton::clicked, this, [this] { showProjectColorMenu(); });
    // …and the "?" readout closes the group, after the rename + colour affordances, which
    // is the browser's order (name · ✎ · 🎨 · ?).
    statusHintAction_ = tbName->addWidget(statusHint_);
    statusHintAction_->setVisible(false);
    nameBar_.colorBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(nameBar_.colorBtn, &QToolButton::customContextMenuRequested, this,
            [this](const QPoint&) { setActiveProjectColor(QString()); });
    // Inline-rename confirm/cancel: line-art check / x glyphs (themed in
    // styleActionIcons) instead of the bare ✓/✗ text, matching the browser's
    // icon buttons. Icon-only with a tooltip.
    nameBar_.accept = new QToolButton(nameBar_.group);
    nameBar_.accept->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Hover text is shared with the browser's toolbar.js (#project-name-accept /
    // #project-name-cancel) — a control in both apps says the same thing.
    // The key each one answers to, as a keycap: the rich tooltip reads a trailing "(…)"
    // (tipContent's key vocabulary) — the pair said only what they did, not how (user
    // report, with a picture). Browser twin: the same two data-titles in toolbar.js.
    nameBar_.accept->setToolTip("Save name (Enter)");
    sizeToRow(nameBar_.accept);   // ✓/✗ replace ✎/🎨 in edit mode — same box, no jump
    // …but their WIDTH must be free to animate: revealControls slides maximumWidth from 0,
    // and a fixed size pins the minimum too, so the pair simply blinked in and out with no
    // sand at all (the browser's markIn/markOut pair).
    const auto letItSlide = [](QToolButton* b) {
      b->setMinimumWidth(0);
      b->setFixedHeight(kNameChipBox);
      b->setMaximumWidth(kNameChipBox);
    };
    letItSlide(nameBar_.accept);
    nameBar_.accept->setVisible(false);
    nameLay->addWidget(nameBar_.accept);
    nameBar_.cancel = new QToolButton(nameBar_.group);
    nameBar_.cancel->setToolButtonStyle(Qt::ToolButtonIconOnly);
    nameBar_.cancel->setToolTip("Cancel (Esc)");
    sizeToRow(nameBar_.cancel);
    letItSlide(nameBar_.cancel);
    nameBar_.cancel->setVisible(false);
    nameLay->addWidget(nameBar_.cancel);
    // The container goes on the toolbar as one action, AFTER its children exist.
    tbName->addWidget(nameBar_.group);
    // Trailing expanding spacer: absorbs the rest of the row so the label + name + ✎/🎨 stay packed
    // together on the LEFT (no huge gap), instead of the name field stretching across the whole row.
    { auto* sp = new QWidget(this); sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); tbName->addWidget(sp); }
    // The per-project name colour control is NOT a toolbar swatch — it lives in the Project
    // menubar menu (actProjectColor_ / actProjectColorClear_). nameBar_.colorBtn stays null; the
    // active project's colour is still visible because the name field itself is painted in it.
    // textEdited fires only on USER edits (not programmatic setText), so updating the
    // field from updateProjectTitle() never re-triggers validation.
    connect(nameBar_.field, &QLineEdit::textEdited, this,
            [this](const QString&) { refreshProjectNameButtons(); });
    connect(nameBar_.field, &QLineEdit::returnPressed, this, [this] {
      if (nameBar_.editing) commitProjectName();  // commit (no-op if unchanged) + leave edit mode
    });
    connect(nameBar_.accept, &QToolButton::clicked, this, [this] { commitProjectName(); });
    connect(nameBar_.cancel, &QToolButton::clicked, this, [this] { cancelProjectName(); });
    // Escape cancels the edit; clicking away (focus-out) reverts any uncommitted text — both via
    // the event filter below, so the user can always leave the field (Enter still commits).
    nameBar_.field->installEventFilter(this);
    // Hover-reveal the ✎/🎨 group: the CONTAINER is the hover region; children still get
    // their own Enter/Leave (Qt sends the parent a Leave when the cursor moves onto a
    // child), so all four recompute the shared state (eventFilter → updateNameHover,
    // which tests the container's gap-free rect).
    nameBar_.group->installEventFilter(this);
    nameBar_.edit->installEventFilter(this);
    nameBar_.colorBtn->installEventFilter(this);
  }
}  // namespace stencil::gui

