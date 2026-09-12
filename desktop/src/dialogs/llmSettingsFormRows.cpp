// The LLM assistant form's rows, as its constructor builds them: the provider picker with its
// endpoint/key/model fields and the live reachability line, then the §12 chat-persistence
// opt-in. Call order and the shared row helpers live in llmSettingsForm.cpp.
#include "../support/modalChrome.hpp"
#include "../support/searchCombo.hpp"
#include "llmSettingsForm.hpp"
#include "connectionStore.hpp"
#include "llmClient.hpp"
#include "llmSettings.hpp"
#include "qtLlmTransport.hpp"
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

  // The provider rows: the provider picker, its endpoint/key/model fields, the server
  // choice, and the live reachability row the dock gear's dot mirrors.
  void LlmSettingsForm::buildProviderRows(const Settings& current) {
    if (mode_ == RowMode::HideRows)
      form_->addRow(modalSectionLabel(QStringLiteral("Provider"), this));

    // Provider labels come from the providers.json canon (browser/extension
    // parity). "none" is the local-only assistant-off value (contract §5 note)
    // — never sent anywhere, so it has no canon row.
    provider_ = new SearchComboBox(this, /*searchable=*/false);
    provider_->setObjectName("llmProvider");
    // Wide enough for every provider label (the default field width truncates
    // the longer entries).
    provider_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    provider_->addItem("None (turned off)", "none");
    for (const char* id : {"ollama", "openai-compat", "stencil-server"})
      provider_->addItem(stencil::llm::llmProviderDisplayName(id), id);
    {
      // Unknown/corrupt values fall back to ollama (the contract default), not
      // to the first "None" entry.
      const int idx = provider_->findData(current.llmProvider);
      provider_->setCurrentIndex(idx >= 0 ? idx : provider_->findData("ollama"));
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
    baseUrlDiv_ = rowDivider();

    // Editable combo: provider-supplied suggestions arrive asynchronously
    // (refreshModels); free-typed text always wins (NoInsert).
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

    // Live reachability of the edited settings — the dialog-local rendering of the dock
    // gear's status dot, so misconfiguration shows before Save. Browser
    // #chat-server-status-row: no "Status" label, just the dot and the muted text.
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
    // Dot then text, side by side — the reading is one thing ("● failed to fetch"), and
    // pinning the text to the right edge left a row of nothing between them.
    status_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // It takes the rest of the row (no trailing stretch): a word-wrapped label handed only
    // its sizeHint width breaks an easily-fitting sentence into three lines. Left-aligned,
    // so the width goes to the right of the text, not between it and the dot.
    statusLay->addWidget(status_, 1);
    form_->addRow(statusRow);
    rowDivider();
  }

  // The §12 chat-persistence opt-in and, in the host that shows it, the note box.
  void LlmSettingsForm::buildChatHistoryRows(const Settings& current, QVBoxLayout* col) {

    if (mode_ == RowMode::HideRows)
      form_->addRow(modalSectionLabel(QStringLiteral("Chat history"), this));

    // Chat persistence opt-in (llm-contract.md §12): provider-independent, so it gets no
    // per-provider row treatment in syncRows. Ships OFF. Box first with its label beside
    // it, so it reads as one control — the browser's .vs-inline-check does the same.
    saveChats_ = new QCheckBox(tr("Save chats with projects"), this);
    saveChats_->setObjectName("llmSaveChats");
    saveChats_->setChecked(current.saveChatsWithProject);
    saveChats_->setToolTip(
        "Save the assistant conversation with the active project and restore it "
        "when the project is reopened. Text only, most recent 32 turns; incognito "
        "never saves.\n\nFor a project on a server the transcript is stored with it, "
        "so everyone that project is shared with can read it. Local projects stay "
        "on this machine.");
    form_->addRow(saveChats_);   // one control spanning the row, not a label/field pair
    rowDivider();
    // §12.2 requires the sharing consequence to be visible, not only on hover.
    auto* saveChatsHint = new QLabel(
        "Chats saved with a server project are <b>readable by everyone the "
        "project is shared with</b>; local projects stay on this machine.",
        this);
    saveChatsHint->setObjectName("llmSaveChatsHint");
    saveChatsHint->setWordWrap(true);
    // heightForWidth, or the layout budgets the label's height from a narrower width than
    // it renders at and reserves room for lines the text never uses — which reads as a slab
    // of padding, not as a margin. Qt consults it only when the policy says to.
    {
      QSizePolicy sp = saveChatsHint->sizePolicy();
      sp.setHeightForWidth(true);
      saveChatsHint->setSizePolicy(sp);
    }

    if (mode_ == RowMode::HideRows) {
      // ONE tinted help note (browser .chat-cors-note; the two boxes merged per
      // user decision): the §12.2 sharing line, and — for direct providers only —
      // the local-endpoint line. The desktop calls the endpoint over Qt Network
      // so CORS is not its problem; being up at the URL is what matters.
      noteBox_ = new QFrame(this);
      noteBox_->setObjectName("llmNoteBox");
      // Tight to its text: it is a note, not a panel, and Fixed height stops the column
      // stretching it into one (too much air above and below the line).
      QSizePolicy boxSp(QSizePolicy::Preferred, QSizePolicy::Fixed);
      boxSp.setHeightForWidth(true);   // …and the frame sizes to the wrapped label, not past it
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
        sp.setHeightForWidth(true);   // same reason as saveChatsHint above
        note_->setSizePolicy(sp);
      }
      noteLay->addWidget(note_);
      col->addWidget(noteBox_);
    } else {
      saveChatsHint->setStyleSheet("color: palette(mid);");
      form_->addRow(saveChatsHint);
    }
  }

}  // namespace stencil::gui
