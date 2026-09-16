#include "KeywordsDialog.hpp"
#include "KeywordChips.hpp"
#include "projectMetaDialog.hpp"

#include <QLineEdit>

namespace stencil::gui {

  KeywordsDialog::KeywordsDialog(const QStringList& current, QWidget* parent)
      : QDialog(parent) {
    const ProjectMetaDialogSpec spec{
        QStringLiteral("keywords"), QStringLiteral("keywords"), tr("Project keywords"),
        QString(), QString(), tr("Cancel"), tr("Save"), 5};
    ModalChrome chrome = startProjectMetaDialog(this, spec);
    chips_ = new KeywordChips(current, this);
    chrome.body->addWidget(chips_, 1);
    finishProjectMetaDialog(this, chrome, spec, chips_->clearButton(), chips_->input());
    chips_->input()->setFocus();
  }

  QStringList KeywordsDialog::keywords() const { return chips_->keywords(); }

  bool KeywordsDialog::apply(std::vector<Project>& projects, const QString& id,
                             const QStringList& keywords, long long now) {
    return applyToProjectMeta(projects, id, now, [&](core::ProjectMeta& m) {
      m.keywords.clear();
      for (const QString& k : keywords) m.keywords.push_back(k.toStdString());
    });
  }

}  // namespace stencil::gui
