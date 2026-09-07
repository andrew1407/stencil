#pragma once
// The shared half of the two project-meta editors (descriptionDialog, keywordsDialog —
// browser projectMetaModal.js parity): the modal shell around one text area with a
// Cancel / Save footer, and the store write that finds a project by id, stamps
// updatedAt and persists. Only the field's size, wording and Enter rule differ.
#include "fileStore.hpp"
#include "../support/modalChrome.hpp"

#include <QDialog>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <vector>

namespace stencil::gui {

  struct ProjectMetaDialogSpec {
    QString name;          // object-name stem: "description" → stencilDescriptionDialog
    QString glyph;         // header icon (modalChrome installModalChrome)
    QString title;         // header title, already translated
    QString placeholder;   // the empty field's prompt, already translated
    QString hint;          // footer hint, already translated
    QString cancelLabel;
    QString saveLabel;
    int rows = 5;          // the field's height, in lines of its own metrics
  };

  // Build the shell on `dlg` (which must have no layout yet) and return its text area,
  // focused with the caret at the end. `onAccept`/`onReject` are the dialog's own slots.
  inline QPlainTextEdit* buildProjectMetaDialog(QDialog* dlg, const ProjectMetaDialogSpec& spec,
                                                const QString& current) {
    const QString stem = spec.name.left(1).toUpper() + spec.name.mid(1);
    dlg->setObjectName(QStringLiteral("stencil%1Dialog").arg(stem));
    dlg->setWindowTitle(spec.title);
    ModalChrome chrome = installModalChrome(dlg, spec.glyph, spec.title);

    auto* edit = new QPlainTextEdit(current, dlg);
    edit->setObjectName(QStringLiteral("%1Text").arg(spec.name));
    edit->setPlaceholderText(spec.placeholder);
    edit->setTabChangesFocus(true);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // wrapped text never needs one
    // `rows` lines of the field's own metrics plus its frame (promptModal's recipe).
    edit->setFixedHeight(edit->fontMetrics().lineSpacing() * spec.rows + 16);
    chrome.body->addWidget(edit);
    chrome.body->addStretch(1);

    QHBoxLayout* footer = addModalFooter(chrome, spec.hint);
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

    dlg->setFixedWidth(kModalWidth);
    dlg->adjustSize();
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
