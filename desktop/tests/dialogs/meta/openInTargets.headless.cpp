// Headless check of Project ▸ Open In Targets… (src/dialogs/meta/OpenInTargetsDialog): an http(s) browser
// URL or none, a Telegram username or none, the '@' dropped, and Save refused while either is malformed.
#include "OpenInTargetsDialog.hpp"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>

using stencil::gui::OpenInTargetsDialog;

#include "../../support/check.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  std::printf("the browser URL:\n");
  check(OpenInTargetsDialog::isBrowserUrlValid(""), "empty hides the target");
  check(OpenInTargetsDialog::isBrowserUrlValid("http://localhost:8080"), "a local http address");
  check(OpenInTargetsDialog::isBrowserUrlValid("https://stencil.example.com/app/"), "an https address with a path");
  check(!OpenInTargetsDialog::isBrowserUrlValid("file:///etc/passwd"), "no file: URL");
  check(!OpenInTargetsDialog::isBrowserUrlValid("javascript:alert(1)"), "no script URL");
  check(!OpenInTargetsDialog::isBrowserUrlValid("localhost:8080"), "a bare host:port is not a URL");
  check(!OpenInTargetsDialog::isBrowserUrlValid("http://"), "a scheme needs a host");

  std::printf("the bot:\n");
  check(OpenInTargetsDialog::isBotUsernameValid(""), "empty hides the target");
  check(OpenInTargetsDialog::isBotUsernameValid("my_stencil_bot"), "a username");
  check(OpenInTargetsDialog::isBotUsernameValid("@my_stencil_bot"), "a leading '@' is allowed");
  check(!OpenInTargetsDialog::isBotUsernameValid("abc"), "too short");
  check(!OpenInTargetsDialog::isBotUsernameValid("my bot"), "no spaces");

  std::printf("the dialog:\n");
  {
    OpenInTargetsDialog dlg("http://localhost:8080", "@my_stencil_bot");
    check(dlg.botUsername() == "my_stencil_bot", "the '@' is dropped");
    dlg.findChild<QLineEdit*>("openInBrowserUrl")->setText("  https://example.com  ");
    check(dlg.browserUrl() == "https://example.com", "the URL is trimmed");
    dlg.accept();
    check(dlg.result() == QDialog::Accepted, "valid fields save");
  }
  {
    OpenInTargetsDialog dlg("ftp://example.com", "");
    dlg.accept();
    auto* error = dlg.findChild<QLabel*>("openInTargetsError");
    check(dlg.result() != QDialog::Accepted, "a non-http(s) URL is refused");
    check(error && !error->isHidden() && error->text().contains("http"), "and the refusal names the field");
    dlg.findChild<QLineEdit*>("openInBrowserUrl")->setText("");
    dlg.accept();
    check(dlg.result() == QDialog::Accepted && error->isHidden(), "cleared, it saves and the error goes");
  }
  return failures ? 1 : 0;
}
