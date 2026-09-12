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
    enum class RowMode { DisableRows, HideRows };
    explicit LlmSettingsForm(const Settings& current, RowMode mode,
                             QWidget* parent = nullptr);
    // Out-of-line so the unique_ptr member's forward-declared type is complete at destruction.
    ~LlmSettingsForm() override;
    // Writes ONLY the assistant keys (llm* + saveChatsWithProject); everything else rides through.
    void applyTo(Settings& s) const;
    void focusProvider();

   private:
    // Construction order is observable; a conditional row hands its divider back so syncRows can hide the pair.
    QFrame* rowDivider();
    void hugRight(QWidget* w);
    void buildProviderRows(const Settings& current);
    void buildChatHistoryRows(const Settings& current, QVBoxLayout* col);
    void wireProviderFields();
    // On a provider switch the base URL re-fills unless the user edited it (it no longer equals the
    // PREVIOUS provider's default).
    void syncRows(const QString& prevProvider);
    void refreshModels();
    // Probes the EDITED (unsaved) settings; stale results are dropped via a generation counter.
    void refreshStatus();
    void setStatus(const char* color, const QString& text);

    RowMode mode_;
    QFormLayout* form_ = nullptr;
    QComboBox* provider_ = nullptr;
    QLineEdit* baseUrl_ = nullptr;
    QComboBox* model_ = nullptr;
    QLineEdit* apiKey_ = nullptr;
    QComboBox* server_ = nullptr;
    QCheckBox* saveChats_ = nullptr;
    QLabel* note_ = nullptr;
    QFrame* baseUrlDiv_ = nullptr;
    QFrame* modelDiv_ = nullptr;
    QFrame* apiKeyDiv_ = nullptr;
    QFrame* serverDiv_ = nullptr;
    QFrame* noteBox_ = nullptr;
    QLabel* statusDot_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* probeDebounce_ = nullptr;
    int probeGen_ = 0;
    // A child of the form, so pending model fetches are severed when the host dialog closes.
    stencil::llm::QtLlmTransport* transport_ = nullptr;
    std::unique_ptr<stencil::llm::LlmClient> client_;
  };

}
