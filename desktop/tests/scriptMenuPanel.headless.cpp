// The script flyout hosted in the canvas context menu (dialogs/ScriptMenuPanel). Browser twin
// browser/tests/ctxScript.test.js: it behaves like the window it mirrors (nothing reported
// until a run, the acting buttons dead while empty) and like a code editor (Tab, Ctrl+Enter).
#include "ScriptBuffer.hpp"
#include "ScriptMenuPanel.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QImage>
#include <QClipboard>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextLayout>
#include <cstdio>

#include "support/check.hpp"

using stencil::gui::ScriptMenuPanel;

namespace {

  template <class W>
  W* find(const ScriptMenuPanel& panel, const char* name) {
    return panel.findChild<W*>(QString::fromLatin1(name));
  }

  // The fill a footer button actually paints, read left of its label.
  QColor faceOf(QPushButton* b) {
    const QImage im = b->grab().toImage();
    return im.pixelColor(4, im.height() / 2);
  }

  // The shared script buffer is session-scoped; each block here is its own session.
  void freshBuffer() { stencil::model::ScriptBuffer::instance().setText(QString()); }

  QVector<QTextLayout::FormatRange> formatsOn(const QPlainTextEdit* edit, int lineIndex) {
    const QTextBlock block = edit->document()->findBlockByNumber(lineIndex);
    return block.isValid() && block.layout() ? block.layout()->formats()
                                             : QVector<QTextLayout::FormatRange>{};
  }

  void sendKey(QWidget* w, int key, Qt::KeyboardModifiers mods) {
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(w, &press);
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  std::printf("an empty editor disables the acting buttons:\n");
  {
    freshBuffer();
    ScriptMenuPanel panel(nullptr, {});
    auto* run = find<QPushButton>(panel, "scriptMenuRun");
    auto* copy = find<QPushButton>(panel, "scriptMenuCopy");
    auto* download = find<QPushButton>(panel, "scriptMenuDownload");
    auto* upload = find<QPushButton>(panel, "scriptMenuUpload");
    auto* clear = find<QPushButton>(panel, "scriptMenuClear");
    check(run && copy && download && upload && clear,
          "Run, Copy, Download, Upload and Clear are all there");
    check(run && !run->isEnabled(), "Run is dead while the editor is empty");
    check(copy && !copy->isEnabled() && download && !download->isEnabled()
              && clear && !clear->isEnabled(),
          "so are Copy, Download and Clear");
    check(upload && upload->isEnabled(), "Upload always has something to do");

    panel.setScript(QStringLiteral("@filter bw\n"));
    check(run && run->isEnabled(), "typing brings Run back");
    check(panel.script() == QStringLiteral("@filter bw\n"), "script() hands back what was typed");

    panel.setScript(QStringLiteral("   \n"));
    check(run && !run->isEnabled(), "whitespace alone is still empty");

    // Browser twin: gateActions() in js/ui/script/scriptEditor.js.
    panel.setScript(QStringLiteral("# just a comment\n"));
    check(run && !run->isEnabled(), "a comment-only script lowers to no ops: nothing to run");
    check(copy && copy->isEnabled(), "but there is text to copy");

    panel.setScript(QStringLiteral("@nope 1\n"));
    check(run && run->isEnabled(),
          "an errored script still runs: the run is how its errors become visible");
  }

  std::printf("nothing is reported until the script is run:\n");
  {
    freshBuffer();
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
    freshBuffer();
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
    freshBuffer();
    ScriptMenuPanel panel(nullptr, hooks);
    auto* edit = find<QPlainTextEdit>(panel, "scriptMenuText");
    check(edit != nullptr, "the editor is there");
    check(edit && edit->property("keepTab").toBool(),
          "it is flagged as owning Tab (the menu's walk steps aside)");

    sendKey(edit, Qt::Key_Tab, Qt::NoModifier);
    check(panel.script() == QStringLiteral("  "), "Tab indents by two spaces");

    panel.setScript(QStringLiteral("# nothing to run"));
    sendKey(edit, Qt::Key_Return, Qt::ControlModifier);
    check(runs == 0, "Ctrl+Enter obeys the same gate the Run button does");

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
    freshBuffer();
    ScriptMenuPanel panel(nullptr, hooks);
    panel.setScript(QStringLiteral("@save\n"));

    find<QPushButton>(panel, "scriptMenuCopy")->click();
    check(QApplication::clipboard()->text() == QStringLiteral("@save\n"),
          "Copy puts the script on the clipboard");
    check(noticed == QStringLiteral("Script copied"), "and says so");

    find<QPushButton>(panel, "scriptMenuUpload")->click();
    find<QPushButton>(panel, "scriptMenuDownload")->click();
    check(uploads == 1 && downloads == 1,
          "the two file actions are the window's: it dismisses the chain and puts it back");
  }

  std::printf("the footer buttons are FILLED, like the window's and the browser's:\n");
  {
    const stencil::gui::Palette pal = stencil::gui::themePalette(true, QStringLiteral("violet"));
    qApp->setStyleSheet(stencil::gui::buildStylesheet(true, QStringLiteral("violet")));
    freshBuffer();
    ScriptMenuPanel panel(nullptr, {});
    panel.setScript(QStringLiteral("@filter bw\n"));
    panel.show();
    QApplication::processEvents();
    bool allFilled = true, allCta = true;
    for (const char* n : {"scriptMenuCopy", "scriptMenuDownload", "scriptMenuUpload"}) {
      QPushButton* b = find<QPushButton>(panel, n);
      if (!b || faceOf(b) != pal.accent) allFilled = false;
      if (!b || !b->property("accentCta").toBool()) allCta = false;
    }
    check(allCta, "Copy, Download and Upload are all the accent CTA");
    check(allFilled, "…and every one of them paints the accent fill when live");
    // Run is the one GO action, so it is GREEN — not the accent the other three wear.
    QPushButton* go = find<QPushButton>(panel, "scriptMenuRun");
    check(go && go->property("successCta").toBool(), "Run wears the go face, not the accent");
    check(go && faceOf(go) != pal.accent, "…and paints something other than the accent");

    panel.setScript(QString());   // Copy/Download/Run go dead, Upload stays live
    QApplication::processEvents();
    QPushButton* copy = find<QPushButton>(panel, "scriptMenuCopy");
    QPushButton* upload = find<QPushButton>(panel, "scriptMenuUpload");
    check(copy && faceOf(copy) != pal.accent
              && copy->palette().color(QPalette::ButtonText) == pal.disabledText,
          "a dead one wears the shared disabled face, not the accent");
    check(upload && faceOf(upload) == pal.accent, "…while the live one keeps its fill");
  }

  std::printf("the flyout colours by the same rule as the window:\n");
  {
    freshBuffer();
    ScriptMenuPanel panel(nullptr, {});
    panel.setScript(QStringLiteral("@crop 10%\n@nonsense 1\n"));
    auto* edit = find<QPlainTextEdit>(panel, "scriptMenuText");
    const auto real = formatsOn(edit, 0);
    const auto bogus = formatsOn(edit, 1);
    check(!real.isEmpty() && !bogus.isEmpty(), "both lines are coloured");
    if (!real.isEmpty() && !bogus.isEmpty())
      check(real[0].format.foreground().color() != bogus[0].format.foreground().color(),
            "the two hosts share one highlighter: @nonsense is plain text here too");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
