#include "AssistantSettingsDialog.hpp"
#include "LlmSettingsForm.hpp"
#include "../support/modalChrome.hpp"
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  AssistantSettingsDialog::AssistantSettingsDialog(const Settings& current,
                                                   QWidget* parent)
      : QDialog(parent), base_(current) {
    setObjectName("assistantSettingsDialog");
    setWindowTitle("Assistant");

    // The browser modal's shell: sparkle + "Assistant" + the ✕ Close pill over a
    // hairline, the form as the body, and the footer hint + Cancel/Save CTAs
    // under a second hairline (llmSettingsModal.js structure).
    ModalChrome chrome = installModalChrome(this, "sparkle", tr("Assistant"));
    form_ = new LlmSettingsForm(current, LlmSettingsForm::RowMode::HIDE_ROWS, this);
    chrome.body->addWidget(form_);

    QHBoxLayout* footer = addModalFooter(
        chrome, tr("The assistant only plans Stencil operations — endpoints "
                   "come from this dialog alone."));
    auto* cancel = new QPushButton(tr("Cancel"), this);
    makeModalCta(cancel, "x");
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancel);
    auto* save = new QPushButton(tr("Save"), this);
    makeModalCta(save, "check");
    save->setDefault(true);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(save);

    // The browser .app-modal width, PINNED (confirmModal does the same): under a
    // SetMinimumSize layout constraint the dialog kept shrinking to its content —
    // the constraint rewrites the widget's minimum every pass, clobbering even a
    // setFixedWidth — so the constraint is gone and the width holds; the height
    // still follows the visible rows (LlmSettingsForm::syncRows adjustSize()).
    setFixedWidth(MODAL_WIDTH);
    form_->focusProvider();
  }

  Settings AssistantSettingsDialog::result() const {
    Settings s = base_;  // everything this dialog doesn't edit rides through
    form_->applyTo(s);
    return s;
  }

}
