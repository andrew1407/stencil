#pragma once
#include "fileStore.hpp"
#include <QDialog>

// Assistant-only settings editor — the desktop counterpart of the browser's dedicated
// "Assistant" modal (llmSettingsModal.js), opened by the chat dock's gear: the shared
// LlmSettingsForm in HideRows mode, so only the LLM fields (llm-contract.md §5) show.
// Same keys and persistence path as SettingsDialog — exec(), then feed result() to
// MainWindow::applySettings on Accepted.
namespace stencil::gui {

  class LlmSettingsForm;

  class AssistantSettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit AssistantSettingsDialog(const Settings& current,
                                     QWidget* parent = nullptr);
    Settings result() const;

   private:
    Settings base_;  // every field this dialog doesn't edit rides through
    LlmSettingsForm* form_ = nullptr;
  };

}
