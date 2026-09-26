#include "AssistantSettingsDialog.hpp"
#include "LlmSettingsForm.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/easeWindowHeight.hpp"
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  AssistantSettingsDialog::AssistantSettingsDialog(const Settings& current,
                                                   QWidget* parent)
      : QDialog(parent), base(current) {
    setObjectName("assistantSettingsDialog");
    setWindowTitle("Assistant settings");

    // The browser modal's shell: the cogged bubble + "Assistant settings" + the Close pill over a
    // hairline, the form as the body, footer hint + Cancel/Save under a second hairline (modal.js).
    ModalChrome chrome = installModalChrome(this, "chat-settings", tr("Assistant settings"));
    form = new LlmSettingsForm(current, LlmSettingsForm::RowMode::HIDE_ROWS, this);
    chrome.body->addWidget(form);

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

    // The browser .app-modal width, PINNED: under a SetMinimumSize layout constraint the dialog kept
    // shrinking to its content (the constraint rewrites the minimum every pass), so it is gone.
    setFixedWidth(MODAL_WIDTH);
    form->focusProvider();
  }

  // The notes wrap at their real width only once every nested layout has run, so the height is
  // taken here, before the window is mapped and the reveal photographs it (easeWindowHeight.hpp).
  void AssistantSettingsDialog::showEvent(QShowEvent* event) {
    support::relayout(this);
    form->pinNoteHeights();
    support::relayout(this);
    support::pinWidthMinimum(this);
    resize(width(), support::naturalHeight(this));
    QDialog::showEvent(event);
  }

  Settings AssistantSettingsDialog::result() const {
    Settings s = base;  // everything this dialog doesn't edit rides through
    form->applyTo(s);
    return s;
  }

}
