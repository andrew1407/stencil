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

  namespace {
    // Status-row state colours - the browser .chat-status-tip states (ok | error | connecting),
    // same values as the dock gear tooltip's (MainWindow.cpp TIP_*_COLOR).
    constexpr const char* STATUS_OK_COLOR = "#28a745";
    constexpr const char* STATUS_ERROR_COLOR = "#dc3545";
    constexpr const char* STATUS_CONNECTING_COLOR = "#e0a800";
    // Wider than SettingsDialog's CTRL_W 180, where a base URL, a model name, a key or a
    // server URL clips (browser modalShell.css stencil-llm-settings-modal --vs-ctrl-w twin).
    constexpr int CTRL_W = 320;
  }

  LlmSettingsForm::LlmSettingsForm(const Settings& current, RowMode mode,
                                   QWidget* parent)
      : QWidget(parent), mode_(mode) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    form_ = new QFormLayout;
    // Visuals & Settings parity: labels flush left, every field one fixed width flush
    // right — a plain QFormLayout stretch let Model/Base URL outrun Provider/Server.
    alignModalForm(form_, /*growFields=*/false);
    // A browser .vs-row is 7px padding + control + 7px + its hairline (~49px pitch, measured live);
    // the default form spacing packs the same rows into ~37px.
    if (mode_ == RowMode::HIDE_ROWS) form_->setVerticalSpacing(9);
    col->addLayout(form_);

    buildProviderRows(current);
    buildChatHistoryRows(current);
    wireProviderFields();
  }

  // Browser assistant-modal parity (HideRows hosts only): every row wears the .vs-row hairline
  // under it. A conditional row hands its divider back, so syncRows hides the pair together.
  QFrame* LlmSettingsForm::rowDivider() {
    if (mode_ != RowMode::HIDE_ROWS) return nullptr;
    auto* d = modalDivider(this);
    form_->addRow(d);
    return d;
  }

  // ONE control column, flush right (components.css --vs-ctrl-w): every field the same
  // width whatever its type, so Provider/Model/Server read as a table, not ragged.
  void LlmSettingsForm::hugRight(QWidget* w) {
    w->setFixedWidth(CTRL_W);
    int row = -1;
    QFormLayout::ItemRole role;
    form_->getWidgetPosition(w, &row, &role);
    if (row >= 0)
      if (QLayoutItem* it = form_->itemAt(row, QFormLayout::FieldRole))
        it->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  }

  // The transport seam model suggestions go through, the debounce that re-probes once
  // typing settles, and every field's change wiring.
  void LlmSettingsForm::wireProviderFields() {

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

  void LlmSettingsForm::refreshStatus() {
    const QString provider = provider_->currentData().toString();
    // "none" is local-only (contract §5): nothing is probed or sent anywhere.
    // Text matches the browser's llmSettingsModal.js renderStatus() verbatim.
    if (provider == QLatin1String("none")) {
      ++probeGen_;  // invalidate any probe still in flight
      setStatus(STATUS_ERROR_COLOR,
                QStringLiteral("Assistant turned off — nothing is sent anywhere."));
      return;
    }
    const QString serverUrl = server_->currentData().toString();
    if (provider == QLatin1String("stencil-server") && serverUrl.isEmpty()) {
      ++probeGen_;
      setStatus(STATUS_ERROR_COLOR,
                QStringLiteral("No collaboration server configured — assistant turned off."));
      return;
    }
    stencil::llm::LlmSettings cfg;
    cfg.provider = provider;
    cfg.baseUrl = baseUrl_->text().trimmed();
    cfg.apiKey = apiKey_->text().trimmed();
    cfg.serverUrl = serverUrl;
    setStatus(STATUS_CONNECTING_COLOR, QStringLiteral("Checking the configured LLM…"));
    const int gen = ++probeGen_;
    QPointer<LlmSettingsForm> self(this);
    client_->probe(cfg, [self, gen, provider, serverUrl](stencil::llm::LlmProbeResult r) {
      if (!self || gen != self->probeGen_) return;  // superseded by a newer edit
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

