#pragma once
// The shared half of the two project-meta editors (descriptionDialog, keywordsDialog —
// browser projectMetaModal.js parity): the modal shell around one field with a
// Cancel / Save footer, and the store write that finds a project by id, stamps
// updatedAt and persists. Only the field, its wording and its Enter rule differ.
#include "fileStore.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QDialog>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QEvent>
#include <QFrame>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QString>
#include <QVBoxLayout>
#include <algorithm>
#include <vector>

namespace stencil::gui {

  // Browser .modal-popover .app-modal: the compact shape is 420px wide whatever it holds, and takes
  // that width from sizeHint - so the FIELD has to ask for it.
  inline constexpr int POPOVER_W = 420;
  // Less the shell's 1px inset, PAD_X and the metaFieldRing's 3px border, per side.
  inline constexpr int META_FIELD_MIN_W = POPOVER_W - 2 * (18 + 1) - 2 * 3;

  struct ProjectMetaDialogSpec {
    QString name;          // object-name stem: "description" → stencilDescriptionDialog
    QString glyph;         // header icon (modalChrome installModalChrome)
    QString title;         // header title, already translated
    QString placeholder;   // the empty field's prompt, already translated
    QString hint;          // footer hint, already translated
    QString cancelLabel;
    QString saveLabel;
    int rows = 5;          // the field's FLOOR, in lines of its own metrics
    // >0 overrides `rows`: an explicit floor in px, so two meta windows whose bodies hold
    // different things still open at the SAME height (browser --meta-field-floor).
    int fieldFloorPx = 0;
  };

  // A filter, not a signal: QWidget has no focusChanged of its own.
  struct FieldRingWatch : QObject {
    QFrame* ring;
    explicit FieldRingWatch(QFrame* r) : QObject(r), ring(r) {}
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut) {
        ring->setProperty("ringOn", e->type() == QEvent::FocusIn);
        ring->style()->unpolish(ring);
        ring->style()->polish(ring);
      }
      return QObject::eventFilter(o, e);
    }
  };

  // Wraps `field` in the browser's focus ring - a thin opaque accent contour inside a thicker
  // translucent one, which QSS cannot paint as a box-shadow. Always on for a field that never focuses.
  inline QFrame* fieldRing(QWidget* field, QWidget* parent, bool alwaysOn = false) {
    auto* ring = new QFrame(parent);
    ring->setObjectName(QStringLiteral("metaFieldRing"));
    auto* col = new QVBoxLayout(ring);
    col->setContentsMargins(0, 0, 0, 0);
    col->addWidget(field);
    if (alwaysOn) ring->setProperty("ringOn", true);
    else field->installEventFilter(new FieldRingWatch(ring));
    return ring;
  }

  // The header and the naming, on a dialog with no layout yet. The caller fills
  // `chrome.body`, then calls finishProjectMetaDialog.
  inline ModalChrome startProjectMetaDialog(QDialog* dlg, const ProjectMetaDialogSpec& spec) {
    const QString stem = spec.name.left(1).toUpper() + spec.name.mid(1);
    dlg->setObjectName(QStringLiteral("stencil%1Dialog").arg(stem));
    dlg->setWindowTitle(spec.title);
    return installModalChrome(dlg, spec.glyph, spec.title);
  }

  // The Cancel / Save footer, then the size: a fixed width, opening 54% of the screen tall (at most
  // 478px). The body's field takes every pixel gained. `extra` is the field's own verb, before Cancel.
  inline void finishProjectMetaDialog(QDialog* dlg, ModalChrome& chrome,
                                      const ProjectMetaDialogSpec& spec,
                                      QWidget* extra = nullptr, QWidget* focusTarget = nullptr) {
    QHBoxLayout* footer = addModalFooter(chrome, spec.hint);
    if (extra) footer->addWidget(extra);
    auto* cancelBtn = new QPushButton(spec.cancelLabel, dlg);
    makeModalCta(cancelBtn, QStringLiteral("x"));
    cancelBtn->setAutoDefault(false);
    footer->addWidget(cancelBtn);
    auto* saveBtn = new QPushButton(spec.saveLabel, dlg);
    saveBtn->setObjectName(QStringLiteral("%1Save").arg(spec.name));
    makeModalCta(saveBtn, QStringLiteral("check"));
    saveBtn->setAutoDefault(false);
    footer->addWidget(saveBtn);
    QObject::connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    QObject::connect(saveBtn, &QPushButton::clicked, dlg, &QDialog::accept);

    const QRect screen = dlg->screen() ? dlg->screen()->availableGeometry() : QRect(0, 0, 900, 900);
    // A MINIMUM, never a fixed width (sizeModalTall's rule): a fixed one leaves the window resizable
    // only vertically, so its side and corner grips go dead. The floor is the compact shape's width.
    dlg->setMinimumWidth(POPOVER_W);
    // execMaybePopover reparents the dialog into its overlay, which drops the focus the constructor
    // set. A 0-timer lands once the loop is running, after any reparenting.
    if (focusTarget)
      QTimer::singleShot(0, focusTarget, [focusTarget] { focusTarget->setFocus(); });
    dlg->resize(MODAL_WIDTH, std::min(478, int(screen.height() * 0.54)));
  }

  // Build the shell on `dlg` around one text area and return it, focused with the caret
  // at the end.
  inline QPlainTextEdit* buildProjectMetaDialog(QDialog* dlg, const ProjectMetaDialogSpec& spec,
                                                const QString& current) {
    ModalChrome chrome = startProjectMetaDialog(dlg, spec);

    auto* edit = new QPlainTextEdit(current, dlg);
    edit->setObjectName(QStringLiteral("%1Text").arg(spec.name));
    edit->setPlaceholderText(spec.placeholder);
    edit->setTabChangesFocus(true);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // wrapped text never needs one
    // A FLOOR of `rows` lines, never a fixed height: the area takes the body's slack, so
    // growing the window grows what is being edited (browser .meta-text flex: 1).
    edit->setMinimumHeight(spec.fieldFloorPx > 0 ? spec.fieldFloorPx
                                                 : edit->fontMetrics().lineSpacing() * spec.rows + 16);
    edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    edit->setMinimumWidth(META_FIELD_MIN_W);
    chrome.body->addWidget(fieldRing(edit, dlg), 1);

    finishProjectMetaDialog(dlg, chrome, spec, nullptr, edit);
    edit->setFocus();
    edit->moveCursor(QTextCursor::End);
    return edit;
  }

  // Run `mutate` on project `id`'s meta, stamp updatedAt and persist the list — the SAME
  // store write the Projects window's row menu makes. False when no project has that id.
  template <class Mutate>
  bool applyToProjectMeta(std::vector<Project>& projects, const QString& id, long long now,
                          Mutate&& mutate) {
    for (auto& p : projects) {
      if (QString::fromStdString(p.meta.id) != id) continue;
      mutate(p.meta);
      p.meta.updatedAt = now;
      fileStore::saveProjects(projects);
      return true;
    }
    return false;
  }

}  // namespace stencil::gui
