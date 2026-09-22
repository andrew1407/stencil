#include "OpenInDialog.hpp"
#include "deepLink.hpp"
#include "iconSet.hpp"
#include "../../support/modal/modalChrome.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  OpenInDialog::OpenInDialog(QWidget* parent, bool serverProject, const QString& serverUrl,
                             bool browserAvailable, bool telegramAvailable, bool startIncognito,
                             const QString& serverId)
      : QDialog(parent), serverUrl(serverUrl), serverId(serverId) {
    setWindowTitle("Open In…");
    setMinimumWidth(MODAL_WIDTH);

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
    incognito = new QCheckBox(tr("Open it there without saving (Stencil incognito mode)."), this);
    incognito->setChecked(startIncognito);
    rows->addWidget(incogLbl, 3, 0);
    rows->addWidget(incognito, 3, 1);
    chrome.body->addLayout(rows);

    // Fallback row (browser #open-in-fallback-row): shown when a Telegram start payload cannot fit
    // in 64 chars - the two bot commands as selectable code, and a copy chip. Hidden until then.
    fallbackRow = new QWidget(this);
    {
      auto* wrap = new QVBoxLayout(fallbackRow);
      wrap->setContentsMargins(0, 0, 0, 0);
      wrap->setSpacing(0);
      wrap->addSpacing(8);
      wrap->addWidget(modalDivider(fallbackRow));
      wrap->addSpacing(8);
      auto* h = new QHBoxLayout;
      h->setContentsMargins(0, 0, 0, 0);
      h->setSpacing(16);
      auto* lbl = new QLabel(tr("In the bot"), fallbackRow);
      h->addWidget(lbl, 0, Qt::AlignTop);
      auto* cmdRow = new QHBoxLayout;
      cmdRow->setContentsMargins(0, 0, 0, 0);
      cmdRow->setSpacing(8);
      fallbackCmds = new QLabel(fallbackRow);
      fallbackCmds->setObjectName(QStringLiteral("openInFallbackCmds"));
      fallbackCmds->setTextInteractionFlags(Qt::TextSelectableByMouse);
      cmdRow->addWidget(fallbackCmds, 1);
      auto* copy = new QPushButton(fallbackRow);
      copy->setObjectName(QStringLiteral("openInFallbackCopy"));
      copy->setProperty("miniChip", true);
      copy->setFixedSize(34, 29);
      copy->setIconSize(QSize(14, 14));
      copy->setIcon(themedIcon("copy", palette().color(QPalette::WindowText), 14));
      copy->setToolTip(tr("Copy commands"));
      copy->setAutoDefault(false);
      connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(fallbackCmds->text());
        emit toast(tr("Commands copied"), false);
      });
      cmdRow->addWidget(copy, 0, Qt::AlignTop);
      h->addLayout(cmdRow, 1);
      wrap->addLayout(h);
    }
    fallbackRow->hide();
    chrome.body->addWidget(fallbackRow);
    chrome.body->addStretch(1);

    // Footer (browser settings-footer): the live hint (#open-in-hint, empty until the
    // fallback speaks), then every enabled action in the accent fill, Cancel included.
    QHBoxLayout* btnRow = addModalFooter(chrome, QString(), /*liveHint=*/true);
    hint = chrome.footerHint;
    auto* cancel = new QPushButton(tr("Cancel"), this);
    makeModalCta(cancel, "x");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancel);

    // Each button is added only when its target is available (HIDDEN, not greyed, matching the browser
    // modal); the caller only opens the dialog when at least one is, so the footer is never empty.
    if (browserAvailable) {
      browser = new QPushButton(tr("Browser app"), this);
      makeModalCta(browser, "external");
      connect(browser, &QPushButton::clicked, this, [this] {
        outcome = Outcome::BROWSER;
        accept();
      });
      btnRow->addWidget(browser);
    }
    // Telegram needs a configured bot username AND a server project (a 64-char start
    // payload can't carry image bytes) — both folded into telegramAvailable.
    if (telegramAvailable) {
      telegram = new QPushButton(tr("Telegram bot"), this);
      makeModalCta(telegram, "message");
      connect(telegram, &QPushButton::clicked, this, [this] {
        // A payload that fits is the owner's link to open; one that doesn't stays
        // here as the manual recipe (browser: the modal stays open on the fallback).
        if (deepLink::encodeTelegramStartPayload(this->serverUrl, this->serverId).isEmpty()) {
          showTelegramFallback();
          return;
        }
        outcome = Outcome::TELEGRAM;
        accept();
      });
      btnRow->addWidget(telegram);
    }
  }

  void OpenInDialog::showTelegramFallback() {
    fallbackCmds->setText(QStringLiteral("/connect %1\n/fetch %2").arg(serverUrl, serverId));
    fallbackRow->show();
    if (hint)
      hint->setText(tr("The link is too long for Telegram — open the bot and paste these commands."));
    adjustSize();
    emit telegramFallback();
  }

  bool OpenInDialog::getIncognito() const { return incognito->isChecked(); }
  bool OpenInDialog::fallbackShown() const { return fallbackRow && fallbackRow->isVisible(); }
  QString OpenInDialog::fallbackCommands() const {
    return fallbackCmds ? fallbackCmds->text() : QString();
  }

}
