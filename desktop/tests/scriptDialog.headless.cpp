// The script window (dialogs/ScriptDialog + ScriptEditorWidget + ScriptHighlighter). Browser
// twin browser/tests/scriptModal.test.js: nothing is REPORTED until the script has been run,
// the three acting buttons are dead while it is empty, and the colouring is the core's tokens.
#include "ScriptDialog.hpp"
#include "ScriptDoc.hpp"
#include "ScriptHighlighter.hpp"

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextLayout>
#include <cstdio>

#include "support/check.hpp"

using stencil::gui::ScriptHighlighter;
using stencil::gui::ScriptDialog;
using stencil::model::ScriptDoc;

namespace {

  template <class W>
  W* find(const ScriptDialog& dlg, const char* name) {
    return dlg.findChild<W*>(QString::fromLatin1(name));
  }

  // The formats the highlighter actually left on a line, in column order.
  QVector<QTextLayout::FormatRange> formatsOn(const QPlainTextEdit* edit, int lineIndex) {
    const QTextBlock block = edit->document()->findBlockByNumber(lineIndex);
    return block.isValid() && block.layout() ? block.layout()->formats()
                                             : QVector<QTextLayout::FormatRange>{};
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  std::printf("an empty editor disables the three acting buttons:\n");
  {
    ScriptDialog dlg{QString()};
    auto* run = find<QPushButton>(dlg, "scriptRun");
    check(run != nullptr, "the Run button is there");
    check(run && !run->isEnabled(), "Run is dead while the editor is empty");

    auto* edit = find<QPlainTextEdit>(dlg, "scriptText");
    check(edit != nullptr, "the editor is there");
    if (edit) edit->setPlainText(QStringLiteral("@filter bw\n"));
    check(run && run->isEnabled(), "typing brings Run back");
    check(dlg.script() == QStringLiteral("@filter bw\n"), "script() hands back what was typed");

    if (edit) edit->setPlainText(QStringLiteral("   \n"));
    check(run && !run->isEnabled(), "whitespace alone is still empty");
  }

  std::printf("nothing is reported until the script is run:\n");
  {
    ScriptDialog dlg(QStringLiteral("@nope 1\n"));
    auto* diag = find<QLabel>(dlg, "scriptDiag");
    check(diag != nullptr, "the diagnostic strip is there");
    check(diag && diag->text().isEmpty(), "a bad script says nothing before it is run");
    check(diag && !diag->property("state").isValid(), "and carries no state to colour");

    dlg.showRunDiagnostics();
    check(diag && diag->text().contains(QStringLiteral("Line 1")), "the run names the line");
    check(diag && diag->property("state").toString() == QStringLiteral("error"),
          "and marks the strip as an error");

    auto* edit = find<QPlainTextEdit>(dlg, "scriptText");
    if (edit) edit->setPlainText(QStringLiteral("@filter bw\n"));
    check(diag && diag->text().isEmpty(), "editing clears the verdict: it was about older text");
  }

  std::printf("the colouring comes from the core's tokens:\n");
  {
    ScriptDialog dlg(QStringLiteral("@filter bw\n# a comment\n"));
    auto* edit = find<QPlainTextEdit>(dlg, "scriptText");
    check(edit != nullptr, "the editor is there");
    if (edit) {
      const auto directive = formatsOn(edit, 0);
      const auto comment = formatsOn(edit, 1);
      check(!directive.isEmpty(), "the directive line is coloured");
      check(!comment.isEmpty(), "the comment line is coloured");
      if (!directive.isEmpty() && !comment.isEmpty()) {
        check(directive[0].format.foreground().color() != comment[0].format.foreground().color(),
              "a directive and a comment are not the same ink");
        check(comment[0].format.fontItalic(), "a comment is italic");
        check(directive[0].start == 0 && directive[0].length == 7,
              "the @filter span covers the directive only");
      }
    }
  }

  std::printf("a diagnostic underlines the token it is about:\n");
  {
    ScriptDialog dlg(QStringLiteral("@filter nosuchmode\n"));
    dlg.showRunDiagnostics();
    auto* edit = find<QPlainTextEdit>(dlg, "scriptText");
    bool wavy = false;
    for (const auto& f : formatsOn(edit, 0))
      if (f.format.underlineStyle() == QTextCharFormat::WaveUnderline) wavy = true;
    check(wavy, "the offending token gained a wavy underline");
  }

  std::printf("a non-ASCII character does not shift the spans after it:\n");
  {
    // The core lexes UTF-8, so an em dash is three BYTES where the editor counts one QChar.
    ScriptDialog dlg(QStringLiteral("@filter bw \u2014 no\n@save \"Z\u00fcrich\"\n"));
    auto* edit = find<QPlainTextEdit>(dlg, "scriptText");
    const auto dashed = formatsOn(edit, 0);
    const auto quoted = formatsOn(edit, 1);
    check(!dashed.isEmpty() && !quoted.isEmpty(), "both lines are coloured");
    if (!dashed.isEmpty() && !quoted.isEmpty()) {
      check(dashed.back().start == 13 && dashed.back().length == 2,
            "the token after the em dash is not pushed right by its two extra bytes");
      check(quoted.back().start == 6 && quoted.back().length == 8,
            "the quoted name spans its eight QChars, not nine bytes' worth");
    }
  }

  std::printf("the highlighter survives a theme flip:\n");
  {
    QPlainTextEdit edit;
    ScriptHighlighter hl(edit.document());
    edit.setPlainText(QStringLiteral("@crop 10%\n"));
    hl.setProgram(ScriptDoc::parse(edit.toPlainText()), false);
    hl.restyle();   // rebuilds every format against the live palette
    check(!formatsOn(&edit, 0).isEmpty(), "the spans are still coloured after a restyle");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
