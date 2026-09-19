#include "../support/modalChrome.hpp"
#include "../support/SearchCombo.hpp"
#include "LlmSettingsForm.hpp"
#include "../support/easeWindowHeight.hpp"
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

  LlmSettingsForm::~LlmSettingsForm() = default;

  void LlmSettingsForm::focusProvider() { provider_->setFocus(); }

  void LlmSettingsForm::syncRows(const QString& prevProvider) {
    const int hostH0 = window() ? window()->height() : 0;   // before the rows move
    const QString provider = provider_->currentData().toString();
    const bool off = provider == "none";  // assistant off: every row irrelevant
    const bool viaServer = provider == "stencil-server";
    const bool direct = !off && !viaServer;  // called by us over the network
    if (mode_ == RowMode::HIDE_ROWS) {
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
      // A layout caches each child's height in its own item, and hiding one does not clear
      // that — the box would keep the vanished line's space. updateGeometry() re-reports it.
      if (noteBox_) {
        if (QLayout* l = noteBox_->layout()) l->invalidate();
        noteBox_->updateGeometry();
      }
      updateGeometry();
      pinNoteHeights();
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
    // The host dialog grows and shrinks with the visible rows, EASING there rather than
    // snapping (browser twin: easeBoxHeight on this same modal) — but only once shown: a
    // resize during construction freezes a too-small size that stacks the rows.
    if (mode_ == RowMode::HIDE_ROWS && window()->isVisible()) {
      if (QLayout* l = window()->layout()) { l->invalidate(); l->activate(); }
      support::easeWindowHeight(window(), window()->sizeHint().height(), hostH0);
    }
  }

  // A wrapped QLabel's hint is measured at a GUESSED width, which the layout then budgets.
  void LlmSettingsForm::pinNoteHeights() {
    for (QLabel* l : {saveChatsHint_, note_}) {
      if (!l || !l->isVisible() || l->width() <= 0) continue;
      const int h = l->heightForWidth(l->width());
      if (h > 0 && h != l->minimumHeight()) l->setFixedHeight(h);
    }
  }

  // Deferred: the children are laid out AFTER this resize, so their widths are still the
  // previous pass's here — a pin measured now would use the wrong width.
  void LlmSettingsForm::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { pinNoteHeights(); });
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

