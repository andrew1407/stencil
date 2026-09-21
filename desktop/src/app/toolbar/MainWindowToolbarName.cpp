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
#include "../../support/control/controlReveal.hpp"   // section buttons come and go as sand
#include "../../support/icon/iconMotion.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include "../../support/control/WrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

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
    nameBar.group = new QWidget(this);
    auto* nameLay = new QHBoxLayout(nameBar.group);
    nameLay->setContentsMargins(0, 0, 0, 0);
    // Browser twin: .project-name-field's `gap`.
    nameLay->setSpacing(8);
    nameBar.field = new QLineEdit(nameBar.group);
    nameBar.field->setObjectName("projectNameField");   // theme.cpp: no hover ring on a title
    nameBar.field->setPlaceholderText("No project");
    nameBar.field->setToolTip(QString());   // no tooltip on the name field (the ✎ button has its own)
    nameBar.field->setMinimumWidth(150);
    nameBar.field->setMaximumWidth(300);
    // A QLineEdit is Expanding by default and would stretch across the row; a trailing spacer
    // absorbs the rest.
    nameBar.field->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    nameBar.field->setEnabled(false);
    nameBar.field->setReadOnly(true);  // browser-like: read-only until edit mode (✎ / double-click)
    nameLay->addWidget(nameBar.field);
    // "?" status hint, a label (browser #hints-btn), after ✎/🎨 in browser order.
    statusHint = new QLabel(this);
    statusHint->setObjectName("statusHint");
    statusHint->setText(QStringLiteral("?"));
    statusHint->setAlignment(Qt::AlignCenter);
    statusHint->setFixedSize(18, 18);
    statusHint->setFocusPolicy(Qt::NoFocus);
    statusHint->setAttribute(Qt::WA_TransparentForMouseEvents, false);   // hover still shows the tip
    statusHint->setStyleSheet(
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
    nameBar.edit = new QToolButton(nameBar.group);
    nameBar.edit->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(nameBar.edit);
    nameBar.edit->setAutoRaise(true);
    nameBar.edit->setToolTip("Rename project");
    setTipHotkey(nameBar.edit, actRenameProject);   // browser #project-name-edit: its chord as the keycap
    nameBar.edit->setEnabled(false);
    nameLay->addWidget(nameBar.edit);
    connect(nameBar.edit, &QToolButton::clicked, this, [this] { enterNameEdit(); });
    nameBar.colorBtn = new QToolButton(nameBar.group);
    nameBar.colorBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(nameBar.colorBtn);
    nameBar.colorBtn->setAutoRaise(true);
    nameBar.colorBtn->setToolTip("Project color — paints the project name");
    nameBar.colorBtn->setEnabled(false);
    nameLay->addWidget(nameBar.colorBtn);
    // The menu runs its own loop and fully closes before the picker opens (deferred), so no stray
    // grab dismisses the dialog.
    connect(nameBar.colorBtn, &QToolButton::clicked, this, [this] { showProjectColorMenu(); });
    statusHintAction = tbName->addWidget(statusHint);
    statusHintAction->setVisible(false);
    nameBar.colorBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(nameBar.colorBtn, &QToolButton::customContextMenuRequested, this,
            [this](const QPoint&) { setActiveProjectColor(QString()); });
    nameBar.accept = new QToolButton(nameBar.group);
    nameBar.accept->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Hover text and the keycap suffix are shared with the browser's toolbar.js (#project-name-
    // accept / #project-name-cancel).
    nameBar.accept->setToolTip("Save name (Enter)");
    sizeToRow(nameBar.accept);   // ✓/✗ replace ✎/🎨 in edit mode — same box, no jump
    // Width must stay free to animate: revealControls slides maximumWidth from 0, and a fixed size
    // pins the minimum too.
    const auto letItSlide = [](QToolButton* b) {
      b->setMinimumWidth(0);
      b->setFixedHeight(NAME_CHIP_BOX);
      b->setMaximumWidth(NAME_CHIP_BOX);
    };
    letItSlide(nameBar.accept);
    nameBar.accept->setVisible(false);
    nameLay->addWidget(nameBar.accept);
    nameBar.cancel = new QToolButton(nameBar.group);
    nameBar.cancel->setToolButtonStyle(Qt::ToolButtonIconOnly);
    nameBar.cancel->setToolTip("Cancel (Esc)");
    sizeToRow(nameBar.cancel);
    letItSlide(nameBar.cancel);
    nameBar.cancel->setVisible(false);
    nameLay->addWidget(nameBar.cancel);
    tbName->addWidget(nameBar.group);
    { auto* sp = new QWidget(this); sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); tbName->addWidget(sp); }
    // The per-project colour control lives in the Project menu (actProjectColor).
    // textEdited fires only on user edits, so updateProjectTitle() never re-triggers validation.
    connect(nameBar.field, &QLineEdit::textEdited, this,
            [this](const QString&) { refreshProjectNameButtons(); });
    connect(nameBar.field, &QLineEdit::returnPressed, this, [this] {
      if (nameBar.editing) commitProjectName();  // commit (no-op if unchanged) + leave edit mode
    });
    connect(nameBar.accept, &QToolButton::clicked, this, [this] { commitProjectName(); });
    connect(nameBar.cancel, &QToolButton::clicked, this, [this] { cancelProjectName(); });
    // Escape cancels, focus-out reverts — both via the event filter below.
    nameBar.field->installEventFilter(this);
    // The container is the hover region; children still get their own Enter/Leave, so all four
    // recompute via updateNameHover.
    nameBar.group->installEventFilter(this);
    nameBar.edit->installEventFilter(this);
    nameBar.colorBtn->installEventFilter(this);
  }
}  // namespace stencil::gui

