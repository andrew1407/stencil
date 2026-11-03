#pragma once
// Where Project ▸ Open In… hands a project to: the browser app's base URL and the Telegram bot.
// Desktop-only: the browser app ships its targets in its own config. An empty field hides that target.
#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;

namespace stencil::gui {

  class OpenInTargetsDialog : public QDialog {
    Q_OBJECT
   public:
    OpenInTargetsDialog(const QString& browserUrl, const QString& botUsername, QWidget* parent = nullptr);

    QString browserUrl() const;    // trimmed; empty = the browser target is hidden
    QString botUsername() const;   // trimmed, any leading '@' dropped; empty = hidden

    // Empty, or an http(s) URL with a host: the one shape a browser hand-off may take.
    static bool isBrowserUrlValid(const QString& url);
    // Empty, or a Telegram username: 5–32 of [A-Za-z0-9_], after any '@'.
    static bool isBotUsernameValid(const QString& name);

   public slots:
    void accept() override;   // refuses, naming the field, while either is malformed

   private:
    QLineEdit* url = nullptr;
    QLineEdit* bot = nullptr;
    QLabel* error = nullptr;
  };

}  // namespace stencil::gui
