#pragma once
#include <QDialog>
#include <QString>

class QCheckBox;
class QPushButton;

// "Open in…" dialog. Mirrors browser/js/ui/openInModal.js: mirror the CURRENT
// session into another Stencil front-end — the browser app or the Telegram bot.
// Unusable targets are HIDDEN, not greyed (the caller only opens the dialog when at
// least one is available). exec(); on QDialog::Accepted read outcome()/incognito().
namespace stencil::gui {

  class OpenInDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Outcome { Browser, Telegram };

    // serverProject: the session is linked to a server project on `serverUrl` (shown
    // in the status line). browserAvailable / telegramAvailable gate each button's
    // visibility (already folded in the config + server-project checks by the caller).
    // startIncognito seeds the incognito checkbox.
    OpenInDialog(QWidget* parent, bool serverProject, const QString& serverUrl,
                 bool browserAvailable, bool telegramAvailable, bool startIncognito);

    bool incognito() const;
    Outcome outcome() const { return outcome_; }

   private:
    QCheckBox* incognito_ = nullptr;
    QPushButton* browser_ = nullptr;
    QPushButton* telegram_ = nullptr;
    Outcome outcome_ = Outcome::Browser;
  };

}
