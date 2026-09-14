// The one session-scoped .stc (model/ScriptBuffer) both desktop hosts edit: the script
// window (dialogs/ScriptDialog) and the context menu's flyout (dialogs/ScriptMenuPanel).
// Typing in either is there when the other opens, closing one keeps it, Clear empties both,
// and none of it is ever written to settings, a project or a file.
#include "ScriptBuffer.hpp"
#include "ScriptDialog.hpp"
#include "ScriptMenuPanel.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <cstdio>

#include "support/check.hpp"

using stencil::gui::ScriptDialog;
using stencil::gui::ScriptMenuPanel;
using stencil::model::ScriptBuffer;

namespace {

  QPushButton* button(const QWidget& host, const char* name) {
    return host.findChild<QPushButton*>(QString::fromLatin1(name));
  }

  // The window's footer buttons carry no object name; they are found by their label.
  QPushButton* labelled(const QWidget& host, const QString& text) {
    for (QPushButton* b : host.findChildren<QPushButton*>())
      if (b->text() == text) return b;
    return nullptr;
  }

  // Anything under the isolated state dir that carries the script would be a leak.
  bool stateDirMentions(const QString& needle) {
    const QString dir = qEnvironmentVariable("STENCIL_STATE_DIR");
    if (dir.isEmpty() || !QDir(dir).exists()) return false;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      QFile f(it.next());
      if (f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains(needle))
        return true;
    }
    return false;
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  std::printf("the two hosts edit one script:\n");
  {
    ScriptBuffer::instance().setText(QString());
    ScriptMenuPanel panel(nullptr, {});
    panel.setScript(QStringLiteral("@filter bw\n"));
    check(ScriptBuffer::instance().text() == QStringLiteral("@filter bw\n"),
          "what the flyout holds IS the shared buffer");

    ScriptDialog dlg{QString()};
    check(dlg.script() == QStringLiteral("@filter bw\n"),
          "the window opens on what the flyout was left holding");

    dlg.findChild<QPlainTextEdit*>(QStringLiteral("scriptText"))
        ->setPlainText(QStringLiteral("@crop 10%\n"));
    check(panel.script() == QStringLiteral("@crop 10%\n"),
          "…and typing in the window reaches the flyout that is still open");
  }

  std::printf("closing a host keeps the script:\n");
  {
    ScriptBuffer::instance().setText(QString());
    {
      ScriptDialog dlg{QString()};
      dlg.findChild<QPlainTextEdit*>(QStringLiteral("scriptText"))
          ->setPlainText(QStringLiteral("@save\n"));
    }
    check(ScriptBuffer::instance().text() == QStringLiteral("@save\n"),
          "the window closing does not take the script with it");
    ScriptMenuPanel panel(nullptr, {});
    check(panel.script() == QStringLiteral("@save\n"), "a later flyout opens on it");
  }

  std::printf("Clear empties the buffer and resets the editor with it:\n");
  {
    ScriptBuffer::instance().setText(QString());
    ScriptMenuPanel panel(nullptr, {});
    panel.setScript(QStringLiteral("@nope 1\n"));
    panel.showRunDiagnostics();
    auto* diag = panel.findChild<QLabel*>(QStringLiteral("scriptMenuDiag"));
    auto* clear = button(panel, "scriptMenuClear");
    check(clear && clear->isEnabled(), "Clear is live while there is text");
    check(diag && !diag->text().isEmpty(), "the strip is reporting the failed run");

    clear->click();
    check(panel.script().isEmpty(), "Clear empties the editor");
    check(ScriptBuffer::instance().text().isEmpty(), "…and the shared buffer with it");
    check(diag && diag->text().isEmpty() && !diag->property("state").isValid(),
          "the diagnostics strip resets");
    check(clear && !clear->isEnabled(), "Clear goes dead, gated like Copy and Download");
    auto* run = button(panel, "scriptMenuRun");
    check(run && !run->isEnabled(), "…and so does Run, re-gated on the empty program");

    ScriptDialog dlg{QString()};
    check(dlg.script().isEmpty(), "the window opens empty after the flyout's Clear");
    auto* windowClear = labelled(dlg, QStringLiteral("Clear"));
    check(windowClear && !windowClear->isEnabled(), "the window carries its own Clear, gated");
  }

  std::printf("Clear sits past Run, in the shared danger red:\n");
  {
    const stencil::gui::Palette pal = stencil::gui::themePalette(true, QStringLiteral("violet"));
    qApp->setStyleSheet(stencil::gui::buildStylesheet(true, QStringLiteral("violet")));
    ScriptBuffer::instance().setText(QStringLiteral("@filter bw\n"));
    ScriptMenuPanel panel(nullptr, {});
    panel.restyle(pal);
    panel.show();
    QApplication::processEvents();
    QPushButton* red = button(panel, "scriptMenuClear");
    QPushButton* run = button(panel, "scriptMenuRun");
    // It throws work away, so a mis-click on the way to the primary action must not reach it.
    check(red && run && run->x() < red->x(), "Clear is the last thing in the flyout's row");
    check(red && red->property("dangerCta").toBool() && !red->property("accentCta").toBool(),
          "…wearing the shared danger face, not the accent CTA");
    check(red && red->grab().toImage().pixelColor(4, red->height() / 2) == pal.danger,
          "…and painting that red when live");
    check(red && !red->icon().isNull(), "…with the trash glyph (trash deletes; eraser wipes)");

    // The row does not wrap, so anything it cannot fit is CUT, label first.
    bool rowFits = true;
    for (const char* n : {"scriptMenuCopy", "scriptMenuDownload", "scriptMenuUpload",
                          "scriptMenuRun", "scriptMenuClear"}) {
      QPushButton* b = button(panel, n);
      if (!b || b->x() < 0 || b->x() + b->width() > panel.width()
          || b->width() < b->sizeHint().width()) rowFits = false;
    }
    check(rowFits, "every action fits the flyout whole — a sixth button would trip this");

    ScriptDialog dlg{QString()};
    dlg.show();
    QApplication::processEvents();
    QPushButton* windowClear = labelled(dlg, QStringLiteral("Clear"));
    QPushButton* windowRun = dlg.findChild<QPushButton*>(QStringLiteral("scriptRun"));
    check(windowClear && windowRun && windowRun->x() < windowClear->x()
              && windowClear->property("dangerCta").toBool(),
          "the window's footer says the same thing: Clear last, and red");
  }

  std::printf("nothing about the script is persisted:\n");
  {
    ScriptBuffer::instance().setText(QStringLiteral("@crop 10% # secret\n"));
    ScriptMenuPanel panel(nullptr, {});
    QApplication::processEvents();
    check(!stateDirMentions(QStringLiteral("secret")),
          "no settings, session or project file under the state dir carries it");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
