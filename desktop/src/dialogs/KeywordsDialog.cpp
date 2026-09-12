#include "KeywordsDialog.hpp"
#include "projectMetaDialog.hpp"

#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>

namespace stencil::gui {

  KeywordsDialog::KeywordsDialog(const QStringList& current, QWidget* parent)
      : QDialog(parent) {
    edit_ = buildProjectMetaDialog(this,
                                   {QStringLiteral("keywords"), QStringLiteral("keywords"),
                                    tr("Project keywords"), tr("keyword, another keyword…"),
                                    tr("Comma or space separated · used by the projects search."),
                                    tr("Cancel"), tr("Save"), 3},
                                   current.join(QLatin1String(", ")));
    edit_->installEventFilter(this);
  }

  // A keyword list is one line: Enter saves it (browser parity), Shift+Enter still
  // types a newline for anyone pasting a column of words.
  bool KeywordsDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == edit_ && event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
          && !(ke->modifiers() & Qt::ShiftModifier)) {
        accept();
        return true;
      }
    }
    return QDialog::eventFilter(obj, event);
  }

  QStringList KeywordsDialog::keywords() const { return parse(edit_->toPlainText()); }

  QStringList KeywordsDialog::parse(const QString& raw) {
    QStringList out;
    for (const QString& word : raw.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts)) {
      const QString k = word.toLower();
      if (!out.contains(k)) out << k;
    }
    return out;
  }

  bool KeywordsDialog::apply(std::vector<Project>& projects, const QString& id,
                             const QStringList& keywords, long long now) {
    return applyToProjectMeta(projects, id, now, [&](core::ProjectMeta& m) {
      m.keywords.clear();
      for (const QString& k : keywords) m.keywords.push_back(k.toStdString());
    });
  }

}  // namespace stencil::gui
