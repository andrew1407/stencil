// The LLM assistant form's rows; call order and the shared row helpers live in LlmSettingsForm.cpp.
#include "../support/modalChrome.hpp"
#include "../support/SearchCombo.hpp"
#include "LlmSettingsForm.hpp"
#include "connectionStore.hpp"
#include "LlmClient.hpp"
#include "llmSettings.hpp"
#include "QtLlmTransport.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace stencil::gui {

  void LlmSettingsForm::buildProviderRows(const Settings& current) {
    if (mode_ == RowMode::HIDE_ROWS)
      form_->addRow(modalSectionLabel(QStringLiteral("Provider"), this));

    // Labels come from the providers.json canon; "none" is the local-only assistant-off value (contract §5 note).
    provider_ = new SearchComboBox(this, /*searchable=*/false);
    provider_->setObjectName("llmProvider");
    provider_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    provider_->addItem("None (turned off)", "none");
    for (const char* id : {"ollama", "openai-compat", "stencil-server"})
      provider_->addItem(stencil::llm::llmProviderDisplayName(id), id);
    {
      // Unknown values fall back to "None" (the contract default — llmSettings.hpp).
      const int idx = provider_->findData(current.llmProvider);
      provider_->setCurrentIndex(idx >= 0 ? idx : provider_->findData("none"));
    }
    provider_->setToolTip(
        "Where the chat assistant runs: a local Ollama, any OpenAI-compatible "
        "server, or a Stencil collaboration server");
    form_->addRow("Provider", provider_);
    hugRight(provider_);
    rowDivider();

    baseUrl_ = new QLineEdit(current.llmBaseUrl, this);
    baseUrl_->setObjectName("llmBaseUrl");
    baseUrl_->setPlaceholderText(stencil::llm::defaultLlmBaseUrl("ollama"));
    baseUrl_->setToolTip(
        QStringLiteral("Provider base URL (Ollama default %1; "
                       "OpenAI-compatible default %2)")
            .arg(stencil::llm::defaultLlmBaseUrl("ollama"),
                 stencil::llm::defaultLlmBaseUrl("openai-compat")));
    form_->addRow("Base URL", baseUrl_);
    hugRight(baseUrl_);
    baseUrlDiv_ = rowDivider();

    // Suggestions arrive asynchronously (refreshModels); free-typed text always wins (NoInsert).
    model_ = new SearchComboBox(this, /*searchable=*/false);
    model_->setObjectName("llmModel");
    model_->setEditable(true);
    model_->setInsertPolicy(QComboBox::NoInsert);
    model_->lineEdit()->setPlaceholderText("(provider default)");
    model_->setToolTip(
        "Model name; leave empty to use whatever the server serves — pick a "
        "suggestion or type any name");
    model_->setCurrentIndex(-1);
    model_->setEditText(current.llmModel);
    form_->addRow("Model", model_);
    hugRight(model_);
    modelDiv_ = rowDivider();

    apiKey_ = new QLineEdit(current.llmApiKey, this);
    apiKey_->setObjectName("llmApiKey");
    apiKey_->setEchoMode(QLineEdit::Password);
    apiKey_->setPlaceholderText("(optional — most local servers need none)");
    apiKey_->setToolTip(
        "Optional API key, sent as a Bearer token (OpenAI-compatible providers only).\n"
        "Local servers (LM Studio, llama.cpp) need none; hosted OpenAI-compatible\n"
        "services issue keys in their dashboards.");
    form_->addRow("API key", apiKey_);
    hugRight(apiKey_);
    apiKeyDiv_ = rowDivider();

    server_ = new SearchComboBox(this, /*searchable=*/false);
    server_->setObjectName("llmServer");
    server_->setToolTip(
        "Which configured Stencil collaboration server proxies the LLM "
        "(uses your existing connection token)");
    {
      const auto saved = stencil::net::connectionStore::loadSavedServers();
      for (const auto& s : saved) server_->addItem(s.url, s.url);
      if (server_->count() == 0)
        server_->addItem("(no saved servers — connect first)", QString());
      const int idx = server_->findData(current.llmServerUrl);
      server_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    server_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    form_->addRow("Server", server_);
    hugRight(server_);
    serverDiv_ = rowDivider();

    // Browser #chat-server-status-row: no "Status" label, just the dot and the muted text.
    auto* statusRow = new QWidget(this);
    statusRow->setObjectName("llmStatusRow");
    auto* statusLay = new QHBoxLayout(statusRow);
    statusLay->setContentsMargins(0, 0, 0, 0);
    statusLay->setSpacing(8);
    statusDot_ = new QLabel(statusRow);
    statusDot_->setObjectName("llmStatusDot");
    statusDot_->setTextFormat(Qt::RichText);
    statusLay->addWidget(statusDot_);
    status_ = new QLabel(statusRow);
    status_->setObjectName("llmStatus");
    status_->setWordWrap(true);
    status_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // No trailing stretch: a word-wrapped label handed only its sizeHint width breaks into three lines.
    statusLay->addWidget(status_, 1);
    form_->addRow(statusRow);
    rowDivider();
  }

  void LlmSettingsForm::buildChatHistoryRows(const Settings& current) {

    if (mode_ == RowMode::HIDE_ROWS)
      form_->addRow(modalSectionLabel(QStringLiteral("Chat history"), this));

    // llm-contract.md §12: provider-independent, ships OFF. Browser .vs-inline-check.
    saveChats_ = new QCheckBox(tr("Save chats with projects"), this);
    saveChats_->setObjectName("llmSaveChats");
    saveChats_->setChecked(current.saveChatsWithProject);
    saveChats_->setToolTip(
        "Save the assistant conversation with the active project and restore it "
        "when the project is reopened. Text only, most recent 32 turns; incognito "
        "never saves.\n\nFor a project on a server the transcript is stored with it, "
        "so everyone that project is shared with can read it. Local projects stay "
        "on this machine.");
    form_->addRow(saveChats_);
    rowDivider();
    // §12.2 requires the sharing consequence to be visible, not only on hover.
    auto* saveChatsHint = new QLabel(
        "Chats saved with a server project are <b>readable by everyone the "
        "project is shared with</b>; local projects stay on this machine.",
        this);
    saveChatsHint->setObjectName("llmSaveChatsHint");
    saveChatsHint->setWordWrap(true);
    saveChatsHint_ = saveChatsHint;   // resizeEvent pins its height to its real width
    // heightForWidth, or the layout budgets the label's height from a narrower width than it renders at.
    {
      QSizePolicy sp = saveChatsHint->sizePolicy();
      sp.setHeightForWidth(true);
      saveChatsHint->setSizePolicy(sp);
    }

    if (mode_ == RowMode::HIDE_ROWS) {
      // ONE tinted help note (browser .chat-cors-note, merged per user decision). The desktop calls the
      // endpoint over Qt Network, so CORS is not its problem.
      noteBox_ = new QFrame(this);
      noteBox_->setObjectName("llmNoteBox");
      QSizePolicy boxSp(QSizePolicy::Preferred, QSizePolicy::Fixed);
      boxSp.setHeightForWidth(true);
      noteBox_->setSizePolicy(boxSp);
      auto* noteLay = new QVBoxLayout(noteBox_);
      noteLay->setContentsMargins(10, 6, 10, 6);
      noteLay->setSpacing(4);
      noteLay->addWidget(saveChatsHint);
      note_ = new QLabel(
          QStringLiteral("Local providers must be running at the URL above "
                         "(Ollama on port %1, LM Studio on %2).")
              .arg(QUrl(stencil::llm::defaultLlmBaseUrl("ollama")).port())
              .arg(QUrl(stencil::llm::defaultLlmBaseUrl("openai-compat")).port()),
          noteBox_);
      note_->setObjectName("llmNote");
      note_->setWordWrap(true);
      {
        QSizePolicy sp = note_->sizePolicy();
        sp.setHeightForWidth(true);
        note_->setSizePolicy(sp);
      }
      noteLay->addWidget(note_);
      // A FORM row, so its own top rides the SAME explicit 9px verticalSpacing every other
      // row shares — style-dependent otherwise: a widget on the outer QVBoxLayout instead
      // inherits whatever spacing the active QStyle's own metric happens to default to,
      // which measured a real ~70px gap under this platform's native style, though offscreen
      // (ctest) hides it behind a smaller default (user report; browser has no such gap).
      form_->addRow(noteBox_);
    } else {
      saveChatsHint->setStyleSheet("color: palette(mid);");
      form_->addRow(saveChatsHint);
    }
  }

}  // namespace stencil::gui
