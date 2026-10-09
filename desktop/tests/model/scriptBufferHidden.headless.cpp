// The shared .stc (model/ScriptBuffer) reaching a host that is hidden: typing in one host does
// not re-lex the other per keystroke; the hidden one hands back the shared text, and its editor
// and parse catch up when it is shown.
#include "ScriptBuffer.hpp"
#include "ScriptDialog.hpp"
#include "ScriptEditorWidget.hpp"
#include "ScriptMenuPanel.hpp"

#include <QApplication>
#include <QPlainTextEdit>
#include <cstdio>

#include "../support/check.hpp"

using stencil::gui::ScriptDialog;
using stencil::gui::ScriptEditorWidget;
using stencil::gui::ScriptMenuPanel;
using stencil::model::ScriptBuffer;

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  std::printf("a hidden host catches up on show, not per keystroke:\n");
  {
    ScriptBuffer::instance().setText(QString());
    ScriptMenuPanel panel(nullptr, {});
    auto* flyout = panel.findChild<ScriptEditorWidget*>();
    ScriptDialog dlg{QString()};
    dlg.show();
    QApplication::processEvents();

    dlg.findChild<QPlainTextEdit*>(QStringLiteral("scriptText"))
        ->setPlainText(QStringLiteral("@crop 10%\n"));
    check(flyout && flyout->editor()->toPlainText().isEmpty(),
          "the hidden flyout's editor is not rewritten (and re-lexed) per keystroke");
    check(flyout && flyout->getProgram().getOps().isEmpty(), "…so it holds no fresh parse yet");
    check(panel.script() == QStringLiteral("@crop 10%\n"), "…yet it hands back the shared text");

    panel.show();
    QApplication::processEvents();
    check(flyout && flyout->editor()->toPlainText() == QStringLiteral("@crop 10%\n"),
          "showing it brings the text in");
    check(flyout && !flyout->getProgram().getOps().isEmpty(), "…and parses it once");
  }

  std::printf("a visible host follows every keystroke:\n");
  {
    ScriptBuffer::instance().setText(QString());
    ScriptMenuPanel panel(nullptr, {});
    panel.show();
    ScriptDialog dlg{QString()};
    QApplication::processEvents();
    dlg.findChild<QPlainTextEdit*>(QStringLiteral("scriptText"))
        ->setPlainText(QStringLiteral("@filter bw\n"));
    auto* flyout = panel.findChild<ScriptEditorWidget*>();
    check(flyout && flyout->editor()->toPlainText() == QStringLiteral("@filter bw\n"),
          "an open flyout shows the window's text as it is typed");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
