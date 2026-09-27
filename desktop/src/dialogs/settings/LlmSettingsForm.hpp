#pragma once
#include "fileStore.hpp"
#include <QWidget>
#include <memory>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QFrame;
class QLabel;
class QDateTime;
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace stencil::llm {
  class LlmClient;
  class QtLlmTransport;
}

// The LLM assistant rows (llm-contract.md §5) shared by SettingsDialog and AssistantSettingsDialog.
// HideRows removes the irrelevant rows (browser assistant-modal parity), DisableRows greys them.
namespace stencil::gui {

  class LlmSettingsForm : public QWidget {
    Q_OBJECT
   public:
    enum class RowMode { DISABLE_ROWS, HIDE_ROWS };
    explicit LlmSettingsForm(const Settings& current, RowMode mode,
                             QWidget* parent = nullptr);
    // Out-of-line so the unique_ptr member's forward-declared type is complete at destruction.
    ~LlmSettingsForm() override;
    // Writes ONLY the assistant keys (llm* + saveChatsWithProject); everything else rides through.
    // The anthropic key is never among them.
    void applyTo(Settings& s) const;
    // The host's Save: a typed anthropic key is held in memory for the session; an empty field
    // keeps the one already held.
    void commitAnthropicKey();
    // The key field when anthropic holds no key, else the provider.
    void focusEntry();
    void pinNoteHeights();   // each wrapped note line to the height its real width needs
    // "Key kept until 21:40 or until Stencil quits.", the weekday added when `until` is another
    // day; an invalid `until` is "No key for this session."
    static QString keyStatusText(const QDateTime& until, const QDateTime& now);

   protected:
    // A wrapped label's sizeHint is measured at a GUESSED width, and the layout budgets that
    // instead of re-asking heightForWidth — pin each note line to the height its real width needs.
    void resizeEvent(QResizeEvent* event) override;

   private:
    // Construction order is observable; a conditional row hands its divider back so syncRows can hide the pair.
    QFrame* rowDivider();
    void hugRight(QWidget* w);
    void buildProviderRows(const Settings& current);
    // The anthropic key field, its status + Forget row; never filled back from memory.
    void buildAnthropicKeyRows();
    void renderKeyStatus();
    // What a probe or a model list sends for anthropic: the typed key, else the held one.
    QString requestKey() const;
    void buildChatHistoryRows(const Settings& current);
    void wireProviderFields();
    // On a provider switch the base URL re-fills unless the user edited it (it no longer equals the
    // PREVIOUS provider's default).
    void syncRows(const QString& prevProvider);
    void refreshModels();
    // Probes the EDITED (unsaved) settings; stale results are dropped via a generation counter.
    void refreshStatus();
    void setStatus(const char* color, const QString& text);

    RowMode mode;
    QFormLayout* form = nullptr;
    QComboBox* provider = nullptr;
    QLineEdit* baseUrl = nullptr;
    QComboBox* model = nullptr;
    QLineEdit* apiKey = nullptr;
    QComboBox* server = nullptr;
    QLineEdit* anthropicKey = nullptr;
    QWidget* keyStatusRow = nullptr;
    QLabel* keyStatus = nullptr;
    QPushButton* forgetKey = nullptr;
    QCheckBox* saveChats = nullptr;
    QLabel* note = nullptr;
    QLabel* keyNote = nullptr;
    QLabel* saveChatsHint = nullptr;
    QFrame* baseUrlDiv = nullptr;
    QFrame* modelDiv = nullptr;
    QFrame* apiKeyDiv = nullptr;
    QFrame* anthropicKeyDiv = nullptr;
    QFrame* keyStatusDiv = nullptr;
    QFrame* serverDiv = nullptr;
    QFrame* noteBox = nullptr;
    QLabel* statusDot = nullptr;
    QLabel* status = nullptr;
    QTimer* probeDebounce = nullptr;
    int probeGen = 0;
    // A child of the form, so pending model fetches are severed when the host dialog closes.
    stencil::llm::QtLlmTransport* transport = nullptr;
    std::unique_ptr<stencil::llm::LlmClient> client;
  };

}
