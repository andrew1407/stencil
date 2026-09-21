#include "../../support/modal/modalChrome.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include "LlmSettingsForm.hpp"
#include "../../support/easeWindowHeight.hpp"
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

  void LlmSettingsForm::focusProvider() { provider->setFocus(); }

  void LlmSettingsForm::syncRows(const QString& prevProvider) {
    const int hostH0 = window() ? window()->height() : 0;   // before the rows move
    const QString provider = this->provider->currentData().toString();
    const bool off = provider == "none";  // assistant off: every row irrelevant
    const bool viaServer = provider == "stencil-server";
    const bool direct = !off && !viaServer;  // called by us over the network
    if (mode == RowMode::HIDE_ROWS) {
      // Browser parity: irrelevant rows disappear rather than sitting greyed out —
      // each together with its .vs-row hairline.
      const auto showRow = [this](QWidget* row, QFrame* divider, bool on) {
        form->setRowVisible(row, on);
        if (divider) form->setRowVisible(divider, on);
      };
      showRow(baseUrl, baseUrlDiv, direct);
      showRow(model, modelDiv, !off);
      showRow(apiKey, apiKeyDiv, provider == "openai-compat");
      showRow(server, serverDiv, viaServer);
      // The merged note stays (the §12.2 line always applies); only its
      // local-endpoint line is provider-conditional.
      note->setVisible(direct);
      // A layout caches each child's height in its own item, and hiding one does not clear
      // that — the box would keep the vanished line's space. updateGeometry() re-reports it.
      if (noteBox) {
        if (QLayout* l = noteBox->layout()) l->invalidate();
        noteBox->updateGeometry();
      }
      updateGeometry();
      pinNoteHeights();
    } else {
      baseUrl->setEnabled(direct);
      model->setEnabled(!off);
      apiKey->setEnabled(provider == "openai-compat");
      server->setEnabled(viaServer);
    }
    // Re-fill the base URL on a provider switch when it still holds the PREVIOUS
    // provider's default (i.e. the user never customized it).
    const QString text = baseUrl->text().trimmed();
    if (direct &&
        (text.isEmpty() || text == stencil::llm::defaultLlmBaseUrl(prevProvider)))
      baseUrl->setText(stencil::llm::defaultLlmBaseUrl(provider));
    // The host dialog EASES to its new height rather than snapping (browser twin: easeBoxHeight on
    // this same modal) - but only once shown: a resize during construction freezes a too-small size.
    if (mode == RowMode::HIDE_ROWS && window()->isVisible()) {
      if (QLayout* l = window()->layout()) { l->invalidate(); l->activate(); }
      support::easeWindowHeight(window(), window()->sizeHint().height(), hostH0);
    }
  }

  // A wrapped QLabel's hint is measured at a GUESSED width, which the layout then budgets.
  void LlmSettingsForm::pinNoteHeights() {
    for (QLabel* l : {saveChatsHint, note}) {
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
    cfg.provider = provider->currentData().toString();
    cfg.baseUrl = baseUrl->text().trimmed();
    cfg.apiKey = apiKey->text().trimmed();
    cfg.serverUrl = server->currentData().toString();
    client->listModels(cfg, [this](QStringList names) {
      // The current edit text is preserved across repopulation, so a
      // free-typed model always survives a suggestion refresh.
      const QString keep = model->currentText();
      const QSignalBlocker block(model);
      model->clear();
      model->addItems(names);
      model->setCurrentIndex(-1);
      model->setEditText(keep);
    });
  }

  // Paint the status row: the dot carries the state colour, the text stays the
  // muted .chat-server-status type (browser #chat-server-status-row).
  void LlmSettingsForm::setStatus(const char* color, const QString& text) {
    statusDot->setText(QStringLiteral("<span style=\"color:%1;\">●</span>")
                            .arg(QLatin1String(color)));
    status->setText(text);
  }

  void LlmSettingsForm::applyTo(Settings& s) const {
    s.llmProvider = provider->currentData().toString();
    s.llmBaseUrl = baseUrl->text().trimmed();
    s.llmModel = model->currentText().trimmed();
    s.llmApiKey = apiKey->text().trimmed();
    s.llmServerUrl = server->currentData().toString();
    s.saveChatsWithProject = saveChats->isChecked();
  }
}

