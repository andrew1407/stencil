#include "assistantSettingsDialog.hpp"
#include "guiHelpers.hpp"
#include "llmSettingsForm.hpp"
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace stencil::gui {

  AssistantSettingsDialog::AssistantSettingsDialog(const Settings& current,
                                                   QWidget* parent)
      : QDialog(parent), base_(current) {
    setObjectName("assistantSettingsDialog");
    setWindowTitle("Assistant");
    setMinimumWidth(420);

    form_ = new LlmSettingsForm(current, LlmSettingsForm::RowMode::HideRows, this);

    // Browser modal's settings-footer line, rendered muted below the form.
    auto* footer = new QLabel(
        "The assistant plans Stencil operations only — it never edits pixels "
        "directly, and endpoints come only from this dialog.",
        this);
    footer->setObjectName("assistantFooterHint");
    footer->setWordWrap(true);
    footer->setStyleSheet("color: palette(mid); font-size: 11px;");

    auto* buttons =
        makeButtonBox(this, QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* layout = new QVBoxLayout(this);
    // Never let the window get smaller than the rows need — a too-small size
    // paints the form rows on top of each other.
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->setSpacing(10);
    layout->addWidget(form_);
    layout->addWidget(footer);
    layout->addWidget(buttons);
    form_->focusProvider();
  }

  Settings AssistantSettingsDialog::result() const {
    Settings s = base_;  // everything this dialog doesn't edit rides through
    form_->applyTo(s);
    return s;
  }

}
