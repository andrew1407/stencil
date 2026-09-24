// The LLM assistant form's rows; call order and the shared row helpers live in LlmSettingsForm.cpp.
#include "../../support/control/dblReset.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/menu/SearchCombo.hpp"
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
    if (mode == RowMode::HIDE_ROWS)
      form->addRow(modalSectionLabel(QStringLiteral("Provider"), this));

    // Labels come from the providers.json canon; "none" is the local-only assistant-off value (contract §5 note).
    provider = new SearchComboBox(this, /*searchable=*/false);
    provider->setObjectName("llmProvider");
    provider->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    provider->addItem("None (turned off)", "none");
    support::setResetDefault(provider, QStringLiteral("none"));   // contract §5: ships off
    for (const char* id : {"ollama", "openai-compat", "stencil-server"})
      provider->addItem(stencil::llm::llmProviderDisplayName(id), id);
    {
      // Unknown values fall back to "None" (the contract default — llmSettings.hpp).
      const int idx = provider->findData(current.llmProvider);
      provider->setCurrentIndex(idx >= 0 ? idx : provider->findData("none"));
    }
    provider->setToolTip(
        "Where the chat assistant runs: a local Ollama, any OpenAI-compatible "
        "server, or a Stencil collaboration server");
    form->addRow("Provider", provider);
    hugRight(provider);
    rowDivider();

    baseUrl = new QLineEdit(current.llmBaseUrl, this);
    baseUrl->setObjectName("llmBaseUrl");
    baseUrl->setPlaceholderText(stencil::llm::defaultLlmBaseUrl("ollama"));
    baseUrl->setToolTip(
        QStringLiteral("Provider base URL (Ollama default %1; "
                       "OpenAI-compatible default %2)")
            .arg(stencil::llm::defaultLlmBaseUrl("ollama"),
                 stencil::llm::defaultLlmBaseUrl("openai-compat")));
    form->addRow("Base URL", baseUrl);
    hugRight(baseUrl);
    baseUrlDiv = rowDivider();

    // Suggestions arrive asynchronously (refreshModels); free-typed text always wins (NoInsert).
    model = new SearchComboBox(this, /*searchable=*/false);
    model->setObjectName("llmModel");
    model->setEditable(true);
    model->setInsertPolicy(QComboBox::NoInsert);
    model->lineEdit()->setPlaceholderText("(provider default)");
    model->setToolTip(
        "Model name; leave empty to use whatever the server serves — pick a "
        "suggestion or type any name");
    model->setCurrentIndex(-1);
    model->setEditText(current.llmModel);
    form->addRow("Model", model);
    hugRight(model);
    modelDiv = rowDivider();

    apiKey = new QLineEdit(current.llmApiKey, this);
    apiKey->setObjectName("llmApiKey");
    apiKey->setEchoMode(QLineEdit::Password);
    apiKey->setPlaceholderText("(optional — most local servers need none)");
    apiKey->setToolTip(
        "Optional API key, sent as a Bearer token (OpenAI-compatible providers only).\n"
        "Local servers (LM Studio, llama.cpp) need none; hosted OpenAI-compatible\n"
        "services issue keys in their dashboards.");
    form->addRow("API key", apiKey);
    hugRight(apiKey);
    apiKeyDiv = rowDivider();

    server = new SearchComboBox(this, /*searchable=*/false);
    server->setObjectName("llmServer");
    support::setResetDefault(server, 0);   // the first saved server, as the browser picks
    server->setToolTip(
        "Which configured Stencil collaboration server proxies the LLM "
        "(uses your existing connection token)");
    {
      const auto saved = stencil::net::connectionStore::loadSavedServers();
      for (const auto& s : saved) server->addItem(s.url, s.url);
      if (server->count() == 0)
        server->addItem("(no saved servers — connect first)", QString());
      const int idx = server->findData(current.llmServerUrl);
      server->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    server->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    form->addRow("Server", server);
    hugRight(server);
    serverDiv = rowDivider();

    // Browser #chat-server-status-row: no "Status" label, just the dot and the muted text.
    auto* statusRow = new QWidget(this);
    statusRow->setObjectName("llmStatusRow");
    auto* statusLay = new QHBoxLayout(statusRow);
    statusLay->setContentsMargins(0, 0, 0, 0);
    statusLay->setSpacing(8);
    statusDot = new QLabel(statusRow);
    statusDot->setObjectName("llmStatusDot");
    statusDot->setTextFormat(Qt::RichText);
    statusLay->addWidget(statusDot);
    status = new QLabel(statusRow);
    status->setObjectName("llmStatus");
    status->setWordWrap(true);
    status->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // No trailing stretch: a word-wrapped label handed only its sizeHint width breaks into three lines.
    statusLay->addWidget(status, 1);
    form->addRow(statusRow);
    rowDivider();
  }

  void LlmSettingsForm::buildChatHistoryRows(const Settings& current) {

    if (mode == RowMode::HIDE_ROWS)
      form->addRow(modalSectionLabel(QStringLiteral("Chat history"), this));

    // llm-contract.md §12: provider-independent, ships OFF. Browser .vs-inline-check.
    saveChats = new QCheckBox(tr("Save chats with projects"), this);
    saveChats->setObjectName("llmSaveChats");
    saveChats->setChecked(current.saveChatsWithProject);
    support::setResetDefault(saveChats, false);   // §12: ships off
    saveChats->setToolTip(
        "Save the assistant conversation with the active project and restore it "
        "when the project is reopened. Text only, most recent 32 turns; incognito "
        "never saves.\n\nFor a project on a server the transcript is stored with it, "
        "so everyone that project is shared with can read it. Local projects stay "
        "on this machine.");
    form->addRow(saveChats);
    rowDivider();
    // §12.2 requires the sharing consequence to be visible, not only on hover.
    auto* saveChatsHint = new QLabel(
        "Chats saved with a server project are <b>readable by everyone the "
        "project is shared with</b>; local projects stay on this machine.",
        this);
    saveChatsHint->setObjectName("llmSaveChatsHint");
    saveChatsHint->setWordWrap(true);
    this->saveChatsHint = saveChatsHint;   // resizeEvent pins its height to its real width
    // heightForWidth, or the layout budgets the label's height from a narrower width than it renders at.
    {
      QSizePolicy sp = saveChatsHint->sizePolicy();
      sp.setHeightForWidth(true);
      saveChatsHint->setSizePolicy(sp);
    }

    if (mode == RowMode::HIDE_ROWS) {
      // ONE tinted help note (browser .chat-cors-note, merged per user decision). The desktop calls the
      // endpoint over Qt Network, so CORS is not its problem.
      noteBox = new QFrame(this);
      noteBox->setObjectName("llmNoteBox");
      QSizePolicy boxSp(QSizePolicy::Preferred, QSizePolicy::Fixed);
      boxSp.setHeightForWidth(true);
      noteBox->setSizePolicy(boxSp);
      auto* noteLay = new QVBoxLayout(noteBox);
      noteLay->setContentsMargins(10, 6, 10, 6);
      noteLay->setSpacing(4);
      noteLay->addWidget(saveChatsHint);
      note = new QLabel(
          QStringLiteral("Local providers must be running at the URL above "
                         "(Ollama on port %1, LM Studio on %2).")
              .arg(QUrl(stencil::llm::defaultLlmBaseUrl("ollama")).port())
              .arg(QUrl(stencil::llm::defaultLlmBaseUrl("openai-compat")).port()),
          noteBox);
      note->setObjectName("llmNote");
      note->setWordWrap(true);
      {
        QSizePolicy sp = note->sizePolicy();
        sp.setHeightForWidth(true);
        note->setSizePolicy(sp);
      }
      noteLay->addWidget(note);
      // A FORM row, so its top rides the SAME explicit 9px verticalSpacing every other row shares; on
      // the outer QVBoxLayout it inherits the QStyle's own metric, a real ~70px gap (user report).
      form->addRow(noteBox);
    } else {
      saveChatsHint->setStyleSheet("color: palette(mid);");
      form->addRow(saveChatsHint);
    }
  }

}  // namespace stencil::gui
