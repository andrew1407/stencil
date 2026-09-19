#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "iconSet.hpp"
#include "LogoHoverFx.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "SearchCombo.hpp"
#include "ControlsPill.hpp"
#include "OpenImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"   // section buttons come and go as sand
#include "../support/iconMotion.hpp"
#include "../support/ShimmerOverlay.hpp"
#include "../support/WrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

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

// MainWindow's toolbar assembly: the project-name field and its affordances.

namespace stencil::gui {

  // Project name field + inline-rename ✓/✗ (browser topbar): ✓ only for a changed, valid (non-
  // empty, ≤80, unique) name.
  void MainWindow::buildProjectNameGroup(QToolBar* tbName) {
    // One container (browser .project-name-field): hover is its gap-free rect, so sweeping between
    // field and chips never replays the reveal.
    nameBar_.group = new QWidget(this);
    auto* nameLay = new QHBoxLayout(nameBar_.group);
    nameLay->setContentsMargins(0, 0, 0, 0);
    // Browser twin: .project-name-field's `gap`.
    nameLay->setSpacing(8);
    nameBar_.field = new QLineEdit(nameBar_.group);
    nameBar_.field->setObjectName("projectNameField");   // theme.cpp: no hover ring on a title
    nameBar_.field->setPlaceholderText("No project");
    nameBar_.field->setToolTip(QString());   // no tooltip on the name field (the ✎ button has its own)
    nameBar_.field->setMinimumWidth(150);
    nameBar_.field->setMaximumWidth(300);
    // A QLineEdit is Expanding by default and would stretch across the row; a trailing spacer
    // absorbs the rest.
    nameBar_.field->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    nameBar_.field->setEnabled(false);
    nameBar_.field->setReadOnly(true);  // browser-like: read-only until edit mode (✎ / double-click)
    nameLay->addWidget(nameBar_.field);
    // "?" status hint, a label (browser #hints-btn), after ✎/🎨 in browser order.
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
    // Fixed 26px chips (the hover reveal would grow the header row otherwise); QSS half:
    // QToolButton[nameAffordance] in theme.cpp.
    const auto sizeToRow = [](QToolButton* b) {
      // Built after the sweep that installs the shimmer across the toolbar rows, so it is added by
      // hand here.
      installHoverShimmer(b);
      b->setFixedSize(NAME_CHIP_BOX, NAME_CHIP_BOX);
      b->setIconSize(QSize(NAME_CHIP_GLYPH, NAME_CHIP_GLYPH));
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
    // The menu runs its own loop and fully closes before the picker opens (deferred), so no stray
    // grab dismisses the dialog.
    connect(nameBar_.colorBtn, &QToolButton::clicked, this, [this] { showProjectColorMenu(); });
    statusHintAction_ = tbName->addWidget(statusHint_);
    statusHintAction_->setVisible(false);
    nameBar_.colorBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(nameBar_.colorBtn, &QToolButton::customContextMenuRequested, this,
            [this](const QPoint&) { setActiveProjectColor(QString()); });
    nameBar_.accept = new QToolButton(nameBar_.group);
    nameBar_.accept->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Hover text and the keycap suffix are shared with the browser's toolbar.js (#project-name-
    // accept / #project-name-cancel).
    nameBar_.accept->setToolTip("Save name (Enter)");
    sizeToRow(nameBar_.accept);   // ✓/✗ replace ✎/🎨 in edit mode — same box, no jump
    // Width must stay free to animate: revealControls slides maximumWidth from 0, and a fixed size
    // pins the minimum too.
    const auto letItSlide = [](QToolButton* b) {
      b->setMinimumWidth(0);
      b->setFixedHeight(NAME_CHIP_BOX);
      b->setMaximumWidth(NAME_CHIP_BOX);
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
    tbName->addWidget(nameBar_.group);
    { auto* sp = new QWidget(this); sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); tbName->addWidget(sp); }
    // The per-project colour control lives in the Project menu (actProjectColor_).
    // textEdited fires only on user edits, so updateProjectTitle() never re-triggers validation.
    connect(nameBar_.field, &QLineEdit::textEdited, this,
            [this](const QString&) { refreshProjectNameButtons(); });
    connect(nameBar_.field, &QLineEdit::returnPressed, this, [this] {
      if (nameBar_.editing) commitProjectName();  // commit (no-op if unchanged) + leave edit mode
    });
    connect(nameBar_.accept, &QToolButton::clicked, this, [this] { commitProjectName(); });
    connect(nameBar_.cancel, &QToolButton::clicked, this, [this] { cancelProjectName(); });
    // Escape cancels, focus-out reverts — both via the event filter below.
    nameBar_.field->installEventFilter(this);
    // The container is the hover region; children still get their own Enter/Leave, so all four
    // recompute via updateNameHover.
    nameBar_.group->installEventFilter(this);
    nameBar_.edit->installEventFilter(this);
    nameBar_.colorBtn->installEventFilter(this);
  }
}  // namespace stencil::gui

