#include "ScriptEditorWidget.hpp"

#include "ScriptHighlighter.hpp"
#include "../model/ScriptBuffer.hpp"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QVBoxLayout>

namespace stencil::gui {

  ScriptEditorWidget::ScriptEditorWidget(QWidget* parent, const Style& style)
      : QWidget(parent), style(style) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(this->style.stripGap);

    /* The halo's border is always the same width and only changes colour: a graphics effect
     * is clipped by the layout, and growing a border on hover would shift every glyph under
     * the pointer. The wrap inside it owns the box (browser .script-editor-wrap). */
    glow = new QFrame(this);
    glow->setObjectName(this->style.glowName);
    auto* glowLayout = new QVBoxLayout(glow);
    glowLayout->setContentsMargins(0, 0, 0, 0);
    if (this->style.hoverOnFrame) {
      glow->setAttribute(Qt::WA_Hover, true);
      glow->installEventFilter(this);
    }

    wrap = new QFrame(glow);
    wrap->setObjectName(this->style.wrapName);
    auto* wrapLayout = new QVBoxLayout(wrap);
    wrapLayout->setContentsMargins(this->style.padX, this->style.padY, this->style.padX, this->style.padY);
    glowLayout->addWidget(wrap);

    edit = new QPlainTextEdit(wrap);
    edit->setObjectName(this->style.editName);
    edit->setPlaceholderText(QStringLiteral("@crop 10%\n@filter bw\n@save"));
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);   // a script is code; let it scroll
    edit->setFrameShape(QFrame::NoFrame);
    // The frame owns the box; the style's own focus ring would draw a second one inside it.
    edit->setAttribute(Qt::WA_MacShowFocusRect, false);
    // The desktop twin of the flyout's data-ctx-keep-tab: a code editor owns Tab, so the
    // menu's Tab-walks-the-controls navigation steps aside for it (stayOpenMenuStops.hpp).
    if (this->style.codeKeys) edit->setProperty("keepTab", true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(this->style.fontPx);
    edit->setFont(mono);
    if (this->style.tabStops)
      edit->setTabStopDistance(this->style.indent *
                                edit->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    edit->installEventFilter(this);
    applyLineHeight();
    wrapLayout->addWidget(edit);
    wrap->setMinimumHeight(this->style.editorMinH);
    col->addWidget(glow, 1);   // the editor takes whatever the strip and the row leave

    diag = new QLabel(this);
    diag->setObjectName(this->style.diagName);
    diag->setWordWrap(true);
    col->addWidget(diag);

    highlighter = new ScriptHighlighter(edit->document());

    // Painting is a lex of one screenful, so it runs on the keystroke: the colours ARE the
    // text as far as the reader is concerned, and a deferred paint reads as lag.
    connect(edit, &QPlainTextEdit::textChanged, this, [this] {
      if (painting) return;
      const QString text = edit->toPlainText();   // QPlainTextEdit reassembles it, so read once
      parseAndPaint(text, false);   // editing also clears the verdict: it was about older text
      emit edited();
    });
    recolour();

    /* Both hosts edit ONE session-scoped script (model::ScriptBuffer): what is typed in the
     * window is there when the flyout opens, and the other way round. Nothing is persisted —
     * a script may be pasted from anywhere, so it dies with the process. */
    model::ScriptBuffer& shared = model::ScriptBuffer::instance();
    if (!shared.getText().isEmpty()) setScript(shared.getText());
    connect(this, &ScriptEditorWidget::edited, this,
            [this] { model::ScriptBuffer::instance().setText(script()); });
    connect(&shared, &model::ScriptBuffer::changed, this,
            [this](const QString& text) { if (text != script()) setScript(text); });
  }

  QString ScriptEditorWidget::script() const { return edit->toPlainText(); }

  void ScriptEditorWidget::setScript(const QString& text) {
    edit->setPlainText(text);
    applyLineHeight();   // setPlainText resets the document's block formats
    edit->moveCursor(QTextCursor::End);
  }

  bool ScriptEditorWidget::isEmpty() const { return edit->toPlainText().trimmed().isEmpty(); }

  bool ScriptEditorWidget::isIdle() const {
    return program.getOps().isEmpty() && program.getDiagnostics().isEmpty();
  }

  void ScriptEditorWidget::copyToClipboard() const {
    QApplication::clipboard()->setText(edit->toPlainText());
  }

  void ScriptEditorWidget::recolour() { parseAndPaint(edit->toPlainText(), false); }

  void ScriptEditorWidget::showRunDiagnostics() {
    // The text has not changed since the run, so the parse it ran from still stands.
    painting = true;
    highlighter->setProgram(program, true);
    painting = false;
    showFirstDiagnostic(true);
  }

  void ScriptEditorWidget::restyleFormats() {
    painting = true;
    highlighter->restyle();   // the formats hold resolved colours
    painting = false;
    diag->style()->unpolish(diag);
    diag->style()->polish(diag);
  }

  void ScriptEditorWidget::parseAndPaint(const QString& text, bool withDiagnostics) {
    program = model::ScriptDoc::parse(text);
    painting = true;
    highlighter->setProgram(program, withDiagnostics);
    painting = false;
    showFirstDiagnostic(withDiagnostics);
  }

  // An error wins over a warning; nothing at all until a run has reported.
  void ScriptEditorWidget::showFirstDiagnostic(bool withDiagnostics) {
    const model::ScriptDiagnostic* first = nullptr;
    if (withDiagnostics) {
      for (const model::ScriptDiagnostic& d : program.getDiagnostics()) {
        if (d.isError) { first = &d; break; }
        if (!first) first = &d;
      }
    }
    diag->setText(first ? tr("Line %1:%2 — %3").arg(first->line).arg(first->col).arg(first->message)
                         : QString());
    diag->setProperty("state", first ? (first->isError ? QStringLiteral("error")
                                                        : QStringLiteral("warn"))
                                      : QVariant());
    diag->style()->unpolish(diag);
    diag->style()->polish(diag);
  }

  /* Hover lights the halo in the accent, focus thickens the border — the browser's
   * .script-editor-wrap :hover / :focus-within, which QSS has no box-shadow for. Under a popup
   * grab the editor is the only child the menu synthesises an Enter for, so the flyout reads
   * the hover off it instead of off the halo. */
  bool ScriptEditorWidget::eventFilter(QObject* watched, QEvent* event) {
    if (style.hoverOnFrame) {
      if (watched == glow) {
        if (event->type() == QEvent::Enter) setFrameState("hovered", true);
        else if (event->type() == QEvent::Leave) setFrameState("hovered", false);
      } else if (watched == edit) {
        if (event->type() == QEvent::FocusIn) setFrameState("focused", true);
        else if (event->type() == QEvent::FocusOut) setFrameState("focused", false);
      }
      return QWidget::eventFilter(watched, event);
    }
    if (watched != edit) return QWidget::eventFilter(watched, event);
    switch (event->type()) {
      case QEvent::Enter: setFrameState("hovered", true); break;
      case QEvent::Leave: setFrameState("hovered", false); break;
      case QEvent::FocusIn: setFrameState("focused", true); break;
      case QEvent::FocusOut: setFrameState("focused", false); break;
      case QEvent::KeyPress: {
        if (!style.codeKeys) break;
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Tab && !(ke->modifiers() & Qt::ShiftModifier)) {
          edit->insertPlainText(QString(style.indent, QLatin1Char(' ')));
          return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            (ke->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
          emit runRequested();
          return true;
        }
        break;
      }
      default: break;
    }
    return QWidget::eventFilter(watched, event);
  }

  void ScriptEditorWidget::setFrameState(const char* key, bool on) {
    for (QFrame* f : {glow, wrap}) {
      if (!f) continue;
      f->setProperty(key, on);
      f->style()->unpolish(f);
      f->style()->polish(f);
    }
  }

  /* Qt has no line-height, so the leading is a block format. Pressing Return copies the
   * current block's format into the new one, so this runs once per document — after the
   * ctor and after any setPlainText, which resets the document to its defaults. */
  void ScriptEditorWidget::applyLineHeight() {
    QTextCursor cur(edit->document());
    cur.select(QTextCursor::Document);
    QTextBlockFormat fmt;
    fmt.setLineHeight(style.lineHeightPct, QTextBlockFormat::ProportionalHeight);
    cur.mergeBlockFormat(fmt);
    edit->document()->clearUndoRedoStacks();
  }

}  // namespace stencil::gui
