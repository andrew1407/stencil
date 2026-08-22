#pragma once
#include "fileStore.hpp"
#include <QDialog>

// Assistant-only settings editor — the desktop counterpart of the browser's
// dedicated "Assistant" modal (browser/js/ui/llmSettingsModal.js), opened by the
// chat dock's gear. It exposes ONLY the LLM fields (llm-contract.md §5) —
// the shared LlmSettingsForm in HideRows mode — so the user isn't hunting for
// them at the bottom of the full Settings dialog, which deliberately keeps its
// own AI-assistant group for anyone arriving that way.
//
// Same keys, same persistence path: construct with the current Settings, exec(),
// and on QDialog::Accepted feed result() to MainWindow::applySettings — exactly
// like SettingsDialog. Only llmProvider/llmBaseUrl/llmModel/llmApiKey/
// llmServerUrl differ from the Settings handed in.
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
