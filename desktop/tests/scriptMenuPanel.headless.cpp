// The script flyout hosted in the canvas context menu (dialogs/ScriptMenuPanel). Browser twin
// browser/tests/ctxScript.test.js: it behaves like the window it mirrors (nothing reported
// until a run, the acting buttons dead while empty) and like a code editor (Tab, Ctrl+Enter).
#include "ScriptMenuPanel.hpp"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <cstdio>

#include "support/check.hpp"

using stencil::gui::ScriptMenuPanel;

namespace {

  template <class W>
  W* find(const ScriptMenuPanel& panel, const char* name) {
    return panel.findChild<W*>(QString::fromLatin1(name));
  }

  void sendKey(QWidget* w, int key, Qt::KeyboardModifiers mods) {
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(w, &press);
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  std::printf("an empty editor disables the three acting buttons:\n");
  {
    ScriptMenuPanel panel(nullptr, {});
    auto* run = find<QPushButton>(panel, "scriptMenuRun");
    auto* copy = find<QPushButton>(panel, "scriptMenuCopy");
    auto* download = find<QPushButton>(panel, "scriptMenuDownload");
    auto* upload = find<QPushButton>(panel, "scriptMenuUpload");
    check(run && copy && download && upload, "Copy, Download, Upload and Run are all there");
    check(run && !run->isEnabled(), "Run is dead while the editor is empty");
    check(copy && !copy->isEnabled() && download && !download->isEnabled(),
          "so are Copy and Download");
    check(upload && upload->isEnabled(), "Upload always has something to do");

    panel.setScript(QStringLiteral("@filter bw\n"));
    check(run && run->isEnabled(), "typing brings Run back");
    check(panel.script() == QStringLiteral("@filter bw\n"), "script() hands back what was typed");

    panel.setScript(QStringLiteral("   \n"));
    check(run && !run->isEnabled(), "whitespace alone is still empty");
  }

  std::printf("nothing is reported until the script is run:\n");
  {
    ScriptMenuPanel panel(nullptr, {});
    panel.setScript(QStringLiteral("@nope 1\n"));
    auto* diag = find<QLabel>(panel, "scriptMenuDiag");
    check(diag != nullptr, "the diagnostic strip is there");
    check(diag && diag->text().isEmpty(), "a bad script says nothing before it is run");
    check(diag && !diag->property("state").isValid(), "and carries no state to colour");

    panel.showRunDiagnostics();
    check(diag && diag->text().contains(QStringLiteral("Line 1")), "the run names the line");
    check(diag && diag->property("state").toString() == QStringLiteral("error"),
          "and marks the strip as an error");

    panel.setScript(QStringLiteral("@filter bw\n"));
    check(diag && diag->text().isEmpty(), "editing clears the verdict: it was about older text");
  }

  std::printf("Run hands the text to the window, and the menu keeps it:\n");
  {
    QString ran;
    int runs = 0;
    ScriptMenuPanel::Hooks hooks;
    hooks.run = [&](QString text) { ran = text; ++runs; };
    ScriptMenuPanel panel(nullptr, hooks);
    auto* run = find<QPushButton>(panel, "scriptMenuRun");

    run->click();
    check(runs == 0, "an empty editor runs nothing");

    panel.setScript(QStringLiteral("@filter nosuchmode\n"));
    run->click();
    check(runs == 1 && ran == QStringLiteral("@filter nosuchmode\n"),
          "Run hands the editor's text over verbatim");
    auto* diag = find<QLabel>(panel, "scriptMenuDiag");
    check(diag && !diag->text().isEmpty(), "a failed run reports, and the panel stays put");
    check(panel.script() == QStringLiteral("@filter nosuchmode\n"),
          "the text that failed is still there to fix");
  }

  std::printf("the editor owns Tab and Ctrl+Enter:\n");
  {
    int runs = 0;
    ScriptMenuPanel::Hooks hooks;
    hooks.run = [&](QString) { ++runs; };
    ScriptMenuPanel panel(nullptr, hooks);
    auto* edit = find<QPlainTextEdit>(panel, "scriptMenuText");
    check(edit != nullptr, "the editor is there");
    check(edit && edit->property("keepTab").toBool(),
          "it is flagged as owning Tab (the menu's walk steps aside)");

    sendKey(edit, Qt::Key_Tab, Qt::NoModifier);
    check(panel.script() == QStringLiteral("  "), "Tab indents by two spaces");

    panel.setScript(QStringLiteral("@filter bw"));
    sendKey(edit, Qt::Key_Return, Qt::ControlModifier);
    check(runs == 1, "Ctrl+Enter runs the script");
    sendKey(edit, Qt::Key_Return, Qt::NoModifier);
    check(runs == 1, "a bare Return is a newline, not a run");
  }

  std::printf("Copy, Download and Upload reach their surfaces:\n");
  {
    int uploads = 0, downloads = 0;
    QString noticed;
    ScriptMenuPanel::Hooks hooks;
    hooks.upload = [&] { ++uploads; };
    hooks.download = [&] { ++downloads; };
    hooks.notice = [&](QString text) { noticed = text; };
    ScriptMenuPanel panel(nullptr, hooks);
    panel.setScript(QStringLiteral("@save\n"));

    find<QPushButton>(panel, "scriptMenuCopy")->click();
    check(QApplication::clipboard()->text() == QStringLiteral("@save\n"),
          "Copy puts the script on the clipboard");
    check(noticed == QStringLiteral("Script copied"), "and says so");

    find<QPushButton>(panel, "scriptMenuUpload")->click();
    find<QPushButton>(panel, "scriptMenuDownload")->click();
    check(uploads == 1 && downloads == 1,
          "the two file actions are the window's (a dialog cannot open under the popup grab)");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
