#pragma once
#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QPushButton;
class QWidget;

// "Open in..." dialog. Mirrors browser/js/ui/openInModal.js: mirror the CURRENT session into
// another Stencil front-end - the browser app or the Telegram bot. Unusable targets are HIDDEN,
// not greyed. exec(); on QDialog::Accepted read getOutcome()/getIncognito().
// A Telegram link that cannot fit the 64-char start payload keeps the dialog open and shows the
// browser's fallback row (the two bot commands + copy) instead.
namespace stencil::gui {

  class OpenInDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Outcome { BROWSER, TELEGRAM };

    // serverProject: the session is linked to a server project on `serverUrl`; `serverId` is its id,
    // for the Telegram payload check. browserAvailable / telegramAvailable gate button visibility.
    OpenInDialog(QWidget* parent, bool serverProject, const QString& serverUrl,
                 bool browserAvailable, bool telegramAvailable, bool startIncognito,
                 const QString& serverId = QString());

    bool getIncognito() const;
    Outcome getOutcome() const { return outcome; }
    // The fallback row is showing (the Telegram link did not fit).
    bool fallbackShown() const;
    QString fallbackCommands() const;

   signals:
    // The Telegram link did not fit: the owner opens the bot chat (browser parity).
    void telegramFallback();
    void toast(const QString& text, bool fail);

   private:
    void showTelegramFallback();

    QString serverUrl;
    QString serverId;
    QCheckBox* incognito = nullptr;
    QPushButton* browser = nullptr;
    QPushButton* telegram = nullptr;
    QWidget* fallbackRow = nullptr;
    QLabel* fallbackCmds = nullptr;
    QLabel* hint = nullptr;
    Outcome outcome = Outcome::BROWSER;
  };

}
