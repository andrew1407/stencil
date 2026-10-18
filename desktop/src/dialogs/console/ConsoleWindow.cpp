#include "ConsoleWindow.hpp"
#include "consoleGrammar.hpp"
#include "../../support/skinPrefs.hpp"
#include "modalChrome.hpp"
#include "modalReveal.hpp"
#include "../../support/console/TypewriterReveal.hpp"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSizeGrip>
#include <QStyle>

namespace stencil::gui {

  namespace {
    constexpr int MIN_W = 420;
    constexpr int MIN_H = 260;
    // Browser .console-modal: 640 wide, under 560 tall.
    constexpr int OPEN_W = 640;
    constexpr int OPEN_H = 420;
  }  // namespace

  ConsoleWindow::ConsoleWindow(QWidget* parent, Hooks hooks) : QDialog(parent), hooks(std::move(hooks)), webcore(support::isWebcore()) {
    setObjectName(QStringLiteral("stencilConsoleDialog"));
    // A tool window: above its editor, never application-modal, no backdrop of its own.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setWindowModality(Qt::NonModal);
    setProperty(support::NO_OUTSIDE_DISMISS_PROPERTY, true);
    setProperty(support::NO_DIALOG_REVEAL_PROPERTY, true);
    setMinimumSize(MIN_W, MIN_H);
    resize(OPEN_W, OPEN_H);

    chrome = installModalChrome(this, QStringLiteral("terminal"), tr("Console"));
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    // One field, as a terminal is: the scrollback with the prompt line as its last row.
    auto* box = new QFrame(this);
    box->setObjectName(QStringLiteral("consoleBox"));
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    output = new QPlainTextEdit(box);
    output->setObjectName(QStringLiteral("consoleOutput"));
    output->setReadOnly(true);
    output->setFont(mono);
    output->setLineWrapMode(QPlainTextEdit::WidgetWidth);   // long lines wrap, as the Zig scrollback does
    output->setFocusPolicy(Qt::NoFocus);
    output->setFrameShape(QFrame::NoFrame);
    col->addWidget(output, 1);
    auto* rule = new QFrame(box);   // the Zig screen's rule between the scrollback and the prompt
    rule->setObjectName(QStringLiteral("consoleRule"));
    rule->setFixedHeight(1);
    col->addWidget(rule);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(8, 4, 4, 4);
    row->setSpacing(6);
    auto* mark = new QLabel(QStringLiteral(">"), box);
    mark->setObjectName(QStringLiteral("consolePromptMark"));
    mark->setFont(mono);
    prompt = new QLineEdit(box);
    prompt->setObjectName(QStringLiteral("consolePrompt"));
    prompt->setFont(mono);
    prompt->setFrame(false);
    prompt->setClearButtonEnabled(false);
    prompt->installEventFilter(this);
    // The word being typed wears the accent once it names a command (line_edit/render.zig).
    connect(prompt, &QLineEdit::textChanged, this, [this](const QString& text) {
      const support::ParsedConsoleLine parsed = support::parseConsoleLine(text);
      const QString word = support::resolveConsoleCommand(parsed.word, false) ? QStringLiteral("cmd") : QString();
      if (prompt->property("word").toString() == word) return;
      prompt->setProperty("word", word);
      prompt->style()->unpolish(prompt);
      prompt->style()->polish(prompt);
    });
    row->addWidget(mark);
    row->addWidget(prompt, 1);
    row->addWidget(new QSizeGrip(box), 0, Qt::AlignBottom | Qt::AlignRight);
    col->addLayout(row);
    chrome.body->addWidget(box, 1);

    reveal = new support::TypewriterReveal(this);
    reveal->setSpeed([this] { return this->hooks.speed ? this->hooks.speed() : support::consoleSpeedRange().def; });
    connect(reveal, &support::TypewriterReveal::trimmed, this, &ConsoleWindow::resetDocument);
    connect(reveal, &support::TypewriterReveal::changed, this, &ConsoleWindow::repaintTail);
    print({{tr("Console — '/command <args>' (the '/' is optional). Tab completes, Up/Down recall history; '/help' lists commands, '/exit' closes."),
            support::ConsoleKind::MUTED}});
  }

  void ConsoleWindow::print(const QVector<support::ConsoleLine>& lines) { reveal->enqueue(lines); }

  void ConsoleWindow::clearOutput() { reveal->clear(); }

  void ConsoleWindow::reject() {
    if (isVisible()) emit closing();
    QDialog::reject();
  }

  void ConsoleWindow::focusPrompt() { prompt->setFocus(Qt::OtherFocusReason); }

  QString ConsoleWindow::outputText() const { return output->toPlainText(); }

  QVector<support::ConsoleLine> ConsoleWindow::scrollbackLines() const {
    QVector<support::ConsoleLine> out;
    for (int i = 0; i < reveal->count(); ++i) out.push_back({reveal->at(i).text, reveal->at(i).kind, reveal->at(i).head});
    return out;
  }

  void ConsoleWindow::applyEffect(support::ConsoleEffect effect) {
    switch (effect) {
      case support::ConsoleEffect::CLEAR: clearOutput(); break;
      case support::ConsoleEffect::EXIT: reject(); break;
      case support::ConsoleEffect::NONE: break;
    }
  }

  QColor ConsoleWindow::inkFor(support::ConsoleKind kind) const {
    using support::ConsoleKind;
    switch (kind) {
      case ConsoleKind::ECHO:
      case ConsoleKind::SECTION:
      case ConsoleKind::TITLE: return hooks.accent ? hooks.accent() : palette().color(QPalette::Highlight);
      case ConsoleKind::ERROR: return hooks.danger ? hooks.danger() : QColor(Qt::red);
      case ConsoleKind::SECRET: return hooks.gold ? hooks.gold() : QColor(0xff, 0xd7, 0x00);
      case ConsoleKind::MUTED: return hooks.muted ? hooks.muted() : palette().color(QPalette::PlaceholderText);
      case ConsoleKind::PLAIN: break;
    }
    return hooks.ink ? hooks.ink() : palette().color(QPalette::Text);
  }

}  // namespace stencil::gui
