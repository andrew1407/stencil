// The script window (dialogs/ScriptDialog + ScriptEditorWidget + ScriptHighlighter). Browser
// twin browser/tests/scriptModal.test.js: nothing is REPORTED until the script has been run,
// the three acting buttons are dead while it is empty, and the colouring is the core's tokens.
#include "ScriptDialog.hpp"
#include "ScriptDoc.hpp"
#include "ScriptHighlighter.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextLayout>
#include <cstdio>

#include "../../support/check.hpp"

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

    // Browser twin: gateActions() in js/ui/script/editor.js.
    QPushButton* copy = nullptr;
    for (QPushButton* b : dlg.findChildren<QPushButton*>())
      if (b->text() == QStringLiteral("Copy")) copy = b;
    if (edit) edit->setPlainText(QStringLiteral("# just a comment\n"));
    check(run && !run->isEnabled(), "a comment-only script lowers to no ops: nothing to run");
    check(copy && copy->isEnabled(), "but there is text to copy");

    if (edit) edit->setPlainText(QStringLiteral("@nope 1\n"));
    check(run && run->isEnabled(),
          "an errored script still runs: the run is how its errors become visible");
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

  std::printf("only a word the core knows is painted as a directive:\n");
  {
    // The lexer tags every @word DIRECTIVE, so the highlighter checks the core's own list.
    QPlainTextEdit edit;
    ScriptHighlighter hl(edit.document());
    edit.setPlainText(QStringLiteral("@crop 10%\n@CROP 10%\n@nonsense 1\n@rect (0,0) (@1,@1)\n"));
    hl.setProgram(ScriptDoc::parse(edit.toPlainText()), false);
    const auto lower = formatsOn(&edit, 0);
    const auto upper = formatsOn(&edit, 1);
    const auto bogus = formatsOn(&edit, 2);
    const auto templated = formatsOn(&edit, 3);
    check(!lower.isEmpty() && !upper.isEmpty() && !bogus.isEmpty() && !templated.isEmpty(),
          "all four lines are coloured");
    if (lower.isEmpty() || upper.isEmpty() || bogus.isEmpty() || templated.isEmpty())
      return failures ? 1 : 0;
    const QColor ink = lower[0].format.foreground().color();
    check(lower[0].format.fontWeight() == QFont::DemiBold, "@crop keeps the directive face");
    check(upper[0].format.foreground().color() == ink, "@CROP is the same directive: case-free");
    check(bogus[0].format.foreground().color() != ink && !bogus[0].format.fontItalic(),
          "@nonsense is plain text, not an accented directive");
    check(templated[0].format.foreground().color() == ink,
          "a real directive in a template body still highlights");
    bool param = false;
    for (const auto& f : templated)
      if (f.start > 0 && f.length == 2 && f.format.fontItalic()) param = true;
    check(param, "@1 keeps its own param face");
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

  std::printf("an error mark reddens its text, a warning only squiggles:\n");
  {
    QPlainTextEdit edit;
    ScriptHighlighter hl(edit.document());
    edit.setPlainText(QStringLiteral("gcghcghgh gf g\n@filter sepia\n"));
    hl.setProgram(ScriptDoc::parse(edit.toPlainText()), true);   // withDiagnostics
    // Theme-agnostic: whatever danger resolves to, the text wears the SAME colour as the
    // squiggle — which is what .stk-error does by setting colour and underline to --danger.
    bool reddened = false;
    for (const auto& f : formatsOn(&edit, 0))
      if (f.format.underlineStyle() == QTextCharFormat::WaveUnderline
          && f.format.foreground().style() != Qt::NoBrush
          && f.format.foreground().color() == f.format.underlineColor()) reddened = true;
    // Browser .stk-error sets BOTH the colour and the squiggle; the desktop only squiggled.
    check(reddened, "the bad line is danger-coloured, not just underlined");
    // The WHOLE line goes red, not only the token the diagnostic names — but the squiggle
    // stays on that token, so the trailing words are red WITHOUT an underline.
    QColor danger;   // whatever the live theme resolved it to, read off the squiggled span
    for (const auto& f : formatsOn(&edit, 0))
      if (f.format.underlineStyle() == QTextCharFormat::WaveUnderline)
        danger = f.format.foreground().color();
    bool tailRed = false, tailPlain = false;
    for (const auto& f : formatsOn(&edit, 0)) {
      if (f.start <= 9 || f.format.underlineStyle() == QTextCharFormat::WaveUnderline) continue;
      if (f.format.foreground().color() == danger) tailRed = true;
      else tailPlain = true;
    }
    check(danger.isValid() && tailRed, "the rest of the errored line is red too");
    check(!tailPlain, "…every span on it, none left in its own ink");
  }

  std::printf("Run asks its host instead of closing the window:\n");
  {
    ScriptDialog dlg(QStringLiteral("@crop 10%\n@save\n"));
    dlg.show();
    int asked = 0;
    QObject::connect(&dlg, &ScriptDialog::runRequested, [&asked] { ++asked; });
    auto* run = dlg.findChild<QPushButton*>(QStringLiteral("scriptRun"));
    check(run && run->isEnabled(), "Run is live for a script with something to do");
    if (run) run->click();
    check(asked == 1, "the click asks the host to run");
    // The old exec-loop accepted here, which closed the window and re-opened it — a flicker.
    check(dlg.isVisible(), "and the window is still open afterwards");
    check(dlg.result() != QDialog::Accepted, "Run never accepts the dialog");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
