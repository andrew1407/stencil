#pragma once
#include "fileStore.hpp"
#include <QWidget>
#include <memory>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QFrame;
class QLabel;
class QLineEdit;
class QTimer;

namespace stencil::llm {
  class LlmClient;
  class QtLlmTransport;
}

// The LLM assistant rows (llm-contract.md §5) shared by the full
// SettingsDialog and the assistant-only AssistantSettingsDialog: provider
// (incl. "None (turned off)"), base URL, model, API key, saved-server
// combo, and a live Status row (dot + text fed by the same LlmClient::probe
// behind the chat dock's gear dot, re-probed as the fields change). The base
// URL pre-fills with the provider default and re-fills on a provider switch
// unless the user edited it; the model combo is editable with async
// provider-supplied suggestions (free-typed text always wins). The two hosts
// differ only in how irrelevant per-provider rows are treated: HideRows
// removes them (browser assistant-modal parity, incl. the local-provider
// note); DisableRows greys them out so the full sheet's row grid stays stable.
namespace stencil::gui {

  class LlmSettingsForm : public QWidget {
    Q_OBJECT
   public:
    enum class RowMode { DisableRows, HideRows };
    explicit LlmSettingsForm(const Settings& current, RowMode mode,
                             QWidget* parent = nullptr);
    // Out-of-line so the unique_ptr member's forward-declared type is complete
    // at destruction.
    ~LlmSettingsForm() override;
    // Write ONLY the assistant keys (llm* + saveChatsWithProject) into s;
    // everything else rides through.
    void applyTo(Settings& s) const;
    void focusProvider();

   private:
    // Apply the per-provider row treatment and, on a provider switch, re-fill
    // the base URL with the new provider's default — unless the user edited it
    // (i.e. it no longer equals the PREVIOUS provider's default).
    void syncRows(const QString& prevProvider);
    // Best-effort async model suggestions for the editable combo; failures are
    // an empty list and the current edit text always survives.
    void refreshModels();
    // Probe the currently EDITED (unsaved) settings and paint the Status row:
    // amber while checking, green "Connected — <detail>" or red with the error.
    // Stale results are dropped via a generation counter, so rapid edits never
    // paint an older probe over a newer one.
    void refreshStatus();
    // Paint the status row: state colour on the dot, plain muted text beside it.
    void setStatus(const char* color, const QString& text);

    RowMode mode_;
    QFormLayout* form_ = nullptr;
    QComboBox* provider_ = nullptr;
    QLineEdit* baseUrl_ = nullptr;
    QComboBox* model_ = nullptr;  // editable: suggestions + free typing
    QLineEdit* apiKey_ = nullptr;    // openai-compat only
    QComboBox* server_ = nullptr;    // saved connections (stencil-server)
    QCheckBox* saveChats_ = nullptr; // §12 chat-persistence opt-in (default off)
    QLabel* note_ = nullptr;    // HideRows only: the local-provider hint
    // HideRows only: the conditional rows' .vs-row hairlines, hidden with them.
    QFrame* baseUrlDiv_ = nullptr;
    QFrame* modelDiv_ = nullptr;
    QFrame* apiKeyDiv_ = nullptr;
    QFrame* serverDiv_ = nullptr;
    QFrame* noteBox_ = nullptr;  // HideRows only: its accent-tinted box (browser .chat-cors-note)
    QLabel* statusDot_ = nullptr;    // live reachability row: the coloured dot…
    QLabel* status_ = nullptr;       // …and its muted text (browser status row)
    QTimer* probeDebounce_ = nullptr;  // settles typing before re-probing
    int probeGen_ = 0;  // drops stale async probe results
    // The transport is a child of the form, so pending model fetches are
    // severed when the host dialog closes.
    stencil::llm::QtLlmTransport* transport_ = nullptr;
    std::unique_ptr<stencil::llm::LlmClient> client_;
  };

}
