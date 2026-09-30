// The script window's keys and face (dialogs/ScriptDialog, dialogs/ScriptEditorWidget): Ctrl+Enter
// runs as its Run tooltip says, the toolbar popover's bar fits its labels, and the editor's example
// is half the theme's placeholder ink.
// Browser twin: scriptModal.test.js.
#include "ScriptBuffer.hpp"
#include "ScriptDialog.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::ScriptDialog;

namespace {

  template <class W>
  W* find(const QWidget& host, const char* name) {
    return host.findChild<W*>(QString::fromLatin1(name));
  }

  void freshBuffer() { stencil::model::ScriptBuffer::instance().setText(QString()); }

  void sendKey(QWidget* w, int key, Qt::KeyboardModifiers mods) {
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(w, &press);
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  std::printf("the window's Ctrl+Enter runs, as its tooltip says:\n");
  {
    freshBuffer();
    ScriptDialog dlg{QString()};
    int runs = 0;
    QObject::connect(&dlg, &ScriptDialog::runRequested, [&runs] { ++runs; });
    auto* edit = find<QPlainTextEdit>(dlg, "scriptText");
    sendKey(edit, Qt::Key_Return, Qt::ControlModifier);
    check(runs == 0, "an empty editor has nothing to run");
    edit->setPlainText(QStringLiteral("@filter bw\n"));
    sendKey(edit, Qt::Key_Return, Qt::ControlModifier);
    check(runs == 1, "Ctrl+Enter runs the script");
    const QString before = edit->toPlainText();
    sendKey(edit, Qt::Key_Tab, Qt::NoModifier);
    check(edit->toPlainText() != before + QStringLiteral("  "), "Tab stays the window's own tab stop");
  }

  std::printf("as a toolbar popover the bar fits its labels whole:\n");
  {
    freshBuffer();
    app.setStyleSheet(stencil::gui::buildStylesheet(true));
    QWidget overlay;   // what execMaybePopover reparents the dialog into, at its 420 cap
    overlay.setObjectName(QStringLiteral("popoverOverlay"));
    ScriptDialog dlg{QString()};
    dlg.setParent(&overlay, Qt::Widget);
    dlg.setMinimumSize(0, 0);
    dlg.setGeometry(0, 0, 420, 400);
    overlay.resize(420, 400);
    overlay.show();
    QApplication::processEvents();
    bool whole = true;
    for (auto* b : dlg.findChildren<QPushButton*>())
      if (!b->text().isEmpty() && b->objectName() != QStringLiteral("modalClosePill")
          && (b->width() < b->sizeHint().width() || b->geometry().right() >= dlg.width()))
        whole = false;
    check(whole, "no button is squeezed under its label or past the edge");
    dlg.setParent(nullptr);   // back before `overlay` goes, or it deletes a stack object
    app.setStyleSheet(QString());
  }

  std::printf("the example is faded, and follows the theme:\n");
  {
    ScriptDialog dlg{QString()};
    auto* edit = find<QPlainTextEdit>(dlg, "scriptText");
    const auto faded = [edit] {
      const QColor theme = QApplication::palette(edit).color(QPalette::PlaceholderText);
      return std::abs(edit->palette().color(QPalette::PlaceholderText).alphaF() - theme.alphaF() * 0.5) < 0.01;
    };
    check(faded(), "the placeholder is half the theme's ink");
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::PlaceholderText, QColor(10, 200, 30));
    pal.setColor(QPalette::Text, QColor(20, 20, 20));   // a flip replaces the whole palette
    QApplication::setPalette(pal);
    QApplication::processEvents();
    check(faded() && edit->palette().color(QPalette::PlaceholderText).green() == 200, "a theme flip re-fades it");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
