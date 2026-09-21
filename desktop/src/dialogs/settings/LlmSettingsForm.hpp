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
    void applyTo(Settings& s) const;
    void focusProvider();

   protected:
    // A wrapped label's sizeHint is measured at a GUESSED width, and the layout budgets that
    // instead of re-asking heightForWidth — pin each note line to the height its real width needs.
    void resizeEvent(QResizeEvent* event) override;

   private:
    void pinNoteHeights();   // each wrapped note line to the height its real width needs
    // Construction order is observable; a conditional row hands its divider back so syncRows can hide the pair.
    QFrame* rowDivider();
    void hugRight(QWidget* w);
    void buildProviderRows(const Settings& current);
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
    QCheckBox* saveChats = nullptr;
    QLabel* note = nullptr;
    QLabel* saveChatsHint = nullptr;
    QFrame* baseUrlDiv = nullptr;
    QFrame* modelDiv = nullptr;
    QFrame* apiKeyDiv = nullptr;
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
