#include "../../support/skinPrefs.hpp"
#include "LlmSettingsForm.hpp"
#include "../../support/easeWindowHeight.hpp"
#include "LlmClient.hpp"
#include "llmSettings.hpp"
#include "SessionKey.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>

namespace stencil::gui {

  LlmSettingsForm::~LlmSettingsForm() = default;

  void LlmSettingsForm::focusEntry() {
    const bool wantsKey = provider->currentData().toString() == QLatin1String("anthropic") &&
                          stencil::llm::SessionKey::instance().key().isEmpty();
    if (wantsKey) anthropicKey->setFocus();
    else provider->setFocus();
  }

  void LlmSettingsForm::syncRows(const QString& prevProvider) {
    const int hostH0 = window() ? window()->height() : 0;   // before the rows move
    const QString provider = this->provider->currentData().toString();
    const bool off = provider == "none";  // assistant off: every row irrelevant
    const bool viaServer = provider == "stencil-server";
    const bool direct = !off && !viaServer;  // called by us over the network
    const bool keyed = provider == "anthropic";   // the session key, not a stored one
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
      showRow(anthropicKey, anthropicKeyDiv, keyed);
      showRow(keyStatusRow, keyStatusDiv, keyed);
      showRow(server, serverDiv, viaServer);
      // The merged note stays (the §12.2 line always applies); only its
      // local-endpoint and session-key lines are provider-conditional.
      note->setVisible(direct && !keyed);
      keyNote->setVisible(keyed);
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
      anthropicKey->setEnabled(keyed);
      keyStatusRow->setEnabled(keyed);
      server->setEnabled(viaServer);
    }
    // A switch gives the new provider its default unless the user typed a URL: empty or any
    // provider's default was never typed (browser twin: withProvider). Opening fills an empty one.
    const QString text = baseUrl->text().trimmed();
    if (provider != prevProvider ? text.isEmpty() || stencil::llm::isDefaultLlmBaseUrl(text)
                                 : direct && text.isEmpty())
      baseUrl->setText(stencil::llm::defaultLlmBaseUrl(provider));
    // The host dialog EASES to its new height rather than snapping (browser twin: easeBoxHeight on
    // this same modal) - but only once shown: a resize during construction freezes a too-small size.
    if (mode == RowMode::HIDE_ROWS && window()->isVisible()) {
      // The form's own layout re-measures the rows it just hid only once its request is run.
      support::relayout(this);
      if (QLayout* l = window()->layout()) { l->invalidate(); l->activate(); }
      support::easeWindowHeight(window(), support::naturalHeight(window()), hostH0);
    }
  }

  // A wrapped QLabel's hint is measured at a GUESSED width, which the layout then budgets.
  void LlmSettingsForm::pinNoteHeights() {
    for (QLabel* l : {saveChatsHint, note, keyNote}) {
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
    cfg.apiKey = cfg.provider == QLatin1String("anthropic") ? requestKey() : apiKey->text().trimmed();
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
    // Under the skin the lamp is square (browser webcore/chat.css .conn-status).
    statusDot->setText(QStringLiteral("<span style=\"color:%1;\">%2</span>")
                            .arg(QLatin1String(color), support::isWebcore() ? QStringLiteral("■") : QStringLiteral("●")));
    status->setText(text);
  }

  void LlmSettingsForm::applyTo(Settings& s) const {
    s.llmProvider = provider->currentData().toString();
    s.llmBaseUrl = baseUrl->text().trimmed();
    s.llmModel = model->currentText().trimmed();
    // The openai-compat key is stored; one that IS the anthropic session key never is (§5).
    const QString stored = apiKey->text().trimmed();
    s.llmApiKey = !stored.isEmpty() && (stored == requestKey() || stored == anthropicKey->text().trimmed())
                      ? QString() : stored;
    s.llmServerUrl = server->currentData().toString();
    s.saveChatsWithProject = saveChats->isChecked();
  }

  void LlmSettingsForm::renderKeyStatus() {
    const QDateTime until = stencil::llm::SessionKey::instance().expiresAt();
    keyStatus->setText(keyStatusText(until, QDateTime::currentDateTime()));
    forgetKey->setEnabled(until.isValid());
  }

  QString LlmSettingsForm::requestKey() const {
    const QString typed = anthropicKey->text().trimmed();
    return typed.isEmpty() ? stencil::llm::SessionKey::instance().key() : typed;
  }

  void LlmSettingsForm::commitAnthropicKey() {
    const QString typed = anthropicKey->text().trimmed();
    anthropicKey->clear();
    if (provider->currentData().toString() == QLatin1String("anthropic") && !typed.isEmpty())
      stencil::llm::SessionKey::instance().hold(typed);
  }

  QString LlmSettingsForm::keyStatusText(const QDateTime& until, const QDateTime& now) {
    if (!until.isValid()) return QStringLiteral("No key for this session.");
    const QDateTime local = until.toLocalTime();
    const QLocale loc = QLocale::system();
    const QString day = local.date() == now.toLocalTime().date()
                            ? QString()
                            : loc.dayName(local.date().dayOfWeek(), QLocale::ShortFormat) + QLatin1Char(' ');
    return QStringLiteral("Key kept until %1%2 or until Stencil quits.")
        .arg(day, loc.toString(local.time(), QLocale::ShortFormat));
  }
}

