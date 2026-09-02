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

  namespace {
    // Status-row state colours — the browser .chat-status-tip states
    // (ok | error | connecting), same values as the dock gear tooltip's
    // (mainWindow.cpp kTip*Color).
    constexpr const char* kStatusOkColor = "#28a745";
    constexpr const char* kStatusErrorColor = "#dc3545";
    constexpr const char* kStatusConnectingColor = "#e0a800";

  }

  LlmSettingsForm::LlmSettingsForm(const Settings& current, RowMode mode,
                                   QWidget* parent)
      : QWidget(parent), mode_(mode) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    form_ = new QFormLayout;
    // Browser .vs-row/.vs-field geometry: labels flush left and the fields
    // spanning the rest of the row (the modal's inputs run the full width).
    alignModalForm(form_, /*growFields=*/mode_ == RowMode::HideRows);
    // The browser rows breathe: a .vs-row is 7px padding + control + 7px + its
    // hairline (~49px pitch, measured live). The default form spacing packs the
    // same rows into ~37px, leaving the dialog visibly shorter than the modal.
    if (mode_ == RowMode::HideRows) form_->setVerticalSpacing(9);
    col->addLayout(form_);

    // Browser assistant-modal parity (HideRows hosts only): the fields sit
    // under the same section headers the modal draws (.vs-section), and every
    // row wears the .vs-row hairline under it. Conditional rows hand back their
    // divider so syncRows hides the pair together.
    const auto rowDivider = [this]() -> QFrame* {
      if (mode_ != RowMode::HideRows) return nullptr;
      auto* d = modalDivider(this);
      form_->addRow(d);
      return d;
    };
    // Browser parity: the modal's SELECTS hug their content on the row's right
    // edge (the .accent-dd trigger — measured live: content-sized, right-aligned)
    // while text fields span. Maximum policy keeps them out of the grow set;
    // the item alignment pins them right.
    const auto hugRight = [this](QWidget* w) {
      w->setSizePolicy(QSizePolicy::Maximum, w->sizePolicy().verticalPolicy());
      int row = -1;
      QFormLayout::ItemRole role;
      form_->getWidgetPosition(w, &row, &role);
      if (row >= 0)
        if (QLayoutItem* it = form_->itemAt(row, QFormLayout::FieldRole))
          it->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    };
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

    // Live reachability of the EDITED settings — the dialog-local rendering of
    // the dock gear's status dot, so misconfiguration shows before Save.
    // Browser #chat-server-status-row: no "Status" label — the coloured dot on
    // the left, the muted text on the right of the same row (space-between).
    auto* statusRow = new QWidget(this);
    statusRow->setObjectName("llmStatusRow");
    statusRow->setToolTip(
        "Reachability of the provider configured above, re-checked as you "
        "edit (ollama /api/version, OpenAI-compatible /models, Stencil server "
        "/llm/info)");
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
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusLay->addWidget(status_, 1);
    form_->addRow(statusRow);
    rowDivider();

    if (mode_ == RowMode::HideRows)
      form_->addRow(modalSectionLabel(QStringLiteral("Chat history"), this));

    // Chat persistence opt-in (llm-contract.md §12): provider-independent,
    // so it gets no per-provider row treatment in syncRows. Ships OFF.
    // Browser row shape: label on the left, the checkbox pinned to the row's
    // right edge (.vs-row space-between).
    saveChats_ = new QCheckBox(this);
    saveChats_->setObjectName("llmSaveChats");
    saveChats_->setChecked(current.saveChatsWithProject);
    saveChats_->setToolTip(
        "Save the assistant conversation with the active project and restore it "
        "when the project is reopened. Text only, most recent 32 turns; incognito "
        "never saves.\n\nFor a project on a server the transcript is stored with it, "
        "so everyone that project is shared with can read it. Local projects stay "
        "on this machine.");
    auto* saveWrap = new QWidget(this);
    auto* saveLay = new QHBoxLayout(saveWrap);
    saveLay->setContentsMargins(0, 0, 0, 0);
    saveLay->addStretch(1);
    saveLay->addWidget(saveChats_);
    form_->addRow("Save chats with projects", saveWrap);
    rowDivider();
    // §12.2 requires the sharing consequence to be visible, not only on hover.
    auto* saveChatsHint = new QLabel(
        "Chats saved with a server project are <b>readable by everyone the "
        "project is shared with</b>; local projects stay on this machine.",
        this);
    saveChatsHint->setObjectName("llmSaveChatsHint");
    saveChatsHint->setWordWrap(true);

    if (mode_ == RowMode::HideRows) {
      // ONE tinted help note (browser .chat-cors-note; the two boxes merged per
      // user decision): the §12.2 sharing line, and — for direct providers only —
      // the local-endpoint line. The desktop calls the endpoint over Qt Network
      // so CORS is not its problem; being up at the URL is what matters.
      noteBox_ = new QFrame(this);
      noteBox_->setObjectName("llmNoteBox");
      auto* noteLay = new QVBoxLayout(noteBox_);
      noteLay->setContentsMargins(10, 8, 10, 8);
      noteLay->setSpacing(6);
      noteLay->addWidget(saveChatsHint);
      note_ = new QLabel(
          QStringLiteral("Local providers must be running at the URL above "
                         "(Ollama on port %1, LM Studio on %2).")
              .arg(QUrl(stencil::llm::defaultLlmBaseUrl("ollama")).port())
              .arg(QUrl(stencil::llm::defaultLlmBaseUrl("openai-compat")).port()),
          noteBox_);
      note_->setObjectName("llmNote");
      note_->setWordWrap(true);
      noteLay->addWidget(note_);
      col->addWidget(noteBox_);
    } else {
      saveChatsHint->setStyleSheet("color: palette(mid);");
      form_->addRow(saveChatsHint);
    }

    // Best-effort model suggestions through the shared transport seam: async,
    // never blocks the dialog, failures = empty list.
    transport_ = new stencil::llm::QtLlmTransport(this);
    client_ = std::make_unique<stencil::llm::LlmClient>(transport_);

    // Re-probe the status row once typing settles (matching the browser
    // modal's on-change probe without one request per keystroke).
    probeDebounce_ = new QTimer(this);
    probeDebounce_->setSingleShot(true);
    probeDebounce_->setInterval(600);
    connect(probeDebounce_, &QTimer::timeout, this,
            &LlmSettingsForm::refreshStatus);

    syncRows(provider_->currentData().toString());
    refreshModels();
    refreshStatus();
    connect(baseUrl_, &QLineEdit::textEdited, probeDebounce_,
            qOverload<>(&QTimer::start));
    connect(apiKey_, &QLineEdit::textEdited, probeDebounce_,
            qOverload<>(&QTimer::start));
    connect(baseUrl_, &QLineEdit::editingFinished, this, [this] {
      refreshModels();
      refreshStatus();
    });
    connect(apiKey_, &QLineEdit::editingFinished, this, [this] {
      refreshModels();
      refreshStatus();
    });
    connect(server_, &QComboBox::currentIndexChanged, this, [this] {
      refreshModels();
      refreshStatus();
    });
    connect(provider_, &QComboBox::currentIndexChanged, this,
            [this, prev = provider_->currentData().toString()]() mutable {
              const QString before = prev;
              prev = provider_->currentData().toString();
              syncRows(before);
              refreshModels();
              refreshStatus();
            });
  }

  LlmSettingsForm::~LlmSettingsForm() = default;

  void LlmSettingsForm::focusProvider() { provider_->setFocus(); }

  void LlmSettingsForm::syncRows(const QString& prevProvider) {
    const QString provider = provider_->currentData().toString();
    const bool off = provider == "none";  // assistant off: every row irrelevant
    const bool viaServer = provider == "stencil-server";
    const bool direct = !off && !viaServer;  // called by us over the network
    if (mode_ == RowMode::HideRows) {
      // Browser parity: irrelevant rows disappear rather than sitting greyed out —
      // each together with its .vs-row hairline.
      const auto showRow = [this](QWidget* row, QFrame* divider, bool on) {
        form_->setRowVisible(row, on);
        if (divider) form_->setRowVisible(divider, on);
      };
      showRow(baseUrl_, baseUrlDiv_, direct);
      showRow(model_, modelDiv_, !off);
      showRow(apiKey_, apiKeyDiv_, provider == "openai-compat");
      showRow(server_, serverDiv_, viaServer);
      // The merged note stays (the §12.2 line always applies); only its
      // local-endpoint line is provider-conditional.
      note_->setVisible(direct);
    } else {
      baseUrl_->setEnabled(direct);
      model_->setEnabled(!off);
      apiKey_->setEnabled(provider == "openai-compat");
      server_->setEnabled(viaServer);
    }
    // Re-fill the base URL on a provider switch when it still holds the PREVIOUS
    // provider's default (i.e. the user never customized it).
    const QString text = baseUrl_->text().trimmed();
    if (direct &&
        (text.isEmpty() || text == stencil::llm::defaultLlmBaseUrl(prevProvider)))
      baseUrl_->setText(stencil::llm::defaultLlmBaseUrl(provider));
    // The host dialog shrinks/grows with the visible rows — but only once shown:
    // an adjustSize during construction (before the host installed its layout)
    // freezes a too-small size that paints the rows on top of each other.
    if (mode_ == RowMode::HideRows && window()->isVisible()) window()->adjustSize();
  }

  void LlmSettingsForm::refreshModels() {
    stencil::llm::LlmSettings cfg;
    cfg.provider = provider_->currentData().toString();
    cfg.baseUrl = baseUrl_->text().trimmed();
    cfg.apiKey = apiKey_->text().trimmed();
    cfg.serverUrl = server_->currentData().toString();
    client_->listModels(cfg, [this](QStringList names) {
      // The current edit text is preserved across repopulation, so a
      // free-typed model always survives a suggestion refresh.
      const QString keep = model_->currentText();
      const QSignalBlocker block(model_);
      model_->clear();
      model_->addItems(names);
      model_->setCurrentIndex(-1);
      model_->setEditText(keep);
    });
  }

  void LlmSettingsForm::refreshStatus() {
    const QString provider = provider_->currentData().toString();
    // "none" is local-only (contract §5): nothing is probed or sent anywhere.
    if (provider == QLatin1String("none")) {
      ++probeGen_;  // invalidate any probe still in flight
      setStatus(kStatusErrorColor,
                QStringLiteral("Assistant turned off — nothing is sent anywhere"));
      return;
    }
    stencil::llm::LlmSettings cfg;
    cfg.provider = provider;
    cfg.baseUrl = baseUrl_->text().trimmed();
    cfg.apiKey = apiKey_->text().trimmed();
    cfg.serverUrl = server_->currentData().toString();
    setStatus(kStatusConnectingColor, QStringLiteral("Checking the configured LLM…"));
    const int gen = ++probeGen_;
    QPointer<LlmSettingsForm> self(this);
    client_->probe(cfg, [self, gen](stencil::llm::LlmProbeResult r) {
      if (!self || gen != self->probeGen_) return;  // superseded by a newer edit
      if (r.ok) {
        QString text = r.detail.isEmpty()
                           ? QStringLiteral("Connected")
                           : QStringLiteral("Connected — %1").arg(r.detail);
        // stencil-server /llm/info reports the server-side model — show it, so
        // "server default" stops being a mystery.
        if (!r.model.isEmpty()) text += QStringLiteral(" · %1").arg(r.model);
        self->setStatus(kStatusOkColor, text);
      } else {
        self->setStatus(kStatusErrorColor,
                        r.detail.isEmpty() ? QStringLiteral("Unreachable") : r.detail);
      }
    });
  }

  // Paint the status row: the dot carries the state colour, the text stays the
  // muted .chat-server-status type (browser #chat-server-status-row).
  void LlmSettingsForm::setStatus(const char* color, const QString& text) {
    statusDot_->setText(QStringLiteral("<span style=\"color:%1;\">●</span>")
                            .arg(QLatin1String(color)));
    status_->setText(text);
  }

  void LlmSettingsForm::applyTo(Settings& s) const {
    s.llmProvider = provider_->currentData().toString();
    s.llmBaseUrl = baseUrl_->text().trimmed();
    s.llmModel = model_->currentText().trimmed();
    s.llmApiKey = apiKey_->text().trimmed();
    s.llmServerUrl = server_->currentData().toString();
    s.saveChatsWithProject = saveChats_->isChecked();
  }

}
