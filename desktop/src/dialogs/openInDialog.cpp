#include "openInDialog.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  OpenInDialog::OpenInDialog(QWidget* parent, bool serverProject, const QString& serverUrl,
                             bool browserAvailable, bool telegramAvailable, bool startIncognito)
      : QDialog(parent) {
    setWindowTitle("Open In…");
    setMinimumWidth(520);

    // Browser openInModal.js parity: shared modal shell, one .vs-section, and the
    // label/value rows split by hairlines.
    ModalChrome chrome = installModalChrome(this, "external", tr("Open In…"));
    chrome.body->addWidget(modalSectionLabel(tr("Open the current project in another app"), this));

    auto* rows = new QGridLayout;
    rows->setHorizontalSpacing(16);
    rows->setVerticalSpacing(0);
    rows->setColumnStretch(1, 1);
    const QString mutedCss =
        QString("color: %1;").arg(palette().color(QPalette::PlaceholderText).name());

    // Row: what will be handed over (server reference vs inline bytes).
    auto* projectLbl = new QLabel(tr("Project"), this);
    auto* status = new QLabel(
        serverProject
            ? QString("Server project on %1 — the link carries only the server "
                      "reference (no token).").arg(serverUrl)
            : QString("Local project — the image and layout are sent inline "
                      "(no server involved)."),
        this);
    status->setWordWrap(true);
    status->setStyleSheet(mutedCss);
    rows->addWidget(projectLbl, 0, 0, Qt::AlignTop);
    rows->addWidget(status, 0, 1);
    rows->setRowMinimumHeight(1, 8);
    rows->addWidget(modalDivider(this), 1, 0, 1, 2, Qt::AlignVCenter);
    rows->setRowMinimumHeight(2, 8);

    // Row: incognito on the RECEIVING side (Stencil's own never-persisted mode).
    auto* incogLbl = new QLabel(tr("Incognito"), this);
    incognito_ = new QCheckBox(tr("Open it there without saving (Stencil incognito mode)."), this);
    incognito_->setChecked(startIncognito);
    rows->addWidget(incogLbl, 3, 0);
    rows->addWidget(incognito_, 3, 1);
    chrome.body->addLayout(rows);
    chrome.body->addStretch(1);

    // Footer (browser settings-footer): every enabled action wears the accent fill,
    // Cancel included — the browser's default <button> treatment.
    QHBoxLayout* btnRow = addModalFooter(chrome);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    makeModalCta(cancel, "x");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancel);

    // Each button is added only when its target is available (HIDDEN, not greyed,
    // when not — matching the browser modal); the caller only opens the dialog when
    // at least one is available, so the footer is never empty.
    if (browserAvailable) {
      browser_ = new QPushButton(tr("Browser app"), this);
      makeModalCta(browser_, "external");
      connect(browser_, &QPushButton::clicked, this, [this] {
        outcome_ = Outcome::Browser;
        accept();
      });
      btnRow->addWidget(browser_);
    }
    // Telegram needs a configured bot username AND a server project (a 64-char start
    // payload can't carry image bytes) — both folded into telegramAvailable.
    if (telegramAvailable) {
      telegram_ = new QPushButton(tr("Telegram bot"), this);
      makeModalCta(telegram_, "message");
      connect(telegram_, &QPushButton::clicked, this, [this] {
        outcome_ = Outcome::Telegram;
        accept();
      });
      btnRow->addWidget(telegram_);
    }
  }

  bool OpenInDialog::incognito() const { return incognito_->isChecked(); }

}
