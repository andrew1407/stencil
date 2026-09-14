#include "ScriptEditorWidget.hpp"

#include "ScriptHighlighter.hpp"

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
      : QWidget(parent), style_(style) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(style_.stripGap);

    /* The halo's border is always the same width and only changes colour: a graphics effect
     * is clipped by the layout, and growing a border on hover would shift every glyph under
     * the pointer. The wrap inside it owns the box (browser .script-editor-wrap). */
    glow_ = new QFrame(this);
    glow_->setObjectName(style_.glowName);
    auto* glowLayout = new QVBoxLayout(glow_);
    glowLayout->setContentsMargins(0, 0, 0, 0);
    if (style_.hoverOnFrame) {
      glow_->setAttribute(Qt::WA_Hover, true);
      glow_->installEventFilter(this);
    }

    wrap_ = new QFrame(glow_);
    wrap_->setObjectName(style_.wrapName);
    auto* wrapLayout = new QVBoxLayout(wrap_);
    wrapLayout->setContentsMargins(style_.padX, style_.padY, style_.padX, style_.padY);
    glowLayout->addWidget(wrap_);

    edit_ = new QPlainTextEdit(wrap_);
    edit_->setObjectName(style_.editName);
    edit_->setPlaceholderText(QStringLiteral("@crop 10%\n@filter bw\n@save"));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);   // a script is code; let it scroll
    edit_->setFrameShape(QFrame::NoFrame);
    // The frame owns the box; the style's own focus ring would draw a second one inside it.
    edit_->setAttribute(Qt::WA_MacShowFocusRect, false);
    // The desktop twin of the flyout's data-ctx-keep-tab: a code editor owns Tab, so the
    // menu's Tab-walks-the-controls navigation steps aside for it (stayOpenMenuStops.hpp).
    if (style_.codeKeys) edit_->setProperty("keepTab", true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(style_.fontPx);
    edit_->setFont(mono);
    if (style_.tabStops)
      edit_->setTabStopDistance(style_.indent *
                                edit_->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    edit_->installEventFilter(this);
    applyLineHeight();
    wrapLayout->addWidget(edit_);
    wrap_->setMinimumHeight(style_.editorMinH);
    col->addWidget(glow_, 1);   // the editor takes whatever the strip and the row leave

    diag_ = new QLabel(this);
    diag_->setObjectName(style_.diagName);
    diag_->setWordWrap(true);
    col->addWidget(diag_);

    highlighter_ = new ScriptHighlighter(edit_->document());

    // Painting is a lex of one screenful, so it runs on the keystroke: the colours ARE the
    // text as far as the reader is concerned, and a deferred paint reads as lag.
    connect(edit_, &QPlainTextEdit::textChanged, this, [this] {
      if (painting_) return;
      const QString text = edit_->toPlainText();   // QPlainTextEdit reassembles it, so read once
      parseAndPaint(text, false);   // editing also clears the verdict: it was about older text
      emit edited(text.trimmed().isEmpty());
    });
    recolour();
  }

  QString ScriptEditorWidget::script() const { return edit_->toPlainText(); }

  void ScriptEditorWidget::setScript(const QString& text) {
    edit_->setPlainText(text);
    applyLineHeight();   // setPlainText resets the document's block formats
    edit_->moveCursor(QTextCursor::End);
  }

  bool ScriptEditorWidget::isEmpty() const { return edit_->toPlainText().trimmed().isEmpty(); }

  void ScriptEditorWidget::copyToClipboard() const {
    QApplication::clipboard()->setText(edit_->toPlainText());
  }

  void ScriptEditorWidget::recolour() { parseAndPaint(edit_->toPlainText(), false); }

  void ScriptEditorWidget::showRunDiagnostics() {
    // The text has not changed since the run, so the parse it ran from still stands.
    painting_ = true;
    highlighter_->setProgram(program_, true);
    painting_ = false;
    showFirstDiagnostic(true);
  }

  void ScriptEditorWidget::restyleFormats() {
    painting_ = true;
    highlighter_->restyle();   // the formats hold resolved colours
    painting_ = false;
    diag_->style()->unpolish(diag_);
    diag_->style()->polish(diag_);
  }

  void ScriptEditorWidget::parseAndPaint(const QString& text, bool withDiagnostics) {
    program_ = model::ScriptDoc::parse(text);
    painting_ = true;
    highlighter_->setProgram(program_, withDiagnostics);
    painting_ = false;
    showFirstDiagnostic(withDiagnostics);
  }

  // An error wins over a warning; nothing at all until a run has reported.
  void ScriptEditorWidget::showFirstDiagnostic(bool withDiagnostics) {
    const model::ScriptDiagnostic* first = nullptr;
    if (withDiagnostics) {
      for (const model::ScriptDiagnostic& d : program_.diagnostics()) {
        if (d.isError) { first = &d; break; }
        if (!first) first = &d;
      }
    }
    diag_->setText(first ? tr("Line %1:%2 — %3").arg(first->line).arg(first->col).arg(first->message)
                         : QString());
    diag_->setProperty("state", first ? (first->isError ? QStringLiteral("error")
                                                        : QStringLiteral("warn"))
                                      : QVariant());
    diag_->style()->unpolish(diag_);
    diag_->style()->polish(diag_);
  }

  /* Hover lights the halo in the accent, focus thickens the border — the browser's
   * .script-editor-wrap :hover / :focus-within, which QSS has no box-shadow for. Under a popup
   * grab the editor is the only child the menu synthesises an Enter for, so the flyout reads
   * the hover off it instead of off the halo. */
  bool ScriptEditorWidget::eventFilter(QObject* watched, QEvent* event) {
    if (style_.hoverOnFrame) {
      if (watched == glow_) {
        if (event->type() == QEvent::Enter) setFrameState("hovered", true);
        else if (event->type() == QEvent::Leave) setFrameState("hovered", false);
      } else if (watched == edit_) {
        if (event->type() == QEvent::FocusIn) setFrameState("focused", true);
        else if (event->type() == QEvent::FocusOut) setFrameState("focused", false);
      }
      return QWidget::eventFilter(watched, event);
    }
    if (watched != edit_) return QWidget::eventFilter(watched, event);
    switch (event->type()) {
      case QEvent::Enter: setFrameState("hovered", true); break;
      case QEvent::Leave: setFrameState("hovered", false); break;
      case QEvent::FocusIn: setFrameState("focused", true); break;
      case QEvent::FocusOut: setFrameState("focused", false); break;
      case QEvent::KeyPress: {
        if (!style_.codeKeys) break;
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Tab && !(ke->modifiers() & Qt::ShiftModifier)) {
          edit_->insertPlainText(QString(style_.indent, QLatin1Char(' ')));
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
    for (QFrame* f : {glow_, wrap_}) {
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
    QTextCursor cur(edit_->document());
    cur.select(QTextCursor::Document);
    QTextBlockFormat fmt;
    fmt.setLineHeight(style_.lineHeightPct, QTextBlockFormat::ProportionalHeight);
    cur.mergeBlockFormat(fmt);
    edit_->document()->clearUndoRedoStacks();
  }

}  // namespace stencil::gui
