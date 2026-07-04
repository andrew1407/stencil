#include "openInDialog.hpp"
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  OpenInDialog::OpenInDialog(QWidget* parent, bool serverProject, const QString& serverUrl,
                             bool browserAvailable, bool telegramAvailable, bool startIncognito)
      : QDialog(parent) {
    setWindowTitle("Open In…");
    setMinimumWidth(440);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    // Status line: what will be handed over (server reference vs inline bytes).
    auto* status = new QLabel(
        serverProject
            ? QString("Server project on %1 — the link carries only the server "
                      "reference (no token).").arg(serverUrl)
            : QString("Local/incognito session — the image and layout are sent "
                      "inline (no server involved)."),
        this);
    status->setWordWrap(true);
    form->addRow("Project:", status);

    // Incognito on the RECEIVING side (Stencil's own never-persisted mode).
    incognito_ = new QCheckBox("Open there without saving (incognito)", this);
    incognito_->setChecked(startIncognito);
    incognito_->setToolTip(
        "The receiving app opens the project in Stencil incognito mode — nothing "
        "is persisted there");
    form->addRow("Incognito:", incognito_);
    layout->addLayout(form);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setToolTip("Close without opening anything");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancel);

    // Each button is added only when its target is available (HIDDEN, not greyed,
    // when not — matching the browser modal); the caller only opens the dialog when
    // at least one is available, so the footer is never empty.
    if (browserAvailable) {
      browser_ = new QPushButton("Browser app", this);
      browser_->setToolTip("Open this project in the Stencil browser app "
                           "(the base URL is set in Settings)");
      connect(browser_, &QPushButton::clicked, this, [this] {
        outcome_ = Outcome::Browser;
        accept();
      });
      btnRow->addWidget(browser_);
    }
    // Telegram needs a configured bot username AND a server project (a 64-char start
    // payload can't carry image bytes) — both folded into telegramAvailable.
    if (telegramAvailable) {
      telegram_ = new QPushButton("Telegram bot", this);
      telegram_->setToolTip("Open this server project in the Telegram bot");
      connect(telegram_, &QPushButton::clicked, this, [this] {
        outcome_ = Outcome::Telegram;
        accept();
      });
      btnRow->addWidget(telegram_);
    }
    layout->addLayout(btnRow);
  }

  bool OpenInDialog::incognito() const { return incognito_->isChecked(); }

}
