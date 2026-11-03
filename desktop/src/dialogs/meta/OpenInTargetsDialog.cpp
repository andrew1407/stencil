#include "OpenInTargetsDialog.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>

namespace stencil::gui {

  namespace {
    QString stripAt(const QString& name) {
      QString s = name.trimmed();
      while (s.startsWith(QLatin1Char('@'))) s.remove(0, 1);
      return s;
    }
  }  // namespace

  OpenInTargetsDialog::OpenInTargetsDialog(const QString& browserUrl, const QString& botUsername, QWidget* parent)
      : QDialog(parent) {
    setObjectName(QStringLiteral("openInTargetsDialog"));
    setWindowTitle(tr("Open In targets"));
    ModalChrome chrome = installModalChrome(this, "external", tr("Open In targets"));

    url = new QLineEdit(browserUrl, this);
    url->setObjectName(QStringLiteral("openInBrowserUrl"));
    url->setPlaceholderText(QStringLiteral("http://localhost:8080 (empty = hidden)"));
    url->setToolTip(tr("Base URL of the Stencil browser app, used by \"Open In… → Browser app\""));
    chrome.body->addWidget(modalRow(this, tr("Browser app URL"), url, /*grow=*/true));

    bot = new QLineEdit(botUsername, this);
    bot->setObjectName(QStringLiteral("openInBotUsername"));
    bot->setPlaceholderText(QStringLiteral("e.g. my_stencil_bot (empty = hidden)"));
    bot->setToolTip(tr("Telegram bot username (without @) for \"Open In… → Telegram bot\"; "
                       "leave empty to hide that option"));
    chrome.body->addWidget(modalRow(this, tr("Telegram bot"), bot, /*grow=*/true));

    error = new QLabel(this);
    error->setObjectName(QStringLiteral("openInTargetsError"));
    error->setWordWrap(true);
    error->hide();
    chrome.body->addWidget(error);

    QHBoxLayout* footer = addModalFooter(chrome, tr("An empty field hides that target."));
    auto* cancel = new QPushButton(tr("Cancel"), this);
    makeModalCta(cancel, "x");
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancel);
    auto* save = new QPushButton(tr("Save"), this);
    makeModalCta(save, "check");
    save->setDefault(true);
    connect(save, &QPushButton::clicked, this, &OpenInTargetsDialog::accept);
    footer->addWidget(save);

    setFixedWidth(MODAL_WIDTH);
    url->setFocus();
  }

  QString OpenInTargetsDialog::browserUrl() const { return url->text().trimmed(); }
  QString OpenInTargetsDialog::botUsername() const { return stripAt(bot->text()); }

  bool OpenInTargetsDialog::isBrowserUrlValid(const QString& value) {
    const QString s = value.trimmed();
    if (s.isEmpty()) return true;
    const QUrl u(s, QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    return u.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https")) && !u.host().isEmpty();
  }

  bool OpenInTargetsDialog::isBotUsernameValid(const QString& name) {
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z0-9_]{5,32}$"));
    const QString s = stripAt(name);
    return s.isEmpty() || shape.match(s).hasMatch();
  }

  void OpenInTargetsDialog::accept() {
    QString problem;
    if (!isBrowserUrlValid(url->text())) problem = tr("The browser app URL must be an http(s) address.");
    else if (!isBotUsernameValid(bot->text())) problem = tr("A Telegram bot name is 5–32 letters, digits or underscores.");
    error->setText(problem);
    error->setVisible(!problem.isEmpty());
    if (problem.isEmpty()) QDialog::accept();
  }

}  // namespace stencil::gui
