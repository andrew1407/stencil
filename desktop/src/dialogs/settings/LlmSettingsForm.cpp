#include "../../support/modal/modalChrome.hpp"
#include "LlmSettingsForm.hpp"
#include "LlmClient.hpp"
#include "llmSettings.hpp"
#include "QtLlmTransport.hpp"
#include "SessionKey.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // Status-row state colours - the browser .chat-status-tip states (ok | error | connecting),
    // same values as the dock gear tooltip's (MainWindow.cpp TIP*_COLOR).
    constexpr const char* STATUS_OK_COLOR = "#28a745";
    constexpr const char* STATUS_ERROR_COLOR = "#dc3545";
    constexpr const char* STATUS_CONNECTING_COLOR = "#e0a800";
    // Wider than SettingsDialog's CTRL_W 180, where a base URL, a model name, a key or a
    // server URL clips (browser modalShell.css stencil-llm-settings-modal --vs-ctrl-w twin).
    constexpr int CTRL_W = 320;
  }

  LlmSettingsForm::LlmSettingsForm(const Settings& current, RowMode mode,
                                   QWidget* parent)
      : QWidget(parent), mode(mode) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    form = new QFormLayout;
    // Visuals & Settings parity: labels flush left, every field one fixed width flush
    // right — a plain QFormLayout stretch let Model/Base URL outrun Provider/Server.
    alignModalForm(form, /*growFields=*/false);
    // A browser .vs-row is 7px padding + control + 7px + its hairline (~49px pitch, measured live);
    // the default form spacing packs the same rows into ~37px.
    if (this->mode == RowMode::HIDE_ROWS) form->setVerticalSpacing(9);
    col->addLayout(form);

    buildProviderRows(current);
    buildChatHistoryRows(current);
    wireProviderFields();
  }

  // Browser assistant-modal parity (HideRows hosts only): every row wears the .vs-row hairline
  // under it. A conditional row hands its divider back, so syncRows hides the pair together.
  QFrame* LlmSettingsForm::rowDivider() {
    if (mode != RowMode::HIDE_ROWS) return nullptr;
    auto* d = modalDivider(this);
    form->addRow(d);
    return d;
  }

  // ONE control column, flush right (components.css --vs-ctrl-w): every field the same
  // width whatever its type, so Provider/Model/Server read as a table, not ragged.
  void LlmSettingsForm::hugRight(QWidget* w) {
    w->setFixedWidth(CTRL_W);
    int row = -1;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(w, &row, &role);
    if (row >= 0)
      if (QLayoutItem* it = form->itemAt(row, QFormLayout::FieldRole))
        it->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  }

  // The anthropic session key (llm-providers.md §5): held in memory when the host saves, so the
  // field starts empty on every opening, and the settings JSON never sees it.
  void LlmSettingsForm::buildAnthropicKeyRows() {
    const int hours = stencil::llm::sessionKeyTtlMinutes() / 60;
    anthropicKey = new QLineEdit(this);
    anthropicKey->setObjectName("llmAnthropicKey");
    anthropicKey->setEchoMode(QLineEdit::Password);
    anthropicKey->setPlaceholderText(QString::fromUtf8("sk-ant-… (kept in memory only)"));
    anthropicKey->setToolTip(
        QStringLiteral("Your own Anthropic API key, sent as 'x-api-key' straight to Anthropic.\n"
                       "Kept in memory until Stencil quits or %1 hours pass; never saved.").arg(hours));
    form->addRow("API key", anthropicKey);
    hugRight(anthropicKey);
    anthropicKeyDiv = rowDivider();

    keyStatusRow = new QWidget(this);
    keyStatusRow->setObjectName("llmKeyStatusRow");
    auto* lay = new QHBoxLayout(keyStatusRow);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);
    keyStatus = new QLabel(keyStatusRow);
    keyStatus->setObjectName("llmKeyStatus");
    keyStatus->setWordWrap(true);
    lay->addWidget(keyStatus, 1);
    forgetKey = new QPushButton(tr("Forget key"), keyStatusRow);
    forgetKey->setObjectName("llmForgetKey");
    makeModalCta(forgetKey, "trash");
    forgetKey->setAutoDefault(false);
    forgetKey->setToolTip("Drop the key now; the next turn asks for it again");
    lay->addWidget(forgetKey);
    form->addRow(keyStatusRow);
    keyStatusDiv = rowDivider();
    connect(forgetKey, &QPushButton::clicked, this, [this] {
      anthropicKey->clear();
      stencil::llm::SessionKey::instance().forget();
      refreshModels();
      refreshStatus();
    });
    connect(&stencil::llm::SessionKey::instance(), &stencil::llm::SessionKey::changed, this,
            &LlmSettingsForm::renderKeyStatus);
    renderKeyStatus();
  }

  // The transport seam model suggestions go through, the debounce that re-probes once
  // typing settles, and every field's change wiring.
  void LlmSettingsForm::wireProviderFields() {

    // Best-effort model suggestions through the shared transport seam: async,
    // never blocks the dialog, failures = empty list.
    transport = new stencil::llm::QtLlmTransport(this);
    client = std::make_unique<stencil::llm::LlmClient>(transport);

    // Re-probe the status row once typing settles (matching the browser
    // modal's on-change probe without one request per keystroke).
    probeDebounce = new QTimer(this);
    probeDebounce->setSingleShot(true);
    probeDebounce->setInterval(600);
    connect(probeDebounce, &QTimer::timeout, this,
            &LlmSettingsForm::refreshStatus);

    syncRows(provider->currentData().toString());
    refreshModels();
    refreshStatus();
    connect(baseUrl, &QLineEdit::textEdited, probeDebounce,
            qOverload<>(&QTimer::start));
    connect(apiKey, &QLineEdit::textEdited, probeDebounce,
            qOverload<>(&QTimer::start));
    connect(baseUrl, &QLineEdit::editingFinished, this, [this] {
      refreshModels();
      refreshStatus();
    });
    connect(apiKey, &QLineEdit::editingFinished, this, [this] {
      refreshModels();
      refreshStatus();
    });
    connect(anthropicKey, &QLineEdit::textEdited, probeDebounce, qOverload<>(&QTimer::start));
    connect(anthropicKey, &QLineEdit::editingFinished, this, [this] {
      refreshModels();
      refreshStatus();
    });
    connect(server, &QComboBox::currentIndexChanged, this, [this] {
      refreshModels();
      refreshStatus();
    });
    connect(provider, &QComboBox::currentIndexChanged, this,
            [this, prev = provider->currentData().toString()]() mutable {
              const QString before = prev;
              prev = provider->currentData().toString();
              syncRows(before);
              refreshModels();
              refreshStatus();
            });
  }

  void LlmSettingsForm::refreshStatus() {
    const QString provider = this->provider->currentData().toString();
    // "none" is local-only (contract §5): nothing is probed or sent anywhere.
    // Text matches the browser's modal.js renderStatus() verbatim.
    if (provider == QLatin1String("none")) {
      ++probeGen;  // invalidate any probe still in flight
      setStatus(STATUS_ERROR_COLOR,
                QStringLiteral("Assistant turned off — nothing is sent anywhere."));
      return;
    }
    const QString serverUrl = server->currentData().toString();
    if (provider == QLatin1String("stencil-server") && serverUrl.isEmpty()) {
      ++probeGen;
      setStatus(STATUS_ERROR_COLOR,
                QStringLiteral("No collaboration server configured — assistant turned off."));
      return;
    }
    stencil::llm::LlmSettings cfg;
    cfg.provider = provider;
    cfg.baseUrl = baseUrl->text().trimmed();
    cfg.apiKey = provider == QLatin1String("anthropic") ? requestKey() : apiKey->text().trimmed();
    cfg.serverUrl = serverUrl;
    setStatus(STATUS_CONNECTING_COLOR, QStringLiteral("Checking the configured LLM…"));
    const int gen = ++probeGen;
    QPointer<LlmSettingsForm> self(this);
    client->probe(cfg, [self, gen, provider, serverUrl](stencil::llm::LlmProbeResult r) {
      if (!self || gen != self->probeGen) return;  // superseded by a newer edit
      if (r.ok) {
        // stencil-server: the browser's "Connected — via {url} ({model})" verbatim.
        QString text = provider == QLatin1String("stencil-server")
            ? QStringLiteral("Connected — via %1 (%2)")
                  .arg(serverUrl, r.model.isEmpty() ? QStringLiteral("server default") : r.model)
            : r.detail.isEmpty() ? QStringLiteral("Connected")
                                  : QStringLiteral("Connected — %1").arg(r.detail);
        self->setStatus(STATUS_OK_COLOR, text);
      } else {
        self->setStatus(STATUS_ERROR_COLOR,
                        r.detail.isEmpty() ? QStringLiteral("Unreachable") : r.detail);
      }
    });
  }
}

