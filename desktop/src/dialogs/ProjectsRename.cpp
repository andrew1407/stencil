#include "ProjectsDialog.hpp"

#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "../support/SearchCombo.hpp"
#include "ProjectsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "ExpirationDialog.hpp"
#include "../support/scrollReveal.hpp"
#include "../support/controlReveal.hpp"
#include "../support/FlowLayout.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/menuReveal.hpp"
#include "../support/MenuShimmer.hpp"
#include "../support/ShimmerOverlay.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "AppTooltip.hpp"
#include "ShimmerOverlay.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPointer>
#include <QSize>
#include <QToolButton>
#include <algorithm>

// Renaming a row in place.

namespace stencil::gui {

  // Inline rename over the row's painted name (browser projectsModal beginRename). Commit emits
  // renameRequested - the dialog STAYS OPEN and the owner repaints it via setProjects().
  void ProjectsDialog::beginInlineRename(QListWidgetItem* it) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local rows only
    if (it->data(DOOMED_ROLE).toBool()) return;
    closeInlineRename();
    if (press_.clickTimer) press_.clickTimer->stop();   // a rename is not an open
    const QString id = it->data(Qt::UserRole).toString();
    const QString current = it->data(Qt::UserRole + 3).toString();

    // The same uniqueness/length rules the browser's inline editor applies.
    auto store = loadedNameStore(projects_);

    const QRect vr = list_->visualItemRect(it);
    auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    QRect nr = del ? del->nameRectFor(list_->row(it)) : QRect();
    if (nr.isEmpty()) nr = QRect(vr.left() + 80, vr.top() + 8, 200, 18);

    renameBox_ = new QWidget(list_->viewport());
    renameBox_->setObjectName("projectsRenameBox");
    auto* lay = new QHBoxLayout(renameBox_);
    lay->setContentsMargins(0, 2, 2, 2);   // even air around the ✓/✗ inside the box
    lay->setSpacing(5);
    // A row's chips are the height of the FIELD beside them: the editor reads as one
    // control, and clears the row's top border instead of straddling it.
    constexpr int RENAME_GLYPH = 12;
    constexpr int RENAME_BOX = 22;
    auto* edit = new QLineEdit(current, renameBox_);
    edit->setObjectName("projectsRenameEdit");
    edit->setToolTip(tr("Project name"));
    edit->setFixedHeight(RENAME_BOX);   // …the chips' own height: one control, three parts
    lay->addWidget(edit, 1);
    // The browser's .name-edit-btn chips: accent-filled with a WHITE glyph (a green tick and a red
    // cross read as a warning). The shared objectName carries their QSS; sized by NAME_CHIP_*.
    auto* okBtn = new QToolButton(renameBox_);
    okBtn->setObjectName("projectsRenameBtn");
    okBtn->setIcon(themedIcon("check", QColor("#ffffff"), RENAME_GLYPH));
    okBtn->setIconSize(QSize(RENAME_GLYPH, RENAME_GLYPH));
    okBtn->setFixedHeight(RENAME_BOX);
    okBtn->setMinimumWidth(0);          // …so revealControls can slide its slot open
    okBtn->setMaximumWidth(RENAME_BOX);
    okBtn->setFocusPolicy(Qt::NoFocus);
    installHoverShimmer(okBtn);   // the row's editor is built on demand, after the dialog's sweep
    lay->addWidget(okBtn);   // its cursor follows enabled/disabled — see makeNameValidator
    auto* cancelBtn = new QToolButton(renameBox_);
    cancelBtn->setObjectName("projectsRenameBtn");
    cancelBtn->setIcon(themedIcon("x", QColor("#ffffff"), RENAME_GLYPH));
    cancelBtn->setIconSize(QSize(RENAME_GLYPH, RENAME_GLYPH));
    cancelBtn->setFixedHeight(RENAME_BOX);
    cancelBtn->setMinimumWidth(0);          // …so revealControls can slide its slot open
    cancelBtn->setMaximumWidth(RENAME_BOX);
    cancelBtn->setFocusPolicy(Qt::NoFocus);
    cancelBtn->setToolTip(tr("Cancel (Esc)"));   // …and its key, as a keycap
    installHoverShimmer(cancelBtn);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    lay->addWidget(cancelBtn);

    // Span from the name's left edge to just short of the "..." strip. The box has to HOLD its tick
    // and cross, so it is at least a chip plus its air and wide enough for the field and both.
    const int left = nr.left() - 4;
    const int chips = 2 * RENAME_BOX + 3 * lay->spacing();
    const int width = std::max(160 + chips, kebabZone(vr).left() - 8 - left);
    const int h = RENAME_BOX + 4;   // the field's height plus the box's own air
    // …centred on the name line, but never across the row's own edges.
    const int top = std::clamp(nr.center().y() - h / 2, vr.top() + 3, vr.bottom() - h - 3);
    renameBox_->setGeometry(left, top, width, h);
    // The ✓/✗ FORM from dust once the editor is up (browser markIn parity): hidden
    // before show, then revealed — their slots open under the gathering motes.
    okBtn->hide();
    cancelBtn->hide();
    renameBox_->show();
    revealControls(okBtn, true);
    revealControls(cancelBtn, true);
    edit->setFocus();
    edit->selectAll();
    edit->installEventFilter(this);   // Esc cancels, focus loss discards (see eventFilter)

    // The shared ✓-enable/tooltip validation (no rest-state tooltip on the chips —
    // user decision, browser look).
    const auto revalidate = makeNameValidator(store, edit, okBtn, id, current);
    connect(edit, &QLineEdit::textChanged, renameBox_, [revalidate](const QString&) { revalidate(); });
    revalidate();
    QPointer<ProjectsDialog> self(this);
    auto commit = [this, self, store, edit, id, current] {
      if (!self) return;
      const QString name = edit->text().trimmed();
      if (name == current) { closeInlineRename(); return; }
      if (!store->validateName(name.toStdString(), id.toStdString()).ok) return;
      closeInlineRename();
      emit renameRequested(id, name);   // the owner renames, then calls setProjects()
    };
    connect(okBtn, &QToolButton::clicked, renameBox_, commit);
    connect(cancelBtn, &QToolButton::clicked, renameBox_, [this, self] { if (self) closeInlineRename(); });
    connect(edit, &QLineEdit::returnPressed, renameBox_, commit);
  }

  void ProjectsDialog::closeInlineRename() {
    if (!renameBox_) return;
    QWidget* box = renameBox_;
    renameBox_ = nullptr;   // cleared FIRST — hiding fires the edit's FocusOut back into us
    // The ✓/✗ come apart as dust (browser markOut parity): the flight is a snapshot on
    // the dialog window, so the editor itself still goes away NOW.
    for (QToolButton* b : box->findChildren<QToolButton*>()) revealControls(b, false);
    box->hide();
    box->deleteLater();
  }

}  // namespace stencil::gui
