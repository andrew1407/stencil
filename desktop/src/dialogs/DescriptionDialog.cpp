#include "DescriptionDialog.hpp"
#include "projectMetaDialog.hpp"

#include <QKeyEvent>
#include <QPlainTextEdit>

namespace stencil::gui {

  DescriptionDialog::DescriptionDialog(const QString& current, QWidget* parent)
      : QDialog(parent) {
    edit_ = buildProjectMetaDialog(this,
                                   {QStringLiteral("description"), QStringLiteral("description"),
                                    tr("Project description"), tr("Describe this project…"),
                                    tr("Shown in the projects list and its tooltip."),
                                    tr("Cancel"), tr("Save"), 5},
                                   current);
    edit_->installEventFilter(this);   // Ctrl/⌘+Enter saves from inside the area
  }

  // The area owns plain Enter (a newline); Ctrl/⌘+Enter saves, Escape cancels (QDialog).
  bool DescriptionDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == edit_ && event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
          && (ke->modifiers() & Qt::ControlModifier)) {
        accept();
        return true;
      }
    }
    return QDialog::eventFilter(obj, event);
  }

  QString DescriptionDialog::text() const {
    return edit_->toPlainText().trimmed().left(MAX_CHARS);
  }

  bool DescriptionDialog::apply(std::vector<Project>& projects, const QString& id,
                                const QString& text, long long now) {
    return applyToProjectMeta(projects, id, now,
                              [&](core::ProjectMeta& m) { m.description = text.toStdString(); });
  }

}  // namespace stencil::gui
