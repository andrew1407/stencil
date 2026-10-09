#include "MainWindow.hpp"
#include "windowSheets.hpp"
#include "ProjectTitleController.hpp"
#include "ToolbarBuilder.hpp"
#include "mainWindowHelpers.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include "tipContent.hpp"
#include "colorDrag.hpp"
#include "../../support/control/dblReset.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolBar>

// MainWindow's toolbar assembly: the project-name field and its affordances.

namespace stencil::gui {

  // Project name field + inline-rename ✓/✗ (browser topbar): ✓ only for a changed, valid (non-
  // empty, ≤80, unique) name.
  void ToolbarBuilder::buildProjectNameGroup(QToolBar* tbName) {
    // One container (browser .project-name-field): hover is its gap-free rect, so sweeping between
    // field and chips never replays the reveal.
    w.nameBar.group = new QWidget(&w);
    auto* nameLay = new QHBoxLayout(w.nameBar.group);
    nameLay->setContentsMargins(0, 0, 0, 0);
    // Browser twin: .project-name-field's `gap`.
    nameLay->setSpacing(8);
    w.nameBar.field = new QLineEdit(w.nameBar.group);
    w.nameBar.field->setObjectName("projectNameField");   // theme.cpp: no hover ring on a title
    w.nameBar.field->setPlaceholderText("No project");
    w.nameBar.field->setToolTip(QString());   // no tooltip on the name field (the ✎ button has its own)
    w.nameBar.field->setMinimumWidth(150);
    w.nameBar.field->setMaximumWidth(300);
    // A QLineEdit is Expanding by default and would stretch across the row; a trailing spacer
    // absorbs the rest.
    w.nameBar.field->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    w.nameBar.field->setEnabled(false);
    w.nameBar.field->setReadOnly(true);  // browser-like: read-only until edit mode (✎ / double-click)
    nameLay->addWidget(w.nameBar.field);
    // "?" status hint, a label (browser #hints-btn), after ✎/🎨 in browser order.
    w.tools.statusHint = new QLabel(&w);
    w.tools.statusHint->setObjectName("statusHint");
    w.tools.statusHint->setText(QStringLiteral("?"));
    w.tools.statusHint->setAlignment(Qt::AlignCenter);
    w.tools.statusHint->setFixedSize(18, 18);
    w.tools.statusHint->setFocusPolicy(Qt::NoFocus);
    w.tools.statusHint->setAttribute(Qt::WA_TransparentForMouseEvents, false);   // hover still shows the tip
    w.tools.statusHint->setStyleSheet(support::statusHintSheet());
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
    w.nameBar.edit = new QToolButton(w.nameBar.group);
    w.nameBar.edit->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(w.nameBar.edit);
    w.nameBar.edit->setAutoRaise(true);
    w.nameBar.edit->setToolTip("Rename project");
    setTipHotkey(w.nameBar.edit, w.acts.renameProject);   // browser #project-name-edit: its chord as the keycap
    w.nameBar.edit->setEnabled(false);
    nameLay->addWidget(w.nameBar.edit);
    QObject::connect(w.nameBar.edit, &QToolButton::clicked, &w, [this] { w.projectTitle->enterNameEdit(); });
    w.nameBar.colorBtn = new QToolButton(w.nameBar.group);
    w.nameBar.colorBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(w.nameBar.colorBtn);
    w.nameBar.colorBtn->setAutoRaise(true);
    w.nameBar.colorBtn->setToolTip("Project color — paints the project name");
    w.nameBar.colorBtn->setEnabled(false);
    nameLay->addWidget(w.nameBar.colorBtn);
    // The menu runs its own loop and fully closes before the picker opens (deferred), so no stray
    // grab dismisses the dialog.
    support::wireColorChip(w.nameBar.colorBtn, [this] { w.parts.projects.showProjectColorMenu(); },
                           [this] { w.parts.projects.setActiveProjectColor(QString()); });
    support::installColorDrag(w.nameBar.colorBtn, {[this] { return w.parts.projects.projectNameColor(); },
                                                   [this](const QColor& c) { w.parts.projects.setActiveProjectColor(c.name()); }});
    w.tools.statusHintAction = tbName->addWidget(w.tools.statusHint);
    w.tools.statusHintAction->setVisible(false);
    w.nameBar.colorBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(w.nameBar.colorBtn, &QToolButton::customContextMenuRequested, &w,
                     [this](const QPoint&) { w.parts.projects.setActiveProjectColor(QString()); });
    w.nameBar.accept = new QToolButton(w.nameBar.group);
    w.nameBar.accept->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Hover text and the keycap suffix are shared with the browser's toolbar.js (#project-name-
    // accept / #project-name-cancel).
    w.nameBar.accept->setToolTip("Save name (Enter)");
    sizeToRow(w.nameBar.accept);   // ✓/✗ replace ✎/🎨 in edit mode — same box, no jump
    // Width must stay free to animate: revealControls slides maximumWidth from 0, and a fixed size
    // pins the minimum too.
    const auto letItSlide = [](QToolButton* b) {
      b->setMinimumWidth(0);
      b->setFixedHeight(NAME_CHIP_BOX);
      b->setMaximumWidth(NAME_CHIP_BOX);
    };
    letItSlide(w.nameBar.accept);
    w.nameBar.accept->setVisible(false);
    nameLay->addWidget(w.nameBar.accept);
    w.nameBar.cancel = new QToolButton(w.nameBar.group);
    w.nameBar.cancel->setToolButtonStyle(Qt::ToolButtonIconOnly);
    w.nameBar.cancel->setToolTip("Cancel (Esc)");
    sizeToRow(w.nameBar.cancel);
    letItSlide(w.nameBar.cancel);
    w.nameBar.cancel->setVisible(false);
    nameLay->addWidget(w.nameBar.cancel);
    tbName->addWidget(w.nameBar.group);
    { auto* sp = new QWidget(&w); sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); tbName->addWidget(sp); }
    // The per-project colour control lives in the Project menu (acts.projectColor).
    // textEdited fires only on user edits, so updateProjectTitle() never re-triggers validation.
    QObject::connect(w.nameBar.field, &QLineEdit::textEdited, &w,
                     [this](const QString&) { w.projectTitle->refreshProjectNameButtons(); });
    QObject::connect(w.nameBar.field, &QLineEdit::returnPressed, &w, [this] {
      if (w.nameBar.editing) w.projectTitle->commitProjectName();  // commit (no-op if unchanged) + leave edit mode
    });
    QObject::connect(w.nameBar.accept, &QToolButton::clicked, &w, [this] { w.projectTitle->commitProjectName(); });
    QObject::connect(w.nameBar.cancel, &QToolButton::clicked, &w, [this] { w.projectTitle->cancelProjectName(); });
    // Escape cancels, focus-out reverts — both via the event filter below.
    w.nameBar.field->installEventFilter(&w);
    // The container is the hover region; children still get their own Enter/Leave, so all four
    // recompute via updateNameHover.
    w.nameBar.group->installEventFilter(&w);
    w.nameBar.edit->installEventFilter(&w);
    w.nameBar.colorBtn->installEventFilter(&w);
  }
}  // namespace stencil::gui

